"""
"Pair a controller": how to pair a PS4 / PS5 pad over Bluetooth on this system, with the step
the GUI can take for you: Windows opens its Bluetooth settings; Linux (the Raspberry Pi) finds,
pairs, trusts and connects the pad through bluetoothctl (smc_core.bt_pair) on a worker thread.
"""

import sys
import threading

from PySide6.QtCore import QObject, QUrl, Signal
from PySide6.QtGui import QDesktopServices
from PySide6.QtWidgets import QDialog, QLabel, QPlainTextEdit, QPushButton, QVBoxLayout

from smc_core import bt_pair
from smc_gui import theme

PAIRING_MODE = "Hold SHARE and PS together until the light bar double-flashes (pairing mode)."

WINDOWS_STEPS = [
    PAIRING_MODE,
    "Open Bluetooth settings (the button below): Add device > Bluetooth.",
    "Pick \"Wireless Controller\". Windows pairs it; no PIN.",
    "Back here the controller group names it and the light bar turns pink.",
    "Afterwards: the PS button wakes the controller and it reconnects by itself.",
]
LINUX_STEPS = [
    PAIRING_MODE,
    "Press \"Find and pair\" below: the GUI scans, pairs, trusts and connects it (bluetoothctl).",
    "The controller group names it and the light bar turns pink.",
    "Afterwards: the PS button wakes the controller and it reconnects by itself.",
    "By hand: bluetoothctl, then power on / agent on / scan on / pair MAC / trust MAC / connect MAC.",
]
OTHER_STEPS = [PAIRING_MODE, "Pair \"Wireless Controller\" in the system's Bluetooth settings."]


class _Relay(QObject):
    line = Signal(str)
    done = Signal(str)


class PairingDialog(QDialog):
    def __init__(self, parent=None, platform: str = sys.platform):
        super().__init__(parent)
        self.setWindowTitle("Pair a controller")
        self.setMinimumWidth(460)
        v = QVBoxLayout(self)
        if platform == "win32":
            steps, self.kind = WINDOWS_STEPS, "windows"
        elif platform.startswith("linux"):
            steps, self.kind = LINUX_STEPS, "linux"
        else:
            steps, self.kind = OTHER_STEPS, "other"
        head = QLabel("A PS4 (DualShock 4) or PS5 controller over Bluetooth:")
        head.setStyleSheet("font-weight: bold;")
        v.addWidget(head)
        self.steps = QLabel("\n".join("%d. %s" % (i + 1, s) for i, s in enumerate(steps)))
        self.steps.setWordWrap(True)
        v.addWidget(self.steps)
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumBlockCount(500)
        self.log.setVisible(False)
        self.action = None
        if self.kind == "windows":
            self.action = QPushButton("Open Bluetooth settings")
            self.action.clicked.connect(lambda: QDesktopServices.openUrl(QUrl("ms-settings:bluetooth")))
        elif self.kind == "linux":
            self.action = QPushButton("Find and pair")
            self.action.setEnabled(bt_pair.available())
            if not bt_pair.available():
                self.action.setToolTip("bluetoothctl is not installed (sudo apt install bluez)")
            self.action.clicked.connect(self.find_and_pair)
        if self.action is not None:
            v.addWidget(self.action)
        v.addWidget(self.log)
        self.result = QLabel("")
        self.result.setStyleSheet("color: %s;" % theme.MUTED)
        v.addWidget(self.result)
        close = QPushButton("Close")
        close.clicked.connect(self.accept)
        v.addWidget(close)
        self._relay = _Relay()
        self._relay.line.connect(self.log.appendPlainText)
        self._relay.done.connect(self._finished)

    def find_and_pair(self) -> None:
        self.action.setEnabled(False)
        self.log.setVisible(True)
        self.log.clear()
        self.result.setText("pairing...")

        def job():
            mac = None
            try:
                mac = bt_pair.pair_playstation_pad(self._relay.line.emit)
            except Exception as e:            # noqa: BLE001 (bluetoothctl missing, BlueZ down ...)
                self._relay.line.emit("error: %s" % e)
            self._relay.done.emit(mac or "")

        threading.Thread(target=job, daemon=True).start()

    def _finished(self, mac: str) -> None:
        self.action.setEnabled(True)
        self.result.setText(("paired: %s" % mac) if mac else "not paired (see above)")
