"""
smc_core: the shared host-side core for the stepper motor controller.

Pure Python (3.11+) plus pyserial; nothing here imports Qt. Every frontend (the
PySide6 GUI in smc_gui, the Raspberry Pi CLI in smc_cli, scenario scripts) talks to
the boards only through this package. docs/PROTOCOL.md is the contract.

Public surface (kept to plain data so a Rust core can implement the same interface):
  transport  ITransport, VcpTransport, MemoryTransport
  framing    FrameKind, Frame, classify()
  codec      Response, Event, ResetInfo, Identity, HeartbeatAck, parse_*()
"""

from .version import __version__, full_version

from .framing import Frame, FrameKind, classify
from .codec import (Event, HeartbeatAck, Identity, ProtocolError, ResetInfo, Response,
                    parse_event, parse_heartbeat_ack, parse_idn, parse_reset,
                    parse_response, parse_telemetry)

__all__ = [
    "__version__", "full_version", "Frame", "FrameKind", "classify",
    "Event", "HeartbeatAck", "Identity", "ProtocolError", "ResetInfo", "Response",
    "parse_event", "parse_heartbeat_ack", "parse_idn", "parse_reset",
    "parse_response", "parse_telemetry",
]
