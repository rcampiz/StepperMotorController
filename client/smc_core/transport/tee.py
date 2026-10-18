"""
A transport wrapper that reports every line sent through it. The fleet uses it to show
exactly what the coordinator (the Pi, or this PC standing in for it) sends to each board,
so the Pi's own code can be compared line for line.
"""

import time
from typing import Callable, Optional

from .base import ITransport, TransportInfo


class TeeTransport(ITransport):
    def __init__(self, inner: ITransport, on_tx: Callable[[float, bytes], None]):
        self._inner = inner
        self._on_tx = on_tx

    # --- what is logged ----------------------------------------------------------------
    def send(self, data: bytes) -> int:
        self._on_tx(time.monotonic(), bytes(data))
        return self._inner.send(data)

    def send_line(self, line: str) -> int:
        self._on_tx(time.monotonic(), (line + "\r\n").encode("ascii", "replace"))
        return self._inner.send_line(line)

    def send_priority(self, data: bytes) -> int:
        self._on_tx(time.monotonic(), b"!" + bytes(data))
        return self._inner.send_priority(data)

    # --- everything else passes through ------------------------------------------------
    def open(self) -> bool:
        return self._inner.open()

    def close(self) -> None:
        self._inner.close()

    def is_connected(self) -> bool:
        return self._inner.is_connected()

    def recv(self, max_bytes: int = 1024, timeout_ms: int = 1000) -> bytes:
        return self._inner.recv(max_bytes, timeout_ms)

    def recv_line(self, timeout_ms: int = 1000) -> Optional[str]:
        return self._inner.recv_line(timeout_ms)

    def available(self) -> int:
        return self._inner.available()

    def flush_input(self) -> None:
        self._inner.flush_input()

    def flush_output(self) -> None:
        self._inner.flush_output()

    def out_waiting(self) -> int:
        return self._inner.out_waiting()

    def drain_output(self, timeout_ms: int = 500) -> bool:
        return self._inner.drain_output(timeout_ms)

    def get_info(self) -> TransportInfo:
        return self._inner.get_info()

    def set_baudrate(self, baudrate: int) -> None:
        self._inner.set_baudrate(baudrate)

    def __getattr__(self, name):
        return getattr(self._inner, name)
