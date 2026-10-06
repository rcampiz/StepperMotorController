"""Protocol 2 (F12): multi-line replies are terminated. ASCII ends with `OK <n>`; JSON is one
envelope with a `lines` array that the client exposes joined in `raw` for text consumers."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "src"))

from transport.interface import ITransport, TransportInfo  # noqa: E402
from protocol.client import MotorClient  # noqa: E402


class ScriptedTransport(ITransport):
    """Replies are queued when the command goes out (the client drains input first)."""

    def __init__(self, replies):
        self.replies = replies      # command -> list of lines
        self.rx_lines = []
        self.sent = []

    def open(self): return True
    def close(self): pass
    def is_connected(self): return True
    def send(self, data: bytes) -> int: return len(data)

    def send_line(self, line: str) -> int:
        self.sent.append(line)
        self.rx_lines.extend(self.replies.get(line, []))
        return len(line) + 2

    def send_priority(self, data: bytes) -> int: return len(data)
    def out_waiting(self) -> int: return 0
    def drain_output(self, timeout_ms: int = 500) -> bool: return True
    def recv(self, max_bytes=1024, timeout_ms=1000): return b""
    def recv_line(self, timeout_ms=1000):
        return self.rx_lines.pop(0) if self.rx_lines else None
    def available(self): return len(self.rx_lines)
    def flush_input(self): self.rx_lines.clear()
    def flush_output(self): pass
    def get_info(self): return TransportInfo("FAKE", "fake", True, {})


def test_ascii_multiline_stops_at_the_ok_terminator_and_leaves_the_rest():
    t = ScriptedTransport({"DRV:CFG?": ["Motor Config (defaults)", "KVAL: HOLD=29", "OK 2", "!FAULT status=0x0000"]})
    resp = MotorClient(t).send_command_multiline("DRV:CFG?")
    assert resp.success
    assert resp.data["lines"] == ["Motor Config (defaults)", "KVAL: HOLD=29"]
    assert resp.raw == "Motor Config (defaults)\nKVAL: HOLD=29"
    assert t.rx_lines == ["!FAULT status=0x0000"]          # not consumed by the reply


def test_ascii_multiline_with_zero_lines_is_still_a_success():
    t = ScriptedTransport({"SPI_STOP": ["OK 0"]})
    resp = MotorClient(t).send_command_multiline("SPI_STOP")
    assert resp.success
    assert resp.data["lines"] == []


def test_json_multiline_is_one_envelope_exposed_as_joined_text():
    t = ScriptedTransport({"SYST:VER?": ['{"status":"ok","command":"VERSION","data":{"lines":["a","b \\"q\\""],"count":2}}']})
    resp = MotorClient(t).send_command_multiline("SYST:VER?")
    assert resp.success
    assert resp.command == "VERSION"
    assert resp.data["count"] == 2
    assert resp.raw == 'a\nb "q"'


def test_json_error_ends_a_multiline_request():
    t = ScriptedTransport({"DBG:MOTOR": ['{"status":"error","command":"MOTOR_DEBUG","code":"DBG_LOCKED","message":"send DBG:UNLOCK first"}']})
    resp = MotorClient(t).send_command_multiline("DBG:MOTOR")
    assert not resp.success
    assert resp.error_code == "DBG_LOCKED"
