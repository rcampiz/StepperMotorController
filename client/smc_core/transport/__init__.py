"""Byte transports: pyserial (VCP), J-Link RTT, and an in-memory pair for tests."""

from .base import ITransport, TransportError, TransportInfo, ConnectionError, TimeoutError
from .memory import MemoryTransport

__all__ = ["ITransport", "TransportError", "TransportInfo", "ConnectionError",
           "TimeoutError", "MemoryTransport", "VcpTransport", "RttTransport", "TcpTransport",
           "open_transport"]


def open_transport(address: str, baudrate: int = 115200) -> ITransport:
    """One address syntax for every frontend:
    COM6 / /dev/ttyACM0 -> serial (VCP); tcp://HOST:PORT -> TCP (virtual Nucleo, bridges)."""
    if address.startswith("tcp://"):
        from .tcp import TcpTransport
        return TcpTransport(address)
    from .serial import VcpTransport
    return VcpTransport(port=address or None, baudrate=baudrate)


def __getattr__(name):
    # pyserial / pylink are imported only when the transport is actually used, so the
    # core (and its tests) import cleanly on a machine without them.
    if name == "TcpTransport":
        from .tcp import TcpTransport
        return TcpTransport
    if name == "VcpTransport":
        from .serial import VcpTransport
        return VcpTransport
    if name in ("RttTransport", "PYLINK_AVAILABLE"):
        from . import rtt
        return getattr(rtt, name)
    raise AttributeError(name)
