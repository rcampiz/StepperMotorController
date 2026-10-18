"""
One-line board status strip: state, position, speed, Hi-Z, faults, watchdog, mode, link.

Fed from the telemetry push (the DIAG:STAT? data object). Every value sits in a chip with a
fixed minimum width and no wrapping, so the strip never reflows when a value changes length.
"""

from typing import Dict, Optional

from PySide6.QtCore import Qt
from PySide6.QtGui import QFont
from PySide6.QtWidgets import QFrame, QHBoxLayout, QLabel, QVBoxLayout, QWidget

STATE_COLOURS = {
    "IDLE": ("#1f3b1f", "#7dff7d"),
    "ARMED": ("#3b3b1f", "#ffe97d"),
    "RUNNING": ("#1f2e3b", "#7dc8ff"),
    "FAULT": ("#3b1f1f", "#ff7d7d"),
    "ESTOP": ("#4d1a1a", "#ff6666"),
}
FAULT_FLAGS = (("ocd", "OCD"), ("thermal_sd", "TH_SD"), ("thermal_warn", "TH_WARN"), ("uvlo", "UVLO"),
               ("stall_a", "STALL_A"), ("stall_b", "STALL_B"), ("cmd_err", "CMD_ERR"))


class _Chip(QFrame):
    def __init__(self, title: str, min_width: int, parent=None):
        super().__init__(parent)
        self.setFrameShape(QFrame.StyledPanel)
        self.setMinimumWidth(min_width)
        self.setStyleSheet("QFrame { background: #262626; border: 1px solid #3a3a3a; border-radius: 6px; }")
        v = QVBoxLayout(self)
        v.setContentsMargins(10, 4, 10, 4)
        v.setSpacing(0)
        t = QLabel(title)
        t.setStyleSheet("color: #888; font-size: 9px; border: none; background: transparent;")
        v.addWidget(t)
        self.value = QLabel("--")
        self.value.setFont(QFont("Consolas", 11, QFont.Bold))
        self.value.setStyleSheet("border: none; background: transparent;")
        self.value.setTextFormat(Qt.PlainText)
        self.value.setWordWrap(False)
        v.addWidget(self.value)

    def set(self, text: str, colour: Optional[str] = None, background: Optional[str] = None) -> None:
        self.value.setText(text)
        style = "border: none; background: %s;" % (background or "transparent")
        if colour:
            style += " color: %s;" % colour
        self.value.setStyleSheet(style + " padding: 0 4px; border-radius: 4px;")


class StatusStrip(QWidget):
    """The strip. Call update_status(data) with each telemetry object; set_geometry() with
    the board's full steps and encoder counts per revolution for the RPM and revolution
    readouts; set_link() on connect and disconnect."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._full_steps = 200
        self._cpr = 4000
        row = QHBoxLayout(self)
        row.setContentsMargins(0, 0, 0, 0)
        row.setSpacing(6)
        self._state = _Chip("STATE", 110)
        self._pos = _Chip("POSITION", 170)
        self._speed = _Chip("SPEED", 170)
        self._hiz = _Chip("OUTPUT", 90)
        self._faults = _Chip("FAULTS", 150)
        self._wd = _Chip("WATCHDOG", 110)
        self._mode = _Chip("MODE", 150)
        self._link = _Chip("LINK", 120)
        for c in (self._state, self._pos, self._speed, self._hiz, self._faults, self._wd, self._mode, self._link):
            row.addWidget(c)
        row.addStretch()
        self.set_link(None)
        self.clear()

    def set_geometry(self, full_steps: Optional[int], cpr: Optional[int]) -> None:
        if full_steps:
            self._full_steps = int(full_steps)
        if cpr:
            self._cpr = int(cpr)

    def set_link(self, label: Optional[str]) -> None:
        if label:
            self._link.set(label, "#7dff7d")
        else:
            self._link.set("disconnected", "#888")

    def clear(self) -> None:
        self._state.set("--", "#888")
        self._pos.set("--")
        self._speed.set("--")
        self._hiz.set("--")
        self._faults.set("--")
        self._wd.set("--")
        self._mode.set("--")

    def update_status(self, data: Dict) -> None:
        state = str(data.get("state", "--"))
        bg, fg = STATE_COLOURS.get(state, ("#333", "#ccc"))
        self._state.set(state, fg, bg)
        motor = data.get("motor", {}) if isinstance(data.get("motor"), dict) else {}
        enc = data.get("encoder", {}) if isinstance(data.get("encoder"), dict) else {}
        pos = motor.get("position")
        count = enc.get("count")
        if count is not None:
            self._pos.set("%d cnt  %.2f rev" % (int(count), int(count) / float(self._cpr)))
        elif pos is not None:
            self._pos.set("%d µsteps" % int(pos))
        speed = motor.get("speed")
        vel = enc.get("velocity")
        if vel is not None:
            rpm = float(vel) / self._cpr * 60.0
            self._speed.set("%d cnt/s  %.1f rpm" % (int(vel), rpm))
        elif speed is not None:
            self._speed.set("%d steps/s" % int(speed))
        hiz = motor.get("hi_z")
        if hiz is not None:
            self._hiz.set("Hi-Z" if hiz else "ENERGISED", "#888" if hiz else "#ffcc00")
        flags = [name for key, name in FAULT_FLAGS if motor.get(key)]
        self._faults.set(" ".join(flags) if flags else "none", "#ff7d7d" if flags else "#7dff7d")
        hb = data.get("heartbeat", {}) if isinstance(data.get("heartbeat"), dict) else {}
        if hb:
            if hb.get("enabled"):
                self._wd.set("%d ms left" % int(hb.get("remaining_ms", 0)),
                             "#ff7d7d" if hb.get("timed_out") else None)
            else:
                self._wd.set("off", "#ffcc00")
        mode = data.get("mode")
        if mode is not None:
            self._mode.set(str(mode))
