"""
Controller group: an Xbox pad (XInput), a PS4 / PS5 pad over USB or Bluetooth (SDL, also on the
Raspberry Pi 5) or the on-screen stick, polled from a timer,
mapped through smc_core.drive_map, and shown as two live stick pads with the button legend.

Two modes. "motor": the left stick's vertical axis drives the connected board (MOT:RUN in
RPM, resonance bands stepped over), B stops, Y enables, X clears the fault, START E-STOPs.
"base": the left stick moves the base (vx/vy), the right stick's x turns it (right half
clockwise, left half counter-clockwise), the triggers brake a side with their pull (LT the left
wheels, RT the right ones; full pull stops and holds them). The on-screen pads stand in for the sticks. The Fleet page owns the driver.
"""

import math
import time
from typing import Optional

from PySide6.QtCore import QPointF, QRectF, Qt, QTimer, Signal
from PySide6.QtGui import QBrush, QColor, QMouseEvent, QPainter, QPen
from PySide6.QtWidgets import (
    QCheckBox, QDoubleSpinBox, QGridLayout, QGroupBox, QHBoxLayout, QLabel, QVBoxLayout, QWidget,
)

from smc_core.drive_map import StickGate, StickMap
from smc_core.gamepad import GamepadState, IGamepad, SoftwareGamepad, default_gamepad
from smc_gui import theme


# On a PlayStation pad the buttons wear Sony's names (SDL reads them in the Xbox layout)
PS_NAMES = {"A": "✕", "B": "○", "X": "□", "Y": "△", "START": "OPTIONS", "BACK": "SHARE",
            "GUIDE": "PS", "LB": "L1", "RB": "R1", "LTHUMB": "L3", "RTHUMB": "R3"}

_shared_pad: Optional[IGamepad] = None


def shared_gamepad() -> IGamepad:
    """One pad for every controller group (the Drive tab's and the Fleet page's): SDL opens
    the device once."""
    global _shared_pad
    if _shared_pad is None:
        _shared_pad = default_gamepad()
    return _shared_pad


TRIGGER_DEAD_ZONE = 0.12               # = drive_map.BaseMap's trigger gates (start): a brake past it


