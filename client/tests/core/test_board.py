"""Board against the in-memory fake board: session, telemetry push, dead-man heartbeat,
ESTOP fast path, event gaps, reset handling, multi-line and binary replies, link loss."""

import threading
import time

import pytest

from smc_core import commands as C
from smc_core.board import Board, BoardConfig, EstopResult, Liveness
from smc_core.sim import FakeBoard
from smc_core.transport.memory import MemoryTransport


def wait_for(cond, timeout=3.0, step=0.005):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if cond():
            return True
        time.sleep(step)
    return False


class Rig:
    def __init__(self, **cfg):
        self.t = MemoryTransport()
        self.fake = FakeBoard(self.t)
        self.live = Liveness(max_age_s=0.2)
        self.live.touch()
        self.keep_alive = True
        self._alive_thread = threading.Thread(target=self._toucher, daemon=True)
        self._alive_thread.start()
        defaults = dict(heartbeat_period_s=0.02, heartbeat_timeout_ms=500, telemetry_period_ms=50,
                        reply_timeout_ms=300)
        defaults.update(cfg)
        self.board = Board(self.t, BoardConfig(**defaults), liveness=self.live, name="t")
        self.seen = {k: [] for k in Board.EVENTS}
        for k in Board.EVENTS:
            self.board.subscribe(k, lambda *a, k=k: self.seen[k].append(a))

    def _toucher(self):
        while True:
            if self.keep_alive:
                self.live.touch()
            time.sleep(0.01)

    def open(self):
        assert self.board.open(wait=True, timeout=3.0)
        return self

    def close(self):
        self.board.close()
        self.fake.stop()

    def tx(self, kinds=None):
        return [r.text for r in self.board.tx_log if kinds is None or r.kind in kinds]


@pytest.fixture
def rig():
    r = Rig()
    yield r
    r.close()


def test_session_setup_runs_json_identity_events_telemetry_watchdog(rig):
    rig.open()
    setup = rig.tx({"setup"})
    assert setup == ["FMT JSON", "*IDN?", "SYST:EVT:EN", "SYST:TLM:RATE 50", "SYST:HB:TIMEOUT 500"]
    assert rig.board.identity.serial == "SIM00001" and rig.board.identity.proto == 2
    assert rig.seen["connected"] and rig.seen["session"]
    assert rig.fake.json and rig.fake.hb_timeout_ms == 500


def test_telemetry_is_pushed_not_polled(rig):
    rig.open()
    assert wait_for(lambda: len(rig.seen["telemetry"]) >= 3)
    data, t = rig.seen["telemetry"][-1]
    assert data["state"] == "IDLE" and "motor" in data
    assert "DIAG:STAT?" not in rig.tx()


def test_heartbeat_needs_fresh_liveness_and_the_watchdog_fires_without_it(rig):
    rig.open()
    assert wait_for(lambda: rig.board.hb.acked >= 3)
    rig.keep_alive = False                                   # the frontend freezes
    assert wait_for(lambda: rig.seen["liveness_stale"] and rig.seen["liveness_stale"][-1] == (True,))
    sent = rig.board.hb.sent
    assert wait_for(lambda: rig.fake.state == "ESTOP", timeout=2.0)   # firmware watchdog
    assert rig.board.hb.sent == sent                         # nothing fed it meanwhile
    assert rig.board.hb.suppressed > 0
    rig.keep_alive = True
    assert wait_for(lambda: rig.seen["liveness_stale"][-1] == (False,))


def test_heartbeat_requires_a_liveness_source():
    with pytest.raises(ValueError):
        Board(MemoryTransport(), BoardConfig(heartbeat=True))


def test_heartbeat_lost_after_three_misses(rig):
    rig.open()
    rig.fake.silent = True
    assert wait_for(lambda: rig.seen["heartbeat_lost"], timeout=3.0)
    assert len(rig.seen["heartbeat_lost"]) == 1


def test_estop_purges_writes_oob_then_text_drops_queue_and_confirms(rig):
    rig.open()
    rig.fake.delay_next_s = 0.25                     # keep the worker busy on one request
    busy = rig.board.submit(C.mot_run(800, 1))
    time.sleep(0.02)
    queued = [rig.board.submit(C.mot_move(100, 1)) for _ in range(5)]
    result = rig.board.estop().wait(3.0)
    assert isinstance(result, EstopResult)
    assert result.confirmed, result.detail
    assert result.dropped == 5
    assert all(q.wait(1.0).error_code == "CANCELLED" for q in queued)
    kinds = [k for k, _ in rig.t.log]
    i = kinds.index("purge")
    assert rig.t.log[i + 1][1] == b"\x18SYST:ESTOP\r\n"      # purge, then 0x18 + text
    busy.wait(2.0)
    assert "MOT:MOVE 100 1" not in rig.fake.received         # nothing queued went out


