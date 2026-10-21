"""
Resonance avoider panel (Drive tab): the firmware avoider's switch and live state, the
measured ripple, the one-number current scale and the quick recalibration.

The avoider (CTRL:RES:*) moves the motor's resonance off the speed in use by changing the run
current; the ripple (telemetry encoder.rip, counts rms) is the resonance as a number, so turning
the avoider off and on at a resonant speed shows what it does. The current scale (DRV:CFG:SCALE)
multiplies every current level when applied: one number to raise when the robot needs more
torque, the avoider's bands follow it. Recalibrate re-measures the main resonance at the current
in use (resonance_recal, ~30 s, the motor runs) and updates the model's constant.
"""

from typing import Optional

from PySide6.QtCore import Signal, Slot
from PySide6.QtWidgets import (
    QCheckBox, QGridLayout, QGroupBox, QHBoxLayout, QLabel, QPushButton, QSpinBox, QVBoxLayout,
    QWidget,
)

from smc_gui import theme

STATE_COLOURS = {"boost": "GREEN", "cut": "ORANGE", "unavoidable": "YELLOW", "nominal": "TEXT", "off": "MUTED"}
STATE_TEXT = {"boost": "raised", "cut": "cut", "unavoidable": "in a band, left alone",
              "nominal": "normal", "off": "off"}


def _colour(name: str) -> str:
    return getattr(theme, name, theme.TEXT)


