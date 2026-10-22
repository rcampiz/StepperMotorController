"""
Pairing a PS4 / PS5 pad over Bluetooth on Linux (the Raspberry Pi), through BlueZ's bluetoothctl.
No Qt: the GUI runs pair_playstation_pad() on a worker thread and shows its log.

The pad must be in pairing mode (SHARE and PS held until the light bar double-flashes). The
steps are the ones a person would type: power on, an agent that needs no PIN, scan until a Sony
pad shows, pair, trust (so it reconnects by itself when its PS button is pressed), connect.
"""

import queue
import re
import shutil
import subprocess
import sys
import threading
import time
from typing import Callable, Optional

from .gamepad import is_playstation

ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]|\x01|\x02|\r")
# "[NEW] Device AA:BB:CC:DD:EE:FF Wireless Controller", "[CHG] Device AA:..:FF Name: Wireless Controller"
DEVICE = re.compile(r"Device ((?:[0-9A-F]{2}:){5}[0-9A-F]{2}) (?:Name: |Alias: )?(.+)$", re.IGNORECASE)


def available() -> bool:
    """bluetoothctl is here (Linux with BlueZ)."""
    return sys.platform.startswith("linux") and shutil.which("bluetoothctl") is not None


class Bluetoothctl:
    """An interactive bluetoothctl fed commands through a pipe, its output read on a thread."""

    def __init__(self, popen: Callable = subprocess.Popen):
        self.proc = popen(["bluetoothctl"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, bufsize=1)
        self.lines: "queue.Queue[str]" = queue.Queue()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self) -> None:
        for line in self.proc.stdout:
            self.lines.put(ANSI.sub("", line).strip())

    def send(self, command: str) -> None:
        self.proc.stdin.write(command + "\n")
        self.proc.stdin.flush()

    def wait_for(self, match: Callable[[str], bool], timeout: float, log: Callable[[str], None]) -> Optional[str]:
        """The first line (logged as it comes) that matches, or None at the timeout."""
        end = time.monotonic() + timeout
        while True:
            left = end - time.monotonic()
            if left <= 0:
                return None
            try:
                line = self.lines.get(timeout=min(left, 0.2))
            except queue.Empty:
                continue
            if line:
                log(line)
                if match(line):
                    return line

    def close(self) -> None:
        try:
            self.send("quit")
            self.proc.wait(timeout=2)
        except Exception:                     # noqa: BLE001 (gone already)
            self.proc.kill()


def sony_device(line: str) -> Optional[str]:
    """The address in a bluetoothctl line that names a Sony pad, else None."""
    m = DEVICE.search(line)
    return m.group(1).upper() if m and is_playstation(m.group(2)) else None


def pair_playstation_pad(log: Callable[[str], None], scan_s: float = 30.0,
                         ctl_factory: Callable[[], Bluetoothctl] = Bluetoothctl) -> Optional[str]:
    """Find a PS4 / PS5 pad in pairing mode, pair, trust and connect it. Its address, or None
    (the log says why)."""
    ctl = ctl_factory()
    try:
        for command in ("power on", "agent NoInputNoOutput", "default-agent"):
            ctl.send(command)
        log("Looking for the controller for %d s: hold SHARE and PS until the light bar double-flashes." % scan_s)
        ctl.send("scan on")
        line = ctl.wait_for(lambda l: sony_device(l) is not None, scan_s, log)
        if line is None:
            log("No PS4 / PS5 controller was seen. Is it in pairing mode (light bar double-flashing)?")
            return None
        mac = sony_device(line)
        log("Found %s: pairing." % mac)
        ctl.send("scan off")
        ctl.send("pair " + mac)
        ok = ctl.wait_for(lambda l: "Pairing successful" in l or "AlreadyExists" in l or "Failed to pair" in l, 30, log)
        if ok is None or "Failed to pair" in ok:
            log("Pairing failed. Put the controller back in pairing mode and try again.")
            return None
        ctl.send("trust " + mac)
        ctl.wait_for(lambda l: "trust succeeded" in l.lower(), 5, log)
        ctl.send("connect " + mac)
        if ctl.wait_for(lambda l: "Connection successful" in l, 20, log) is None:
            log("Paired and trusted, but not connected yet: press the PS button.")
        else:
            log("Connected. The light bar turns pink once the GUI has the controller.")
        return mac
    finally:
        ctl.close()
