"""Test helper: start virtual Nucleo boards (sim/vnucleo) on free ports; skip if not built."""

import os
import socket
import subprocess
import time
from contextlib import contextmanager

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXE = os.path.join(ROOT, "build-vnucleo", "vnucleo.exe" if os.name == "nt" else "vnucleo")


def available() -> bool:
    return os.path.exists(EXE)


def require():
    if not available():
        pytest.skip("virtual Nucleo not built (cmake --preset vnucleo in sim/)")


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _wait_listening(port: int, timeout: float = 5.0) -> None:
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError(f"virtual Nucleo did not start on port {port}")


@contextmanager
def boards(count: int = 1, serial_base: int = 1, log_dir: str = None):
    """Yield a list of (url, Popen) for `count` fresh boards; stops them afterwards.
    With log_dir, board n writes its motion log to log_dir/VN00000n.log."""
    require()
    procs = []
    try:
        for i in range(count):
            port = free_port()
            serial = "VN%06d" % (serial_base + i)
            args = [EXE, "--port", str(port), "--serial", serial]
            if log_dir:
                args += ["--motion-log", os.path.join(log_dir, serial + ".log")]
            p = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            procs.append((f"tcp://127.0.0.1:{port}", p))
            _wait_listening(port)
        # The probe connection above is a client too; let the board see it close.
        time.sleep(0.1)
        yield procs
    finally:
        for _, p in procs:
            if p.poll() is None:
                p.kill()
                p.wait(5)


def motion_log(log_dir: str, serial: str):
    """[(steady_ns, event, value)] written by one board's fake powerSTEP01."""
    path = os.path.join(log_dir, serial + ".log")
    if not os.path.exists(path):
        return []
    with open(path, encoding="utf-8") as fh:
        rows = [line.split() for line in fh if line.strip()]
    return [(int(t), ev, int(v)) for t, ev, v in rows]
