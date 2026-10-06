"""
TCP transport: the host side of a virtual Nucleo (sim/vnucleo), or any board reached
through a serial-to-TCP bridge. Address form: tcp://HOST:PORT.

Differences from the serial transport: there is no driver output queue to purge, so
send_priority() is an ordinary write under the same lock, and out_waiting() is 0.
"""

import socket
import threading
import time
from typing import Optional, Tuple

from .base import ConnectionError, ITransport, TransportError, TransportInfo


def parse_tcp_url(url: str) -> Tuple[str, int]:
    if not url.startswith("tcp://"):
        raise ValueError(f"not a tcp:// address: {url}")
    host, _, port = url[len("tcp://"):].rpartition(":")
    if not host or not port.isdigit():
        raise ValueError(f"expected tcp://HOST:PORT, got {url}")
    return host, int(port)


class TcpTransport(ITransport):
    def __init__(self, url: str, connect_timeout_s: float = 3.0):
        self._url = url
        self._host, self._port = parse_tcp_url(url)
        self._timeout = connect_timeout_s
        self._sock: Optional[socket.socket] = None
        self._buf = bytearray()
        self._write_lock = threading.RLock()

    def open(self) -> bool:
        if self._sock is not None:
            return True
        try:
            s = socket.create_connection((self._host, self._port), timeout=self._timeout)
        except OSError as e:
            raise ConnectionError(f"cannot connect to {self._url}: {e}") from None
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._sock = s
        self._buf.clear()
        return True

    def close(self) -> None:
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass
        self._sock = None
        self._buf.clear()

    def is_connected(self) -> bool:
        return self._sock is not None

    def _need(self) -> socket.socket:
        if self._sock is None:
            raise TransportError("Not connected")
        return self._sock

    def send(self, data: bytes) -> int:
        s = self._need()
        try:
            with self._write_lock:
                s.sendall(data)
            return len(data)
        except OSError as e:
            raise TransportError(f"send failed: {e}") from None

    def send_line(self, line: str) -> int:
        return self.send((line + "\r\n").encode("utf-8"))

    def send_priority(self, data: bytes) -> int:
        return self.send(data)                        # nothing queued below us to purge

    def out_waiting(self) -> int:
        return 0

    def drain_output(self, timeout_ms: int = 500) -> bool:
        return True

    def _fill(self, timeout_s: float) -> bool:
        s = self._need()
        s.settimeout(max(0.001, timeout_s))
        try:
            chunk = s.recv(4096)
        except socket.timeout:
            return False
        except OSError as e:
            raise TransportError(f"receive failed: {e}") from None
        if not chunk:
            raise TransportError("connection closed by the board")
        self._buf += chunk
        return True

    def recv(self, max_bytes: int = 1024, timeout_ms: int = 1000) -> bytes:
        if not self._buf:
            self._fill(timeout_ms / 1000.0)
        out = bytes(self._buf[:max_bytes])
        del self._buf[:max_bytes]
        return out

    def recv_line(self, timeout_ms: int = 1000) -> Optional[str]:
        deadline = time.monotonic() + timeout_ms / 1000.0
        while b"\n" not in self._buf:
            left = deadline - time.monotonic()
            if left <= 0:
                return None
            self._fill(left)
        i = self._buf.index(b"\n")
        raw = bytes(self._buf[:i])
        del self._buf[:i + 1]
        return raw.rstrip(b"\r").decode("utf-8", errors="replace")

    def available(self) -> int:
        return len(self._buf)

    def flush_input(self) -> None:
        self._buf.clear()
        if self._sock is None:
            return
        self._sock.settimeout(0.0)
        try:
            while self._sock.recv(4096):
                pass
        except (BlockingIOError, socket.timeout, OSError):
            pass

    def flush_output(self) -> None:
        pass

    def get_info(self) -> TransportInfo:
        return TransportInfo("TCP", self._url, self.is_connected(), {})
