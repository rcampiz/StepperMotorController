"""
Codec: turn classified lines into plain data (docs/PROTOCOL.md sections 2, 3 and 8).

Pure functions and dataclasses only, no I/O and no threads, so the same code serves
the GUI, the CLI, the scenarios and a future Rust port (the dataclasses are the
interface). Both reply formats are understood; the Board runs in JSON mode.
"""

import json
import re
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional

from .framing import Frame, FrameKind, classify


class ProtocolError(Exception):
    """The board said something the protocol does not allow (or said nothing)."""


@dataclass
class Response:
    """One command reply."""
    success: bool
    command: str
    data: Dict[str, Any] = field(default_factory=dict)
    error_code: Optional[str] = None
    error_message: Optional[str] = None
    raw: str = ""

    @property
    def is_error(self) -> bool:
        return not self.success

    @property
    def lines(self) -> List[str]:
        """Text lines of a multi-line reply (F12), empty otherwise."""
        v = self.data.get("lines") if isinstance(self.data, dict) else None
        return list(v) if isinstance(v, list) else []


@dataclass
class Event:
    """An asynchronous event (`!TYPE ...` / `{"kind":"event",...}`)."""
    type: str
    seq: Optional[int] = None          # JSON only: per-boot, counts event lines sent
    ts_ms: Optional[int] = None
    status: Optional[int] = None       # powerSTEP01 STATUS register
    detail: Optional[str] = None
    data: Dict[str, Any] = field(default_factory=dict)
    raw: str = ""


@dataclass
class ResetInfo:
    """The `!RESET` line: the board has (re)booted."""
    boot: int
    fw: str
    proto: int
    build: str
    raw: str = ""
    rst: str = "?"        # MCU reset cause: PIN, POR, SFT, IWDG, WWDG, LPWR, BOR (joined with +)


@dataclass
class Identity:
    """`*IDN?` -> vendor,model,serial,fw,proto."""
    vendor: str
    model: str
    serial: str
    fw: str
    proto: int


@dataclass
class HeartbeatAck:
    seq: int
    mcu_tick: int
    remaining_ms: int
    boot: Optional[int] = None


_CODE_RE = re.compile(r"^[A-Z][A-Z0-9_]+$")


# ---------------------------------------------------------------------------
# Replies
# ---------------------------------------------------------------------------
def parse_response(line: str, command: str = "", obj: Optional[dict] = None) -> Response:
    """Parse one reply line in either format."""
    text = line.strip()
    if text.startswith("{"):
        if obj is None:
            try:
                obj = json.loads(text)
            except json.JSONDecodeError as e:
                raise ProtocolError(f"invalid JSON reply: {e}") from None
        return _parse_json(obj, text, command)
    return _parse_ascii(text, command)


def _parse_json(obj: dict, text: str, command: str) -> Response:
    status = obj.get("status", "")
    cmd = obj.get("command", command)
    if status == "ok":
        data = obj.get("data", {})
        return Response(True, cmd, data if isinstance(data, dict) else {"value": data}, raw=text)
    if status == "error":
        return Response(False, cmd, {}, obj.get("code", "ERROR"),
                        obj.get("message", "unknown error"), raw=text)
    raise ProtocolError(f"unknown status in JSON reply: {status!r}")


def _ints(parts, names):
    try:
        return {n: int(p) for n, p in zip(names, parts)}
    except ValueError:
        return {}


def _parse_ascii(text: str, command: str) -> Response:
    if text.startswith("ERROR"):
        rest = text[5:].strip()
        head, _, tail = rest.partition(" ")
        if _CODE_RE.match(head):                    # protocol 2: ERROR <CODE> <message>
            return Response(False, command, {}, head, tail or head, raw=text)
        return Response(False, command, {}, "ERROR", rest or "unknown error", raw=text)
    if text.startswith("OK"):
        message = text[2:].strip()
        data: Dict[str, Any] = {}
        parts = message.split()
        if len(parts) == 2 and parts[0] == "READY" and parts[1].isdigit():
            data["ready_bytes"] = int(parts[1])
        elif message:
            data["message"] = message
        return Response(True, command, data, raw=text)
    parts = text.split()
    if parts[0] == "PONG":
        return Response(True, command, _ints(parts[1:4], ("seq", "rx_tick", "tx_tick")) |
                        ({"state": parts[4]} if len(parts) > 4 else {}), raw=text)
    if parts[0] == "HEARTBEAT_ACK":
        d = _ints(parts[1:4], ("seq", "mcu_tick", "remaining_ms"))
        if len(parts) > 4:
            try:
                d["boot"] = int(parts[4], 16)
            except ValueError:
                pass
        return Response(True, command, d, raw=text)
    if parts[0] == "HEARTBEAT_STATUS" and len(parts) >= 6:
        return Response(True, command, {
            "enabled": parts[1] == "ENABLED", "timeout_ms": int(parts[2]),
            "last_seq": int(parts[3]), "remaining_ms": int(parts[4]),
            "timed_out": parts[5] == "TIMED_OUT"}, raw=text)
    if parts[0] == "STATUS" and len(parts) >= 8:
        return Response(True, command, {
            "state": parts[1], "tick": int(parts[2]), "queue_depth": int(parts[3]),
            "mode": parts[4], "encoder_status": parts[5],
            "position": int(parts[6]), "speed": int(parts[7])}, raw=text)
    return Response(True, command, {"raw": text}, raw=text)


