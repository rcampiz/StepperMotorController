"""ESTOP fast path: purge-then-write under the transport lock, chunked drained
transfers that stop within one chunk, worker queue drop, and read-back confirmation."""

import sys
import threading
import time
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent / "src"))

from transport.interface import ITransport, TransportInfo  # noqa: E402
from protocol.client import MotorClient, ProtocolError  # noqa: E402


class FakeTransport(ITransport):
    """Records every write in order; emulates a driver output queue."""

    def __init__(self):
        self.log = []            # ("send"|"purge"|"drain", payload)
        self.wire = bytearray()  # bytes that "left" the port
        self.pending = 0         # bytes handed to the driver but not sent
        self.lock = threading.RLock()
        self.rx_lines = []

    def open(self): return True
    def close(self): pass
    def is_connected(self): return True

    def send(self, data: bytes) -> int:
        with self.lock:
            self.log.append(("send", bytes(data)))
            self.pending += len(data)
            return len(data)

    def send_line(self, line: str) -> int:
        n = self.send((line + "\r\n").encode())
        # Reply only after the command went out (the client drains input first)
        if line.startswith("UI:DISP:BITMAP:RLE"):
            self.rx_lines.append("OK READY " + line.split()[-1])
        return n

    def send_priority(self, data: bytes) -> int:
        with self.lock:
            self.log.append(("purge", self.pending))
            self.pending = 0
            self.log.append(("send", bytes(data)))
            self.wire += data
            return len(data)

    def out_waiting(self) -> int:
        return self.pending

    def drain_output(self, timeout_ms: int = 500) -> bool:
        with self.lock:
            self.log.append(("drain", self.pending))
            self.pending = 0
        return True

    def recv(self, max_bytes=1024, timeout_ms=1000): return b""
    def recv_line(self, timeout_ms=1000):
        return self.rx_lines.pop(0) if self.rx_lines else None
    def available(self): return len(self.rx_lines)
    def flush_input(self): self.rx_lines.clear()
    def flush_output(self): pass
    def get_info(self):
        return TransportInfo("FAKE", "fake", True, {})


def test_estop_fast_purges_then_writes_the_stop_line():
    t = FakeTransport()
    c = MotorClient(t)
    t.send(b"X" * 100)                      # a chunk sitting in the driver
    c.estop_fast()
    assert t.log[-2] == ("purge", 100)      # unsent bytes discarded first
    assert t.log[-1] == ("send", b"SYST:ESTOP\r\n")
    assert c.transfer_cancelled()


def test_estop_fast_with_oob_byte_puts_0x18_first():
    t = FakeTransport()
    c = MotorClient(t)
    c.estop_fast(oob=True)
    assert t.log[-1] == ("send", b"\x18SYST:ESTOP\r\n")


def test_chunk_writes_drain_between_chunks_and_stop_on_cancel():
    t = FakeTransport()
    c = MotorClient(t)
    c._send_chunk(b"a" * 256)
    assert t.log == [("send", b"a" * 256), ("drain", 256)]
    c.cancel_transfers()
    with pytest.raises(ProtocolError):
        c._send_chunk(b"b" * 256)           # refused before writing
    assert t.log[-1] == ("drain", 256)      # nothing more went out
    c.clear_cancel()
    c._send_chunk(b"c")
    assert t.log[-2] == ("send", b"c")


def test_streaming_loop_aborts_within_one_chunk():
    """disp_bitmap_rle: cancel lands after chunk 2 of 4 -> at most that chunk is written."""
    t = FakeTransport()
    c = MotorClient(t)
    written = []
    orig = c._send_chunk

    def send_and_cancel_after_two(chunk):
        orig(chunk)
        written.append(len(chunk))
        if len(written) == 2:
            c.cancel_transfers()           # "ESTOP" from another thread
    c._send_chunk = send_and_cancel_after_two
    with pytest.raises(ProtocolError):
        c.disp_bitmap_rle(0, 0, 240, 20, bytes(1024))
    assert written == [256, 256]


def test_estop_confirmed_requires_latch_and_hiz():
    ok, detail = MotorClient.estop_confirmed({"state": "ESTOP", "motor": {"hi_z": True}})
    assert ok and "ESTOP" in detail
    ok, _ = MotorClient.estop_confirmed({"state": "IDLE", "motor": {"hi_z": True}})
    assert not ok
    ok, _ = MotorClient.estop_confirmed({"state": "ESTOP", "motor": {"hi_z": False}})
    assert not ok
    ok, _ = MotorClient.estop_confirmed({})
    assert not ok


# --- worker side (needs PySide6) ---------------------------------------------

PySide6 = pytest.importorskip("PySide6")




class FakeClient:
    def __init__(self):
        self.cancelled = False
        self.cleared = False
    def is_connected(self): return True
    def cancel_transfers(self): self.cancelled = True
    def clear_cancel(self): self.cleared = True


def test_worker_drops_queue_and_queues_one_confirmation(qapp):
    from gui.serial_worker import SerialWorker, QueuedCommand, CommandTag
    client = FakeClient()
    w = SerialWorker(client)
    dropped = []
    w.estop_dispatched.connect(lambda n: dropped.append(n))
    for i in range(5):
        w.queue_command(f"MOT:MOVE {i}")
    w.set_live_command("MOT:RUN 100 1")
    w.request_estop()
    assert client.cancelled
    assert w._estop_pending
    w._service_estop()
    assert dropped == [5]
    assert w._pending_live is None
    assert client.cleared
    remaining = w._command_queue.get_nowait()
    assert remaining.command == "DIAG:STAT?"
    assert remaining.tag == CommandTag.ESTOP_CONFIRM
    assert w._command_queue.empty()


def test_upload_precondition_requires_idle_hiz():
    ok, why = MotorClient.upload_precondition({})
    assert not ok and "snapshot" in why
    ok, _ = MotorClient.upload_precondition({"state": "RUNNING", "motor": {"hi_z": True}})
    assert not ok
    ok, _ = MotorClient.upload_precondition({"state": "IDLE", "motor": {"hi_z": False}})
    assert not ok
    ok, _ = MotorClient.upload_precondition({"state": "IDLE", "motor": {"hi_z": True, "busy": True}})
    assert not ok
    ok, why = MotorClient.upload_precondition({"state": "IDLE", "motor": {"hi_z": True, "busy": False}})
    assert ok and why == "idle, Hi-Z"