class StickPad(QWidget):
    """One thumbstick as a square pad; draggable, so it doubles as an on-screen stick."""
    moved = Signal(float, float)        # x, y in -1..1 (up positive)

    def __init__(self, label: str, parent=None):
        super().__init__(parent)
        self._label = label
        self._x = 0.0
        self._y = 0.0
        self._live = False
        self.dead_zone = 0.0            # drawn as a ring: inside it the stick is ignored
        self.active = True              # False: the stick is inside its dead zone (dot drawn dim)
        self.setFixedSize(96, 96)

    def set(self, x: float, y: float, live: bool = True) -> None:
        self._x, self._y, self._live = x, y, live
        self.update()

    def _from_mouse(self, e: QMouseEvent) -> None:
        w = self.width() / 2.0
        x = max(-1.0, min(1.0, (e.position().x() - w) / (w - 6)))
        y = max(-1.0, min(1.0, -(e.position().y() - w) / (w - 6)))
        self.moved.emit(x, y)

    def mousePressEvent(self, e):  # noqa: N802
        self._from_mouse(e)

    def mouseMoveEvent(self, e):  # noqa: N802
        self._from_mouse(e)

    def mouseReleaseEvent(self, e):  # noqa: N802
        self.moved.emit(0.0, 0.0)

    def paintEvent(self, event):  # noqa: N802
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        w = self.width()
        p.setPen(QPen(QColor(theme.GRID_MAJ), 1))
        p.setBrush(QBrush(QColor(theme.BASE)))
        p.drawRect(1, 1, w - 2, w - 2)
        p.setPen(QPen(QColor(theme.GRID), 1))
        p.drawLine(w // 2, 2, w // 2, w - 2)
        p.drawLine(2, w // 2, w - 2, w // 2)
        span = w / 2.0 - 6
        if self.dead_zone > 0:
            # The dead zone: a stick resting in here (a pad rarely rests dead centre) moves nothing
            p.setPen(QPen(QColor(theme.GRID_MAJ), 1, Qt.DashLine))
            p.setBrush(Qt.NoBrush)
            p.drawEllipse(QPointF(w / 2.0, w / 2.0), self.dead_zone * span, self.dead_zone * span)
        cx = w / 2.0 + self._x * span
        cy = w / 2.0 - self._y * span
        lit = self._live and self.active
        p.setPen(QPen(QColor(theme.MAGENTA if lit else theme.DIM), 2))
        p.setBrush(QBrush(QColor(theme.MAGENTA if lit else theme.DIM)))
        p.drawEllipse(QPointF(cx, cy), 5, 5)
        p.setPen(QPen(QColor(theme.MUTED)))
        p.drawText(QRectF(0, w - 14, w, 12), Qt.AlignCenter, self._label)
        p.end()


class ControllerPanel(QGroupBox):
    """A controller group box; `state_changed` carries each polled state to its driver."""
    state_changed = Signal(object)      # GamepadState or None, every poll
    drive_toggled = Signal(bool)
    max_rpm_changed = Signal(float)     # base mode: the motors' speed at full stick / trigger

    def __init__(self, mode: str = "motor", parent=None):
        super().__init__("Controller" if mode == "motor" else "Controller (base)", parent)
        self.mode = mode
        self._pad: IGamepad = shared_gamepad()
        self._legend_ps: Optional[bool] = None         # the legend's button names as drawn
        self._soft = SoftwareGamepad()
        self._use_soft = False
        self.stick = StickMap()
        self._setup_ui()
        self._timer = QTimer(self)
        self._timer.setInterval(20)                 # 50 Hz: the pad is read every 20 ms; only changes are sent
        self._timer.timeout.connect(self._poll)
        self._timer.start()
        self._last_packet = -1
        self._last_seen = 0.0

    def _setup_ui(self):
        v = QVBoxLayout(self)
        top = QHBoxLayout()
        self._left = StickPad("L: speed" if self.mode == "motor" else "L: move")
        self._right = StickPad("R: --" if self.mode == "motor" else "R: turn")
        self._left.moved.connect(lambda x, y: self._on_screen(lx=x, ly=y))
        self._right.moved.connect(lambda x, y: self._on_screen(rx=x, ry=y))
        # The dead zones the drivers apply, drawn on the pads (a pad at rest sits a little off centre)
        if self.mode == "base":
            self._left.dead_zone = StickGate().enter
            self._right.dead_zone = StickGate().enter
        else:
            self._left.dead_zone = self.stick.dead_zone
        top.addWidget(self._left)
        top.addWidget(self._right)
        settings = QGridLayout()
        self._status = QLabel("no controller")
        self._status.setStyleSheet("color: %s;" % theme.MUTED)
        settings.addWidget(self._status, 0, 0, 1, 2)
        self._drive_cb = QCheckBox("Drive with controller")
        self._drive_cb.toggled.connect(self.drive_toggled.emit)
        settings.addWidget(self._drive_cb, 1, 0, 1, 2)
        if self.mode == "base":
            settings.addWidget(QLabel("Max:"), 2, 0)
            self._max_rpm = QDoubleSpinBox(); self._max_rpm.setRange(5, 600); self._max_rpm.setValue(400)
            self._max_rpm.setSuffix(" RPM"); self._max_rpm.setSingleStep(10)
            self._max_rpm.setToolTip("Motor speed at full stick or full trigger; no motor goes faster "
                                     "(a diagonal or a move with a turn is scaled down)")
            self._max_rpm.valueChanged.connect(lambda v: self.max_rpm_changed.emit(float(v)))
            settings.addWidget(self._max_rpm, 2, 1)
        if self.mode == "motor":
            settings.addWidget(QLabel("Max:"), 2, 0)
            self._max_rpm = QDoubleSpinBox(); self._max_rpm.setRange(1, 1500); self._max_rpm.setValue(250); self._max_rpm.setSuffix(" RPM")
            self._max_rpm.valueChanged.connect(lambda v: setattr(self.stick, "max_rpm", float(v)))
            settings.addWidget(self._max_rpm, 2, 1)
            self._bands_cb = QCheckBox("Cross resonance bands fast")
            self._bands_cb.setToolTip("While the stick sweeps the speed through a resonance band, jump to its "
                                      "far edge (the chip crosses it in ~15 ms). The bands come from the "
                                      "board's resonance avoider, else from the motor profile")
            self._bands_cb.setChecked(True)
            self._bands_cb.toggled.connect(lambda on: setattr(self.stick, "avoid_bands", bool(on)))
            settings.addWidget(self._bands_cb, 3, 0, 1, 2)
        self._legend = QLabel(self.legend_text(False))
        self._legend.setWordWrap(True)
        self._legend.setStyleSheet("color: %s; font-size: 10px;" % theme.MUTED)
        settings.addWidget(self._legend, 4, 0, 1, 2)
        top.addLayout(settings, 1)
        v.addLayout(top)

    def legend_text(self, playstation: bool) -> str:
        """What each control does, in the pad's own names."""
        n = (lambda b: PS_NAMES.get(b, b)) if playstation else (lambda b: b)
        trig = "L2/R2" if playstation else "LT/RT"
        return (("L stick move   R stick turn   %s brake left/right   " % trig if self.mode == "base" else "") +
                "%s stop   %s enable   %s %s   %s e-stop (hold 3 s: clear)" % (
                    n("B"), n("Y"), n("X"), "re-arm" if self.mode == "base" else "clear fault", n("START")))

    # --- input sources ---------------------------------------------------------------
    def use_gamepad(self, pad: IGamepad) -> None:
        """Tests: inject a pad."""
        self._pad = pad
        self._use_soft = False

    def _on_screen(self, **axes) -> None:
        self._use_soft = True
        self._soft.set(**axes)
        if all(abs(v) < 1e-6 for v in axes.values()):
            # released: back to the real pad on the next poll if it is there
            self._use_soft = False

    @property
    def max_rpm(self) -> float:
        return float(self._max_rpm.value())

    def set_max_rpm(self, rpm: float) -> None:
        self._max_rpm.setValue(rpm)

    @property
    def driving(self) -> bool:
        return self._drive_cb.isChecked()

    def set_driving(self, on: bool) -> None:
        self._drive_cb.setChecked(on)

    def _poll(self) -> None:
        state: Optional[GamepadState] = self._pad.poll()
        if state is None and self._use_soft:
            state = self._soft.poll()
        elif self._use_soft:
            state = self._soft.poll()
        now = time.monotonic()
        if state is not None:
            if self.mode == "base":
                self._left.active = math.hypot(state.lx, state.ly) > self._left.dead_zone
                self._right.active = abs(state.rx) > self._right.dead_zone
            else:
                self._left.active = abs(state.ly) > self._left.dead_zone
            self._left.set(state.lx, state.ly, True)
            self._right.set(state.rx, state.ry, True)
            src = "on-screen stick" if self._use_soft else getattr(self._pad, "name", "pad")
            ps = bool(getattr(self._pad, "playstation", False)) and not self._use_soft
            if ps != self._legend_ps:
                self._legend.setText(self.legend_text(ps))
                self._legend_ps = ps
            words = sorted(PS_NAMES.get(b, b) if ps else b for b in state.buttons)
            if self.mode == "base":                  # the side brakes, with their pull
                words += ["brake L %d%%" % round(state.lt * 100)] if state.lt > TRIGGER_DEAD_ZONE else []
                words += ["brake R %d%%" % round(state.rt * 100)] if state.rt > TRIGGER_DEAD_ZONE else []
            self._status.setText("%s: %s" % (src, " ".join(words) or "ok"))
            self._last_seen = now
        else:
            self._left.set(0, 0, False)
            self._right.set(0, 0, False)
            why = getattr(self._pad, "error", "")
            self._status.setText("no controller: an Xbox pad, or a PS4 pad over USB or Bluetooth, or drag a stick" +
                                 (" (%s)" % why if why else ""))
        self.state_changed.emit(state if self.driving else None)
