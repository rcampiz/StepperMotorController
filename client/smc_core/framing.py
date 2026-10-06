"""
Line framing: classify one received line (docs/PROTOCOL.md sections 2 and 3).

Every line the board sends is exactly one of these frames. The classifier is the
only place that decides; the reader thread in board.py and the legacy MotorClient
both call it. Pure function, no I/O: easy to port and to test from recorded lines.
"""

import json
from dataclasses import dataclass
from enum import Enum
from typing import Any, Dict, Optional


class FrameKind(Enum):
    RESPONSE = "response"     # OK / ERROR / JSON envelope / legacy ASCII replies / plain data lines
    EVENT = "event"           # !TYPE ... or {"kind":"event",...}
    TELEMETRY = "telemetry"   # TELEM: ... or {"kind":"telemetry",...}
    RESET = "reset"           # !RESET boot=... fw=... proto=... build=...  (first line after boot)
    BANNER = "banner"         # the human banner lines that follow !RESET
    JOYSTICK = "joystick"     # EVENT JOY <dir> pressed|released
    ECHO = "echo"             # the command we just sent, echoed back (ASCII mode only)
    EMPTY = "empty"           # blank line


# Human banner after !RESET (command_parser.cpp emitBanner)
BANNER_PREFIXES = ("====", "Stepper Motor Controller", "Type 'SYST:HELP?'", "Type 'HELP'")


@dataclass(frozen=True)
class Frame:
    kind: FrameKind
    text: str                              # the stripped line
    obj: Optional[Dict[str, Any]] = None   # parsed JSON object, when the line is JSON


def classify(line: str, sent_command: Optional[str] = None) -> Frame:
    """Classify one line. `sent_command` is the command in flight, for echo detection."""
    text = line.strip()
    if not text:
        return Frame(FrameKind.EMPTY, text)
    if sent_command and text == sent_command.strip():
        return Frame(FrameKind.ECHO, text)
    if text.startswith("!RESET"):
        return Frame(FrameKind.RESET, text)
    if text.startswith("!"):
        return Frame(FrameKind.EVENT, text)
    if text.startswith("{"):
        try:
            obj = json.loads(text)
        except json.JSONDecodeError:
            return Frame(FrameKind.RESPONSE, text)     # the codec reports the bad JSON
        if isinstance(obj, dict):
            kind = obj.get("kind")
            if kind == "event":
                return Frame(FrameKind.EVENT, text, obj)
            if kind == "telemetry":
                return Frame(FrameKind.TELEMETRY, text, obj)
        return Frame(FrameKind.RESPONSE, text, obj if isinstance(obj, dict) else None)
    if text.startswith("TELEM:"):
        return Frame(FrameKind.TELEMETRY, text)
    if text.startswith("EVENT JOY"):
        return Frame(FrameKind.JOYSTICK, text)
    if text.startswith(BANNER_PREFIXES):
        return Frame(FrameKind.BANNER, text)
    return Frame(FrameKind.RESPONSE, text)


def is_unsolicited(frame: Frame) -> bool:
    """Frames that can arrive at any time, between or inside replies."""
    return frame.kind in (FrameKind.EVENT, FrameKind.TELEMETRY, FrameKind.RESET,
                          FrameKind.BANNER, FrameKind.JOYSTICK, FrameKind.EMPTY,
                          FrameKind.ECHO)