def test_estop_reply_is_not_mistaken_for_the_confirmation_reply(rig):
    rig.open()
    result = rig.board.estop().wait(3.0)
    assert result.confirmed and result.status.get("state") == "ESTOP"
    assert any(r.kind == "stale" for r in rig.board.recorder)  # the OK ESTOP line was set aside


def test_estop_cancels_an_upload_within_one_chunk():
    r = Rig(stream_chunk=64)
    r.open()
    orig = r.t.send

    def slow_send(data):
        time.sleep(0.005)
        return orig(data)

    r.t.send = slow_send
    try:
        ticket = r.board.submit(C.flash_upload_rle(1, bytes(64 * 200)))
        assert wait_for(lambda: any(k == "send" and len(p) == 64 for k, p in r.t.log), 2.0)
        r.board.estop().wait(3.0)
        resp = ticket.wait(3.0)
        assert resp.error_code == "CANCELLED"
        chunks = sum(1 for k, p in r.t.log if k == "send" and len(p) == 64)
        assert chunks < 200
    finally:
        r.close()


def test_event_gap_is_reported_with_types_and_triggers_a_resync(rig):
    rig.open()
    rig.fake.emit_event("MOTION_DONE", 0x7E03)
    assert wait_for(lambda: rig.seen["event"])
    rig.fake.skip_next_seq = 2                      # two event lines lost on the wire
    rig.fake.emit_event("FAULT", 0x1234, "OCD")
    assert wait_for(lambda: rig.seen["event_gap"])
    gap = rig.seen["event_gap"][0][0]
    assert (gap.expected, gap.got, gap.missing) == (2, 4, 2)
    assert (gap.before_type, gap.after_type, gap.critical) == ("MOTION_DONE", "FAULT", True)
    assert wait_for(lambda: "DIAG:STAT?" in rig.tx({"resync"}))
    assert "SYST:EVT:STAT?" in rig.tx({"resync"}) or wait_for(
        lambda: "SYST:EVT:STAT?" in rig.tx({"resync"}))


def test_board_reset_reruns_setup_and_restarts_seq_tracking(rig):
    rig.open()
    rig.fake.emit_event("MOTION_DONE")
    assert wait_for(lambda: rig.seen["event"])
    rig.fake.reboot()
    assert wait_for(lambda: rig.seen["reset"])
    assert wait_for(lambda: rig.tx({"setup"}).count("FMT JSON") == 2)
    assert wait_for(lambda: rig.board.session_ready and rig.fake.json)
    rig.fake.emit_event("MOTION_DONE")               # seq restarts at 1: not a gap
    assert wait_for(lambda: len(rig.seen["event"]) == 2)
    assert not rig.seen["event_gap"]
    assert len(rig.seen["connected"]) == 1


def test_boot_id_change_in_heartbeat_ack_counts_as_a_reset(rig):
    rig.open()
    assert wait_for(lambda: rig.board.boot_id == rig.fake.boot)
    rig.fake.boot = 0x0BADB007                        # rebooted, !RESET line lost
    assert wait_for(lambda: rig.seen["reset"])
    assert rig.board.boot_id == 0x0BADB007


def test_multiline_and_typed_replies(rig):
    rig.open()
    cfg = rig.board.request(C.driver_config())
    assert cfg.success and cfg.lines[-1].startswith("StepMode: 3")
    idn = rig.board.request(C.idn())
    assert idn.data["serial"] == "SIM00001"


def test_binary_upload_with_crc(rig):
    """The payload (which contains 0x18) goes out as base64 text: the fake board decodes it
    strictly, so raw bytes would be refused."""
    rig.open()
    payload = bytes(range(256)) * 3
    resp = rig.board.request(C.disp_bitmap_rle(0, 0, 240, 20, payload))
    assert resp.success, resp.error_message
    assert rig.fake.uploads[-1] == payload


def test_late_reply_after_timeout_is_not_given_to_the_next_request(rig):
    rig.open()
    rig.fake.delay_next_s = 0.5                      # longer than reply_timeout_ms=300
    late = rig.board.request(C.ping(1))
    assert late.error_code == "TIMEOUT"
    nxt = rig.board.request(C.idn())
    assert nxt.success and "serial" in nxt.data


