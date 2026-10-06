"""Byte transports: pyserial (VCP), J-Link RTT, and an in-memory pair for tests."""

from .base import ITransport, TransportError, TransportInfo, ConnectionError, TimeoutError
from .memory import MemoryTransport

__all__ = ["ITransport", "TransportError", "TransportInfo", "ConnectionError",
           "TimeoutError", "MemoryTransport", "VcpTransport", "RttTransport"]


def __getattr__(name):
    # pyserial / pylink are imported only when the transport is actually used, so the
    # core (and its tests) import cleanly on a machine without them.
    if name == "VcpTransport":
        from .serial import VcpTransport
        return VcpTransport
    if name in ("RttTransport", "PYLINK_AVAILABLE"):
        from . import rtt
        return getattr(rtt, name)
    raise AttributeError(name)
