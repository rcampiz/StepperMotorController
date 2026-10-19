"""
Protection configuration panel.

Overcurrent and stall thresholds, the faults that raise the driver's alarm, and the fault
action, in physical units: the overcurrent threshold in amps (with the volts the driver really
compares when it senses a switch voltage, DRV:CAPS? overcurrent_sense=voltage), the stall
threshold on the driver's 0..1 scale (shown in %), the faults by chip-neutral name. Ranges and
steps come from DRV:CAPS? (set_caps); smc_core.driver turns an Apply into the protocol 3
commands or, for a protocol 2 board, into the powerSTEP01 codes.
"""

from PySide6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QLabel,
    QGroupBox,
    QPushButton,
    QDoubleSpinBox,
    QSlider,
    QCheckBox,
    QComboBox,
)
from PySide6.QtCore import Qt, Slot, Signal

from smc_core import commands as C
from smc_core import driver as D
from PySide6.QtGui import QFont


FADER_STYLE = """
    QSlider::groove:vertical {
        background: #2d2d2d;
        width: 22px;
        border-radius: 6px;
    }
    QSlider::handle:vertical {
        background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
            stop:0 #888, stop:0.5 #ccc, stop:1 #888);
        border: 1px solid #666;
        height: 22px;
        margin: 0 -6px;
        border-radius: 4px;
    }
    QSlider::add-page:vertical {
        background: #ff6633;
        border-radius: 6px;
    }
    QSlider::sub-page:vertical {
        background: #2d2d2d;
        border-radius: 6px;
    }
"""

# The faults a driver can alarm on (chip-neutral names, smc_core.driver.FAULT_NAMES):
# (name, label, description)
FAULT_DEFS = [
    ("overcurrent",      "Overcurrent",       "Uses the threshold above"),
    ("undervoltage",     "Undervoltage lockout", "Supply below the driver's lockout level"),
    ("thermal_shutdown", "Thermal shutdown",  "Die temperature limit"),
    ("thermal_warning",  "Thermal warning",   "Die temperature warning"),
    ("stall",            "Stall",             "Uses the stall threshold above (both bridges)"),
    ("command_error",    "Command error",     "A command the driver refused"),
]
FAULT_ACTIONS = ("HardStop", "HardHiZ", "SoftStop")