# ---------------------------------------------------------------------------
# Unsolicited frames
# ---------------------------------------------------------------------------
def parse_event(frame_or_line) -> Event:
    frame = frame_or_line if isinstance(frame_or_line, Frame) else classify(frame_or_line)
    if frame.obj is not None:
        o = frame.obj
        data = o.get("data", {}) if isinstance(o.get("data"), dict) else {}
        status = data.get("status")
        return Event(type=str(o.get("type", "")), seq=o.get("seq"), ts_ms=o.get("ts_ms"),
                     status=int(status) if isinstance(status, (int, float)) else None,
                     detail=data.get("detail"), data=data, raw=frame.text)
    body = frame.text[1:] if frame.text.startswith("!") else frame.text
    typ, _, rest = body.partition(" ")
    data: Dict[str, Any] = {}
    for kv in rest.split():
        if "=" in kv:
            k, v = kv.split("=", 1)
            data[k] = v
    status = None
    if "status" in data:
        try:
            status = int(str(data["status"]), 0)
        except ValueError:
            pass
    return Event(type=typ, status=status, detail=data.get("detail"), data=data, raw=frame.text)


def parse_reset(frame_or_line) -> ResetInfo:
    text = frame_or_line.text if isinstance(frame_or_line, Frame) else frame_or_line.strip()
    kv = dict(p.split("=", 1) for p in text.split()[1:] if "=" in p)
    try:
        return ResetInfo(boot=int(kv.get("boot", "0"), 16), fw=kv.get("fw", "?"),
                         proto=int(kv.get("proto", "0")), build=kv.get("build", "?"), raw=text,
                         rst=kv.get("rst", "?"))
    except ValueError as e:
        raise ProtocolError(f"bad !RESET line: {text}") from e


def parse_telemetry(frame_or_line) -> Dict[str, Any]:
    """Telemetry push -> the DIAG:STAT? `data` dict (JSON) or a reduced dict (ASCII TELEM:)."""
    frame = frame_or_line if isinstance(frame_or_line, Frame) else classify(frame_or_line)
    if frame.obj is not None:
        data = frame.obj.get("data", {})
        return data if isinstance(data, dict) else {}
    kv = dict(p.split("=", 1) for p in frame.text[len("TELEM:"):].split() if "=" in p)
    try:
        return {"motor": {"position": int(kv["pos"]), "speed": int(kv["spd"])},
                "encoder": {"count": int(kv["enc"]), "velocity": int(kv["vel"])}}
    except (KeyError, ValueError) as e:
        raise ProtocolError(f"bad TELEM line: {frame.text}") from e


def parse_joystick(frame_or_line) -> Dict[str, Any]:
    text = frame_or_line.text if isinstance(frame_or_line, Frame) else frame_or_line.strip()
    parts = text.split()
    return {"direction": parts[2] if len(parts) > 2 else "?",
            "pressed": len(parts) > 3 and parts[3] == "pressed"}


# ---------------------------------------------------------------------------
# Typed views of specific replies
# ---------------------------------------------------------------------------
def parse_idn(resp: Response) -> Identity:
    d = resp.data
    if "vendor" in d:                                   # JSON reply
        return Identity(str(d.get("vendor", "")), str(d.get("model", "")),
                        str(d.get("serial", "")), str(d.get("fw", "")), int(d.get("proto", 0)))
    text = d.get("message") or resp.raw
    if text.startswith("OK "):
        text = text[3:]
    parts = [p.strip() for p in str(text).split(",")]
    if len(parts) < 5:
        raise ProtocolError(f"bad *IDN? reply: {resp.raw}")
    try:
        proto = int(parts[4])
    except ValueError:
        raise ProtocolError(f"bad protocol version in *IDN?: {parts[4]}") from None
    return Identity(parts[0], parts[1], parts[2], parts[3], proto)


def parse_heartbeat_ack(resp: Response) -> HeartbeatAck:
    d = resp.data
    boot = d.get("boot")
    return HeartbeatAck(int(d.get("seq", 0)), int(d.get("mcu_tick", 0)),
                        int(d.get("remaining_ms", 0)), int(boot) if boot is not None else None)


def multiline_text(resp: Response) -> str:
    """The joined text of a multi-line reply (JSON `lines` or ASCII lines)."""
    return "\n".join(resp.lines) if resp.lines else resp.raw
