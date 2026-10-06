"""Heartbeat producer: dead-man gating (A2) and timeout signalling (A4)."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent / "src"))

pytest.importorskip("PySide6")


@pytest.fixture(scope="module")
def qapp():
    from PySide6.QtCore import QCoreApplication
    yield QCoreApplication.instance() or QCoreApplication([])


class Resp:
    def __init__(self, ok=True):
        self.success = ok
        self.data = {"seq": 1, "mcu_tick": 10, "remaining_ms": 1500}


class FakeClient:
    def __init__(self, fail=False):
        self.sent = 0
        self.fail = fail
    def is_connected(self): return True
    def heartbeat(self, seq):
        self.sent += 1
        if self.fail:
            raise RuntimeError("no ack")
        return Resp()


def make_worker(client):
    from gui.serial_worker import SerialWorker
    w = SerialWorker(client)
    w.set_heartbeat_interval(0.050)
    w.start_heartbeat()
    return w


def test_heartbeat_is_sent_only_while_liveness_is_fresh(qapp):
    client = FakeClient()
    w = make_worker(client)
    stale_events = []
    w.liveness_stale.connect(lambda s: stale_events.append(s))

    # Fresh stamp -> heartbeat goes out
    w.touch_liveness(now=1000.0)
    w._liveness_stamp = 1000.0
    import time
    real = time.monotonic
    try:
        time.monotonic = lambda: 1000.05          # 50 ms later: fresh
        w._maybe_send_heartbeat(now_wall=10.0)
        assert client.sent == 1
        # UI "frozen": stamp not refreshed for 1 s -> suppressed, one stale signal
        time.monotonic = lambda: 1001.0
        w._maybe_send_heartbeat(now_wall=10.1)
        w._maybe_send_heartbeat(now_wall=10.2)
        assert client.sent == 1
        assert w.heartbeats_suppressed() == 2
        assert stale_events == [True]
        # UI recovers: stamp refreshed -> heartbeats resume, one recovery signal
        w.touch_liveness(now=1001.0)
        w._maybe_send_heartbeat(now_wall=10.3)
        assert client.sent == 2
        assert stale_events == [True, False]
    finally:
        time.monotonic = real


def test_heartbeat_respects_interval(qapp):
    client = FakeClient()
    w = make_worker(client)
    w.touch_liveness()
    w._maybe_send_heartbeat(now_wall=5.000)
    w._maybe_send_heartbeat(now_wall=5.010)     # 10 ms later: not due
    assert client.sent == 1
    w._maybe_send_heartbeat(now_wall=5.060)
    assert client.sent == 2


def test_liveness_max_age_tracks_interval(qapp):
    w = make_worker(FakeClient())
    w.set_heartbeat_interval(0.010)
    assert w._liveness_max_age == pytest.approx(0.2)    # floor
    w.set_heartbeat_interval(0.100)
    assert w._liveness_max_age == pytest.approx(0.4)    # 4 x interval


def test_heartbeat_timeout_emitted_once_after_three_misses(qapp):
    client = FakeClient(fail=True)
    w = make_worker(client)
    fired = []
    w.heartbeat_timeout.connect(lambda: fired.append(1))
    w.touch_liveness()
    for i in range(5):
        w._maybe_send_heartbeat(now_wall=1.0 + 0.1 * i)
    assert client.sent == 5
    assert fired == [1]                       # once per outage, not per miss
    # An ack re-arms the detector
    client.fail = False
    w._maybe_send_heartbeat(now_wall=2.0)
    assert w._consecutive_misses == 0
    client.fail = True
    for i in range(3):
        w._maybe_send_heartbeat(now_wall=3.0 + 0.1 * i)
    assert fired == [1, 1]
