"""
Microstep resolution configuration panel.

Selects the microsteps per full step (full step to the driver's finest resolution, DRV:CAPS?)
and shows the resolution in force as the driver reads it back. A protocol 3 board takes
DRV:CFG:USTEP, a protocol 2 one DRV:CFG:STEPMODE (smc_core.driver picks).
"""

from PySide6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QLabel,
    QGroupBox,
    QPushButton,
    QComboBox,
)
from PySide6.QtCore import Qt, Slot, Signal

from smc_core import driver as D
from PySide6.QtGui import QFont


# (log2 of the microsteps, display label, microsteps per full step)
STEP_MODES = [
    (0, "Full Step (1)", 1),
    (1, "Half Step (1/2)", 2),
    (2, "1/4 Step", 4),
    (3, "1/8 Step", 8),
    (4, "1/16 Step", 16),
    (5, "1/32 Step", 32),
    (6, "1/64 Step", 64),
    (7, "1/128 Step", 128),
]


class StepModePanel(QWidget):
    """Panel for the motor driver's microstep resolution."""

    stepmode_apply_requested = Signal(object)  # Request
    refresh_requested = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._dirty = False
        self._config_mode = None   # the stored settings (DRV:CFG:PHYS? / DRV:CFG?), log2
        self._chip_mode = None     # the driver's readback (DRV:USTEP? / DRV:STEP_MODE?), log2
        self._caps = dict(D.POWERSTEP01_CAPS)
        self._physical = False
        self._setup_ui()
        self._connect_dirty_tracking()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setSpacing(12)

        layout.addWidget(self._create_stepmode_group())
        layout.addWidget(self._create_buttons_row())
        layout.addStretch()

    def _create_stepmode_group(self) -> QGroupBox:
        group = QGroupBox("Microstep Resolution")
        main_layout = QVBoxLayout(group)
        main_layout.setSpacing(8)

        # Current value label
        status_row = QHBoxLayout()
        status_lbl = QLabel("Current:")
        status_lbl.setFont(QFont("Segoe UI", 9, QFont.Bold))
        status_row.addWidget(status_lbl)

        self._current_label = QLabel("--")
        self._current_label.setFont(QFont("Consolas", 10))
        self._current_label.setStyleSheet("color: #ff9900;")
        status_row.addWidget(self._current_label)
        status_row.addStretch()
        main_layout.addLayout(status_row)

        main_layout.addSpacing(4)

        # Combo box for selection
        select_row = QHBoxLayout()
        select_lbl = QLabel("Set Mode:")
        select_lbl.setFont(QFont("Segoe UI", 9, QFont.Bold))
        select_row.addWidget(select_lbl)

        self._mode_combo = QComboBox()
        for _reg_val, label, _denom in STEP_MODES:
            self._mode_combo.addItem(label)
        self._mode_combo.setCurrentIndex(7)  # Default: 1/128
        self._mode_combo.setFixedWidth(200)
        select_row.addWidget(self._mode_combo)
        select_row.addStretch()
        main_layout.addLayout(select_row)

        # Info note
        note = QLabel(
            "Lower microstep = more torque, rougher motion.\n"
            "Higher microstep = smoother motion, less torque per step.\n"
            "Motor must be stopped (Hi-Z or idle) to change step mode safely."
        )
        note.setStyleSheet("font-size: 9px; color: #888; font-style: italic;")
        main_layout.addWidget(note)

        return group

    def _create_buttons_row(self) -> QWidget:
        w = QWidget()
        layout = QHBoxLayout(w)
        layout.setContentsMargins(0, 0, 0, 0)

        self._apply_btn = QPushButton("Apply")
        self._apply_btn.setToolTip("Send microstep mode to driver and save")
        self._apply_btn.clicked.connect(self._on_apply)
        layout.addWidget(self._apply_btn)

        self._refresh_btn = QPushButton("Refresh")
        self._refresh_btn.setToolTip("Read current microstep mode from driver")
        self._refresh_btn.clicked.connect(self._on_refresh)
        layout.addWidget(self._refresh_btn)

        layout.addStretch()
        return w

    def _connect_dirty_tracking(self):
        self._mode_combo.currentIndexChanged.connect(self._mark_dirty)

    @Slot()
    def _mark_dirty(self):
        self._dirty = True

    def set_caps(self, caps: dict, physical: bool) -> None:
        """The driver's finest resolution (DRV:CAPS?) limits the choices; physical picks the
        command (DRV:CFG:USTEP or the protocol 2 DRV:CFG:STEPMODE)."""
        self._caps = dict(caps)
        self._physical = bool(physical)
        finest = max(int(caps.get("max_microsteps_voltage") or 1), int(caps.get("max_microsteps_current") or 1))
        top = finest.bit_length() - 1
        self._mode_combo.blockSignals(True)
        try:
            keep = self._mode_combo.currentIndex()
            self._mode_combo.clear()
            for log2, label, _n in STEP_MODES[:top + 1]:
                self._mode_combo.addItem(label)
            for log2 in range(len(STEP_MODES), top + 1):
                self._mode_combo.addItem("1/%d Step" % (1 << log2))
            self._mode_combo.setCurrentIndex(min(keep if keep >= 0 else top, top))
        finally:
            self._mode_combo.blockSignals(False)
        self._render_current()

    @Slot()
    def _on_apply(self):
        microsteps = 1 << self._mode_combo.currentIndex()
        self.stepmode_apply_requested.emit(D.settings_requests({"microsteps": microsteps}, self._physical)[0])

    @Slot()
    def _on_refresh(self):
        self._dirty = False
        self.refresh_requested.emit()

    def update_physical(self, phys: dict):
        """The stored settings (smc_core.views.driver_physical): the microsteps asked for."""
        if "microsteps_asked" in phys:
            self.update_step_mode({"step_mode": int(phys["microsteps_asked"]).bit_length() - 1})

    def update_step_mode(self, cfg: dict):
        """The step mode (log2 of the microsteps) of the stored settings, from a DRV:CFG? view.
        Skipped if the user has unsaved edits (dirty flag set)."""
        if self._dirty:
            return

        if "step_mode" not in cfg:
            return
        reg_val = cfg["step_mode"]
        self._config_mode = reg_val

        self._mode_combo.blockSignals(True)
        try:
            if 0 <= reg_val < self._mode_combo.count():
                self._mode_combo.setCurrentIndex(reg_val)
            self._render_current()
        finally:
            self._mode_combo.blockSignals(False)

    def update_chip_step_mode(self, data: dict):
        """The resolution read back from the driver (smc_core.views.resolution of DRV:USTEP? or
        DRV:STEP_MODE?). Shown as the current value, with a warning when the stored settings
        disagree (a driver may take a new resolution only with its outputs off)."""
        if "step_mode" not in data:
            return
        self._chip_mode = int(data["step_mode"])
        if "drive_mode" in data:
            self._current_mode = str(data["drive_mode"]) == "current"
        self._render_current()

    def _render_current(self):
        chip, cfg = self._chip_mode, self._config_mode
        if chip is None and cfg is None:
            return
        shown = chip if chip is not None else cfg
        text = f"1/{1 << shown} microstep (mode {shown})"
        if chip is None:
            text += "  [config]"
        # The driver caps the resolution per drive mode (DRV:CAPS?; the powerSTEP01 runs 1/16 at
        # most in current mode): in force below the request is right, not a mismatch
        expected = cfg
        drive = "current" if getattr(self, "_current_mode", False) else "voltage"
        if cfg is not None:
            expected = D.microsteps_in_force(self._caps, 1 << cfg, drive).bit_length() - 1
        if chip is not None and cfg is not None and chip == expected and expected != cfg:
            text += f"  -- {drive} mode runs 1/{1 << expected} at most (config 1/{1 << cfg})"
        if chip is not None and expected is not None and chip != expected:
            text += f"  -- config says 1/{1 << expected}: apply again with the motor idle"
            self._current_label.setStyleSheet("color: #ff4040;")
        else:
            self._current_label.setStyleSheet("color: #ff9900;")
        self._current_label.setText(text)

    @Slot()
    def clear(self):
        """Reset to defaults on disconnect."""
        self._dirty = False
        self._mode_combo.blockSignals(True)
        self._mode_combo.setCurrentIndex(7)
        self._mode_combo.blockSignals(False)
        self._current_label.setText("--")
