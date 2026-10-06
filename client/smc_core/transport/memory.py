"""
In-memory transport: the host side of a byte pipe whose other end is a Python object.

Used by the core's tests and by the Python fake board. Every write is delivered
synchronously to `peer.on_host_bytes(data)`; the peer answers with `feed_line()` /
`feed_bytes()` from any thread. `send_priority()` records a purge so tests can
assert the ESTOP ordering.
"""

import threading
import time
from typing import Callable, List, Optional, Tuple

from .base import ITransport, TransportError, TransportInfo


class MemoryTransport(ITransport):
    def __init__(self, peer=None, name: str = "mem0"):
        self._peer = peer
        self._name = name
        self._open = False
        self._rx = bytearray()
        self._cond = threading.Condition()
        self._write_lock = threading.RLock()
        self.log: List[Tuple[str, bytes]] = []      # ("send" | "purge", payload)
        self.fail_writes = False                      # simulate an unplugged port

    def attach(self, peer) -> None:
        self._peer = peer

    # --- peer side ----------------------------------------------------------
    def feed_bytes(self, data: bytes) -> None:
        with self._cond:
            self._rx += data
            self._cond.notify_all()

    def feed_line(self, line: str) -> None:
        self.feed_bytes(line.encode("utf-8") + b"\r\n")

    # --- ITransport ---------------------------------------------------------
    def open(self) -> bool:
        self._open = True
        return True

    def close(self) -> None:
        self._open = False
        with self._cond:
            self._cond.notify_all()

    def is_connected(self) -> bool:
        return self._open

    def _deliver(self, data: bytes) -> None:
        if self._peer is not None:
            self._peer.on_host_bytes(bytes(data))

    def send(self, data: bytes) -> int:
        if not self._open or self.fail_writes:
            raise TransportError("Not connected")
        with self._write_lock:
            self.log.append(("send", bytes(data)))
            self._deliver(data)
            return len(data)

    def send_line(self, line: str) -> int:
        return self.send((line + "\r\n").encode("utf-8"))

    def send_priority(self, data: bytes) -> int:
        if not self._open or self.fail_writes:
            raise TransportError("Not connected")
        with self._write_lock:
            self.log.append(("purge", b""))
            self.log.append(("send", bytes(data)))
            self._deliver(data)
            return len(data)

    def out_waiting(self) -> int:
        return 0

    def drain_output(self, timeout_ms: int = 500) -> bool:
        return True

    def recv(self, max_bytes: int = 1024, timeout_ms: int = 1000) -> bytes:
        deadline = time.monotonic() + timeout_ms / 1000.0
        with self._cond:
            while not self._rx and self._open:
                left = deadline - time.monotonic()
                if left <= 0:
                    return b""
                self._cond.wait(left)
            out = bytes(self._rx[:max_bytes])
            del self._rx[:max_bytes]
            return out

    def recv_line(self, timeout_ms: int = 1000) -> Optional[str]:
        if not self._open:
            raise TransportError("Not connected")
        deadline = time.monotonic() + timeout_ms / 1000.0
        with self._cond:
            while b"\n" not in self._rx:
                left = deadline - time.monotonic()
                if left <= 0 or not self._open:
                    return None
                self._cond.wait(left)
            i = self._rx.index(b"\n")
            raw = bytes(self._rx[:i])
            del self._rx[:i + 1]
        if raw.endswith(b"\r"):
            raw = raw[:-1]
        return raw.decode("utf-8", errors="replace")

    def available(self) -> int:
        with self._cond:
            return len(self._rx)

    def flush_input(self) -> None:
        with self._cond:
            self._rx.clear()

    def flush_output(self) -> None:
        pass

    def get_info(self) -> TransportInfo:
        return TransportInfo("MEMORY", self._name, self._open, {})

    def sent_lines(self) -> List[str]:
        """Every complete text line the host wrote, in order (priority writes included)."""
        text = b"".join(p for k, p in self.log if k == "send")
        text = text.replace(b"\x18", b"")          # the out-of-band ESTOP byte is not a line
        return [l.rstrip(b"\r").decode("utf-8", "replace")
                for l in text.split(b"\n") if l.strip(b"\r")]