def test_late_reply_with_a_different_json_command_name_is_dropped(rig):
    """SYST:HB:TIMEOUT answers as SET_HEARTBEAT. Seen on COM5 2026-10-17: a late ack shifted
    every later reply by one (DRV:STEP_MODE? "answered" with the watchdog data)."""
    rig.open()
    rig.fake.delay_next_s = 0.5
    late = rig.board.request(C.heartbeat_timeout(500))
    assert late.error_code == "TIMEOUT"
    nxt = rig.board.request(C.step_mode_query())
    assert nxt.success and "step_mode" in nxt.data, nxt
    assert "requested" not in nxt.data


def test_the_tail_of_a_line_cut_at_open_is_not_taken_as_a_reply(rig):
    """Opening COM5 in the middle of a telemetry push delivered its tail as the reply to the
    first command and shifted every later reply by one (seen 2026-10-17)."""
    rig.open()
    rig.fake.delay_next_s = 0.2
    ticket = rig.board.submit(C.step_mode_query())
    time.sleep(0.05)
    rig.fake.t.feed_line('en":true,"revolutions":6,"index_period_us":4298409,"vel_quality":2}}')
    resp = ticket.wait(2.0)
    assert resp.success and "step_mode" in resp.data, resp
    assert any(r.kind == "junk" for r in rig.board.recorder)


def test_coalesced_latest_sends_only_the_newest(rig):
    rig.open()
    rig.fake.delay_next_s = 0.15
    rig.board.submit(C.ping(0))
    time.sleep(0.02)
    for rpm in (10, 20, 30, 40):
        rig.board.set_latest("live", C.mot_run_rpm(rpm, 1))
    assert wait_for(lambda: "MOT:RUN 40.0 RPM 1" in rig.fake.received)
    assert "MOT:RUN 20.0 RPM 1" not in rig.fake.received


def test_jobs_run_on_the_worker_and_keep_the_heartbeat_alive(rig):
    rig.open()
    before = rig.board.hb.acked

    def job(ctx):
        ctx.sleep(0.15)
        return ctx.call(C.status()).data["state"]

    assert rig.board.run_job(job).wait(3.0) == "IDLE"
    assert rig.board.hb.acked > before


def test_link_loss_is_reported(rig):
    rig.open()
    rig.t.fail_writes = True
    assert wait_for(lambda: rig.seen["link_lost"], timeout=2.0)
    assert not rig.board.connected


def test_link_budget_accounts_bytes_by_kind(rig):
    rig.open()
    time.sleep(0.2)
    budget = rig.board.link_budget()
    assert budget["tx:heartbeat"] > 0 and budget["rx:line"] > 0


def test_ascii_multiline_reply_is_collected_until_ok_n(rig):
    """After a reset whose !RESET line was lost the board is back in ASCII mode; a
    multi-line reply then ends with `OK <n>` and must still come back whole."""
    rig.open()
    rig.fake.json, rig.fake.echo = False, True        # silent fall-back to ASCII
    resp = rig.board.request(C.driver_config())
    assert resp.success and resp.lines[0] == "Motor Config (defaults)"
    assert resp.data["count"] == 9 and resp.lines[-1].startswith("StepMode:")
    err = rig.board.request(C.debug_raw("PS01_DIAG"))
    assert not err.success and err.error_code == "UNKNOWN_CMD"


def test_watchdog_is_widened_around_flash_writes_and_restored(rig):
    rig.open()
    rig.board.request(C.cfg_apply())
    seen = rig.fake.received
    i = seen.index("DRV:CFG:APPLY")
    assert seen[i - 1] == "SYST:HB:TIMEOUT 5000"           # widened, never 0
    assert seen[i + 1] == "SYST:HB:TIMEOUT 500"            # restored to the configured value
    assert all(l != "SYST:HB:TIMEOUT 0" for l in seen)
    rig.board.request(C.mot_enable())                      # ordinary commands are untouched
    assert rig.fake.received[-1] == "MOT:EN" and rig.fake.received[-2] != "SYST:HB:TIMEOUT 5000"


def test_defaults_follow_the_link_budget():
    cfg = BoardConfig()
    assert (cfg.heartbeat_timeout_ms, cfg.heartbeat_period_s, cfg.telemetry_period_ms) == (500, 0.1, 200)
    assert cfg.persist_watchdog_ms == 5000