class ProtectionPanel(QWidget):
    """Panel for the motor driver's protection / fault configuration."""

    protection_apply_requested = Signal(list)   # [Request]
    refresh_requested = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._dirty = False  # True when user has unsaved edits
        self._caps = dict(D.POWERSTEP01_CAPS)
        self._physical = False
        self._setup_ui()
        self.set_caps(self._caps, False)
        self._connect_dirty_tracking()

    # ========================================================================
    # Fader channel helper
    # ========================================================================

    def _create_fader_channel(self, name, description, suffix, decimals):
        """A vertical fader over the driver's steps with a spinbox in physical units (the
        conversion is set by set_caps). Returns (layout, slider, spinbox)."""
        ch = QVBoxLayout()
        ch.setSpacing(4)

        name_lbl = QLabel(name)
        name_lbl.setAlignment(Qt.AlignCenter)
        name_lbl.setFont(QFont("Segoe UI", 9, QFont.Bold))
        ch.addWidget(name_lbl)

        slider = QSlider(Qt.Vertical)
        slider.setTickPosition(QSlider.TicksBothSides)
        slider.setMinimumHeight(120)
        slider.setFixedWidth(60)
        slider.setStyleSheet(FADER_STYLE)
        ch.addWidget(slider, alignment=Qt.AlignHCenter)

        spin = QDoubleSpinBox()
        spin.setDecimals(decimals)
        spin.setSuffix(suffix)
        spin.setAlignment(Qt.AlignCenter)
        spin.setFont(QFont("Consolas", 9))
        spin.setMinimumWidth(90)
        ch.addWidget(spin)

        desc_lbl = QLabel(description)
        desc_lbl.setAlignment(Qt.AlignCenter)
        desc_lbl.setStyleSheet("font-size: 9px; color: #888;")
        ch.addWidget(desc_lbl)
        return ch, slider, spin

    # ========================================================================
    # Setup
    # ========================================================================

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setSpacing(12)

        layout.addWidget(self._create_thresholds_group())
        layout.addWidget(self._create_fault_enables_group())
        layout.addWidget(self._create_buttons_row())
        layout.addStretch()

    def _create_thresholds_group(self) -> QGroupBox:
        group = QGroupBox("Protection Thresholds")
        main_layout = QHBoxLayout(group)
        main_layout.setSpacing(24)

        ch, self._ocd_slider, self._ocd_spin = self._create_fader_channel(
            "OVERCURRENT", "Overcurrent\nthreshold", " A", 1)
        main_layout.addLayout(ch)
        self._ocd_note = QLabel("")
        self._ocd_note.setAlignment(Qt.AlignCenter)
        self._ocd_note.setWordWrap(True)
        self._ocd_note.setMinimumWidth(170)
        self._ocd_note.setStyleSheet("font-size: 10px; color: #ff9900;")
        ch.addWidget(self._ocd_note)

        ch2, self._stall_slider, self._stall_spin = self._create_fader_channel(
            "STALL", "Stall threshold\n(0 % most sensitive)", " %", 1)
        main_layout.addLayout(ch2)
        self._stall_note = QLabel("")
        self._stall_note.setAlignment(Qt.AlignCenter)
        self._stall_note.setStyleSheet("font-size: 10px; color: #ff9900;")
        ch2.addWidget(self._stall_note)

        main_layout.addStretch()

        self._ocd_slider.valueChanged.connect(lambda i: self._sync_spin(self._ocd_spin, self._ocd_amps(i)))
        self._ocd_spin.valueChanged.connect(lambda a: self._sync_slider(self._ocd_slider, self._ocd_index(a)))
        self._stall_slider.valueChanged.connect(
            lambda i: self._sync_spin(self._stall_spin, 100.0 * self._stall_level(i)))
        self._stall_spin.valueChanged.connect(
            lambda p: self._sync_slider(self._stall_slider, self._stall_index(p / 100.0)))
        self._ocd_slider.valueChanged.connect(self._update_ocd_label)
        self._stall_slider.valueChanged.connect(self._update_stall_label)
        return group

    @staticmethod
    def _sync_spin(spin, value):
        if abs(spin.value() - value) > 1e-6:
            spin.setValue(value)

    @staticmethod
    def _sync_slider(slider, index):
        if slider.value() != index:
            slider.setValue(index)

    # --- the driver's grids (DRV:CAPS?) ---------------------------------------------------
    def _ocd_amps(self, i: int) -> float:
        c = self._caps
        return float(c["overcurrent_min_a"]) + int(i) * float(c["overcurrent_step_a"])

    def _ocd_index(self, amps: float) -> int:
        c = self._caps
        step = float(c["overcurrent_step_a"]) or 1.0
        i = int(round((float(amps) - float(c["overcurrent_min_a"])) / step))
        return max(0, min(self._ocd_slider.maximum(), i))

    def _stall_level(self, i: int) -> float:
        return min(1.0, int(i) * float(self._caps.get("stall_step") or 0.01))

    def _stall_index(self, level: float) -> int:
        step = float(self._caps.get("stall_step") or 0.01)
        return max(0, min(self._stall_slider.maximum(), int(round(float(level) / step))))

    def _update_ocd_label(self, *_):
        c = self._caps
        amps = self._ocd_amps(self._ocd_slider.value())
        if c.get("overcurrent_sense") == "voltage" and float(c.get("overcurrent_ohms") or 0) > 0:
            ohms = float(c["overcurrent_ohms"])
            self._ocd_note.setText("%.4g V across the switch\n(~%.0f A estimated at %.2g mΩ)"
                                   % (amps * ohms, amps, ohms * 1000.0))
        else:
            self._ocd_note.setText("%.2f A" % amps)

    def _update_stall_label(self, *_):
        self._stall_note.setText("%.3f of the driver's scale" % self._stall_level(self._stall_slider.value())
                                 if self._caps.get("stall_detect", True) else "no stall detection")

    def set_caps(self, caps: dict, physical: bool) -> None:
        """The driver's ranges and steps (DRV:CAPS?, or the powerSTEP01's for a protocol 2
        board) and whether it takes the physical commands."""
        amps = self._ocd_amps(self._ocd_slider.value())
        level = self._stall_level(self._stall_slider.value())
        self._caps = dict(caps)
        self._physical = bool(physical)
        c = self._caps
        n = int(round((float(c["overcurrent_max_a"]) - float(c["overcurrent_min_a"]))
                      / (float(c["overcurrent_step_a"]) or 1.0)))
        widgets = (self._ocd_slider, self._ocd_spin, self._stall_slider, self._stall_spin)
        for w in widgets:
            w.blockSignals(True)
        self._ocd_slider.setRange(0, max(n, 0))
        self._ocd_slider.setTickInterval(max(1, n // 8))
        self._ocd_spin.setRange(float(c["overcurrent_min_a"]), float(c["overcurrent_max_a"]))
        self._ocd_spin.setSingleStep(float(c["overcurrent_step_a"]))
        m = int(round(1.0 / (float(c.get("stall_step") or 0.01))))
        self._stall_slider.setRange(0, m)
        self._stall_slider.setTickInterval(max(1, m // 8))
        self._stall_spin.setRange(0.0, 100.0)
        self._stall_spin.setSingleStep(100.0 * float(c.get("stall_step") or 0.01))
        self._ocd_slider.setValue(self._ocd_index(amps))
        self._stall_slider.setValue(self._stall_index(level))
        self._ocd_spin.setValue(self._ocd_amps(self._ocd_slider.value()))
        self._stall_spin.setValue(100.0 * self._stall_level(self._stall_slider.value()))
        for w in widgets:
            w.blockSignals(False)
        for w in (self._stall_slider, self._stall_spin):
            w.setEnabled(bool(c.get("stall_detect", True)))
        alarmable = set(c.get("faults_alarmable", [n for n, _, _ in FAULT_DEFS]))
        for name, cb in self._fault_checks.items():
            cb.setVisible(name in alarmable)
        self._update_ocd_label()
        self._update_stall_label()

    # ========================================================================
    # Fault enables group
    # ========================================================================

    def _create_fault_enables_group(self) -> QGroupBox:
        group = QGroupBox("Fault Alarms")
        main_layout = QVBoxLayout(group)

        self._fault_checks = {}
        for key, label, description in FAULT_DEFS:
            row = QHBoxLayout()
            row.setSpacing(8)

            cb = QCheckBox(label)
            cb.setChecked(True)
            cb.setFont(QFont("Segoe UI", 9))
            cb.setMinimumWidth(220)
            row.addWidget(cb)

            desc = QLabel(description)
            desc.setStyleSheet("font-size: 9px; color: #888;")
            row.addWidget(desc)
            row.addStretch()

            self._fault_checks[key] = cb
            main_layout.addLayout(row)

        # Fault action
        main_layout.addSpacing(8)
        action_row = QHBoxLayout()
        action_lbl = QLabel("Fault Action:")
        action_lbl.setFont(QFont("Segoe UI", 9, QFont.Bold))
        action_row.addWidget(action_lbl)

        self._fault_action_combo = QComboBox()
        self._fault_action_combo.addItems(
            ["Hard Stop", "Hard Hi-Z", "Soft Stop"])
        self._fault_action_combo.setFixedWidth(140)
        action_row.addWidget(self._fault_action_combo)
        action_row.addStretch()
        main_layout.addLayout(action_row)

        note = QLabel("Action applies to ALL enabled faults (hardware limitation)")
        note.setStyleSheet("font-size: 9px; color: #888; font-style: italic;")
        main_layout.addWidget(note)

        return group

    # ========================================================================
    # Buttons
    # ========================================================================

    def _create_buttons_row(self) -> QWidget:
        w = QWidget()
        layout = QHBoxLayout(w)
        layout.setContentsMargins(0, 0, 0, 0)

        self._apply_btn = QPushButton("Apply")
        self._apply_btn.setToolTip("Send protection parameters to driver")
        self._apply_btn.clicked.connect(self._on_apply)
        layout.addWidget(self._apply_btn)

        self._refresh_btn = QPushButton("Refresh")
        self._refresh_btn.setToolTip("Read current protection parameters from driver")
        self._refresh_btn.clicked.connect(self._on_refresh)
        layout.addWidget(self._refresh_btn)

        layout.addStretch()
        return w

    # ========================================================================
    # Dirty tracking — ignore external updates while user has pending edits
    # ========================================================================

    def _connect_dirty_tracking(self):
        """Connect UI controls to mark panel dirty on user interaction."""
        self._ocd_slider.valueChanged.connect(self._mark_dirty)
        self._stall_slider.valueChanged.connect(self._mark_dirty)
        for cb in self._fault_checks.values():
            cb.stateChanged.connect(self._mark_dirty)
        self._fault_action_combo.currentIndexChanged.connect(self._mark_dirty)

    @Slot()
    def _mark_dirty(self):
        self._dirty = True

    # ========================================================================
    # Slots
    # ========================================================================

    @Slot()
    def _on_apply(self):
        alarmable = set(self._caps.get("faults_alarmable", [n for n, _, _ in FAULT_DEFS]))
        changes = {
            "overcurrent_a": round(self._ocd_amps(self._ocd_slider.value()), 4),
            "alarms": [n for n, cb in self._fault_checks.items() if cb.isChecked() and n in alarmable],
            "fault_action": self._fault_action_combo.currentIndex(),
        }
        if self._caps.get("stall_detect", True):
            changes["stall_threshold"] = round(self._stall_level(self._stall_slider.value()), 6)
        # Keep _dirty=True so intermediate auto-refreshes are blocked.
        # main_window clears dirty when the final APPLY_PROTECTION response
        # arrives, right before triggering the post-apply refresh.
        self.protection_apply_requested.emit(D.settings_requests(changes, self._physical) + [C.cfg_apply()])

    @Slot()
    def _on_refresh(self):
        self._dirty = False  # User explicitly wants fresh data
        self.refresh_requested.emit()

    # ========================================================================
    # Data update
    # ========================================================================

    def _block(self, on: bool) -> None:
        for w in [self._ocd_spin, self._ocd_slider, self._stall_spin, self._stall_slider,
                  self._fault_action_combo] + list(self._fault_checks.values()):
            w.blockSignals(on)

    def update_physical(self, phys: dict):
        """Protection settings in physical units (smc_core.views.driver_physical, or a protocol
        2 board's DRV:CFG? through smc_core.driver.settings_from_legacy). Skipped while the user
        has unsaved edits (dirty flag set)."""
        if self._dirty:
            return
        self._block(True)
        try:
            if "overcurrent_a" in phys:
                self._ocd_slider.setValue(self._ocd_index(phys["overcurrent_a"]))
                self._ocd_spin.setValue(self._ocd_amps(self._ocd_slider.value()))
            if "stall_threshold" in phys:
                self._stall_slider.setValue(self._stall_index(phys["stall_threshold"]))
                self._stall_spin.setValue(100.0 * self._stall_level(self._stall_slider.value()))
            if "alarms" in phys:
                on = set(phys["alarms"])
                for name, cb in self._fault_checks.items():
                    cb.setChecked(name in on)
            if phys.get("fault_action") in FAULT_ACTIONS:
                self._fault_action_combo.setCurrentIndex(FAULT_ACTIONS.index(phys["fault_action"]))
            self._update_ocd_label()
            self._update_stall_label()
        finally:
            self._block(False)

    def update_protection_params(self, cfg: dict):
        """A DRV:CFG? view (smc_core.views.motor_config), translated (protocol 2 boards)."""
        self.update_physical(D.settings_from_legacy(cfg))

    @Slot()
    def clear(self):
        """Reset all protection controls to defaults."""
        self._dirty = False
        self._block(True)
        self._ocd_slider.setValue(0)
        self._ocd_spin.setValue(self._ocd_amps(0))
        self._stall_slider.setValue(0)
        self._stall_spin.setValue(0.0)
        for cb in self._fault_checks.values():
            cb.setChecked(True)
        self._fault_action_combo.setCurrentIndex(0)
        self._update_ocd_label()
        self._update_stall_label()
        self._block(False)