class ResonancePanel(QWidget):
    enable_requested = Signal(bool)          # CTRL:RES:CFG 0/1 (not saved: an A/B switch)
    scale_requested = Signal(int)            # DRV:CFG:SCALE <pct> + APPLY
    recal_requested = Signal()               # resonance_recal scenario

    def __init__(self, parent=None):
        super().__init__(parent)
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        box = QGroupBox("Resonance avoider")
        g = QGridLayout(box)

        self._on = QCheckBox("On")
        self._on.setToolTip("The firmware moves the run current so the motor's resonance is never at the "
                            "speed in use. Off/on here is not saved: an A/B switch (CTRL:RES:SAVE keeps a state)")
        self._on.clicked.connect(lambda checked: self.enable_requested.emit(bool(checked)))
        g.addWidget(self._on, 0, 0)
        self._state = QLabel("--")
        self._state.setToolTip("normal: the configured run current; raised / cut: moved to keep the "
                               "resonance off this speed; in a band, left alone: no allowed current clears it")
        g.addWidget(self._state, 0, 1)
        self._run = QLabel("run -- A")
        self._run.setToolTip("The run current in force now (and the configured one)")
        g.addWidget(self._run, 0, 2)
        self._ripple = QLabel("ripple --")
        self._ripple.setToolTip("The position's wobble about its trend, rms, measured on the board at the "
                                "encoder's sample rate: the resonance as a number (1 count = 0.09 deg). "
                                "Quiet running is ~1; the bench motor's resonance without the avoider ~5")
        self._ripple.setStyleSheet("font-family: Consolas; font-size: 14px; font-weight: bold;")
        g.addWidget(self._ripple, 0, 3)

        self._bands = QLabel("bands --")
        self._bands.setStyleSheet("color: %s;" % theme.MUTED)
        g.addWidget(self._bands, 1, 0, 1, 3)
        self._counts = QLabel("")
        self._counts.setStyleSheet("color: %s;" % theme.MUTED)
        g.addWidget(self._counts, 1, 3)

        scale_row = QHBoxLayout()
        scale_row.addWidget(QLabel("Current scale"))
        self._scale = QSpinBox()
        self._scale.setRange(25, 200)
        self._scale.setSingleStep(5)
        self._scale.setSuffix(" %")
        self._scale.setValue(100)
        self._scale.setToolTip("One number for every current level: hold, run, accel and decel go in force "
                               "x this. Raise it when the robot stalls under load; the avoider's bands follow")
        scale_row.addWidget(self._scale)
        self._scale_apply = QPushButton("Apply")
        self._scale_apply.setToolTip("Set the scale and apply the settings: in force at once, and saved")
        self._scale_apply.clicked.connect(lambda: self.scale_requested.emit(int(self._scale.value())))
        scale_row.addWidget(self._scale_apply)
        self._applied = QLabel("")
        self._applied.setStyleSheet("color: %s;" % theme.MUTED)
        scale_row.addWidget(self._applied, 1)
        g.addLayout(scale_row, 2, 0, 1, 4)

        recal_row = QHBoxLayout()
        self._recal = QPushButton("Recalibrate (~30 s, the motor runs)")
        self._recal.setToolTip("Re-measure the main resonance at the current in use (after a load or a "
                               "scale change) and update the model; the avoider is off meanwhile")
        self._recal.clicked.connect(self._on_recal)
        recal_row.addWidget(self._recal)
        self._recal_msg = QLabel("")
        self._recal_msg.setWordWrap(True)
        recal_row.addWidget(self._recal_msg, 1)
        g.addLayout(recal_row, 3, 0, 1, 4)
        root.addWidget(box)
        self.set_connected(False)

    # --- inputs ------------------------------------------------------------------------------
    def set_connected(self, connected: bool) -> None:
        for w in (self._on, self._scale, self._scale_apply, self._recal):
            w.setEnabled(connected)
        if not connected:
            self._state.setText("--")
            self._ripple.setText("ripple --")

    def update_status(self, st: dict) -> None:
        """A CTRL:RES? reply."""
        if not st:
            return
        has_model = float(st.get("f1a_hz") or 0) > 0
        self._on.setEnabled(has_model)
        self._on.setChecked(bool(st.get("enabled")))
        state = str(st.get("state", "off"))
        self._state.setText(STATE_TEXT.get(state, state))
        self._state.setStyleSheet("font-weight: bold; color: %s;" % _colour(STATE_COLOURS.get(state, "TEXT")))
        self._run.setText("run %.3f A (cfg %.3f)" % (float(st.get("run_a", 0)), float(st.get("run_cfg_a", 0))))
        if has_model:
            self._bands.setText("resonant at %.0f, %.0f, %.0f RPM (band +-%d %%)   model %.1f Hz x I^%.2f" % (
                float(st.get("rpm_k4", 0)), float(st.get("rpm_k2", 0)), float(st.get("rpm_k1", 0)),
                int(st.get("band_pct", 8)), float(st.get("f1a_hz", 0)), float(st.get("exponent", 0))))
        else:
            self._bands.setText("no model: run the resonance_profile scenario (Diagnostics > Tests) with apply")
        self._counts.setText("%s changes, %s guard trips" % (st.get("changes", 0), st.get("guard_trips", 0)))

    def update_ripple(self, counts: Optional[float]) -> None:
        if counts is None:
            return
        c = float(counts)
        col = theme.GREEN if c < 2.0 else theme.YELLOW if c < 3.5 else theme.RED
        self._ripple.setText("ripple %.1f" % c)
        self._ripple.setStyleSheet("font-family: Consolas; font-size: 14px; font-weight: bold; color: %s;" % col)

    def update_scale(self, d: dict) -> None:
        """A DRV:CFG:SCALE(?) reply."""
        if not d:
            return
        if not self._scale.hasFocus():
            self._scale.setValue(int(d.get("current_scale_pct", 100)))
        self._applied.setText("in force: hold %.2f  run %.2f  acc %.2f  dec %.2f A" % (
            float(d.get("hold_a", 0)), float(d.get("run_a", 0)), float(d.get("acc_a", 0)), float(d.get("dec_a", 0))))

    @Slot()
    def _on_recal(self) -> None:
        self._recal.setEnabled(False)
        self._recal_msg.setText("measuring...")
        self.recal_requested.emit()

    def recal_done(self, result) -> None:
        self._recal.setEnabled(True)
        if isinstance(result, Exception) or result is None:
            self._recal_msg.setText("failed: %s" % result)
            return
        m = getattr(result, "measurements", {}) or {}
        if not getattr(result, "ok", False):
            self._recal_msg.setText("not updated: %s" % (getattr(result, "summary", "") or m.get("why", "")))
            return
        self._recal_msg.setText("resonance at %.1f RPM (%.1f Hz): model %.1f -> %.1f Hz at 1 A (%+.1f %%)" % (
            m.get("peak_rpm", 0), m.get("fn_hz", 0), m.get("f1a_old", 0), m.get("f1a_new", 0), m.get("shift_pct", 0)))
