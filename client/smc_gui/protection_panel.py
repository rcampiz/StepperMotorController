"""
Protection configuration panel.

Overcurrent / stall thresholds, fault enable flags, and fault action
for the powerSTEP01 driver IC.
"""

from PySide6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QLabel,
    QGroupBox,
    QPushButton,
    QSpinBox,
    QSlider,
    QCheckBox,
    QComboBox,
)
from PySide6.QtCore import Qt, Slot, Signal
from PySide6.QtGui import QFont
import re


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

# Fault enable definitions: (key, label, description)
FAULT_DEFS = [
    ("OCD",     "Overcurrent (OCD)",
     "Uses threshold from slider above"),
    ("TH_SD",   "Thermal Shutdown (TH_SD)",
     "Fixed ~170\u00b0C die temperature"),
    ("TH_WARN", "Thermal Warning (TH_WARN)",
     "Fixed ~130\u00b0C die temperature"),
    ("UVLO",    "Undervoltage Lockout (UVLO)",
     "Fixed ~8V supply"),
    ("STALL_A", "Stall \u2014 Bridge A",
     "Uses threshold from slider above"),
    ("STALL_B", "Stall \u2014 Bridge B",
     "Uses threshold from slider above"),
    ("CMD_ERR", "Command Error (CMD_ERR)",
     "Invalid SPI command to driver"),
]


class ProtectionPanel(QWidget):
    """Panel for powerSTEP01 protection / fault configuration."""

    protection_apply_requested = Signal(list)
    refresh_requested = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._dirty = False  # True when user has unsaved edits
        self._setup_ui()
        self._connect_dirty_tracking()

    # ========================================================================
    # Fader channel helper
    # ========================================================================

    def _create_fader_channel(self, name, description, min_val, max_val,
                              tick_interval):
        """Create a vertical fader with a synced spinbox.

        Returns (layout, slider, spinbox).
        """
        ch = QVBoxLayout()
        ch.setSpacing(4)

        name_lbl = QLabel(name)
        name_lbl.setAlignment(Qt.AlignCenter)
        name_lbl.setFont(QFont("Segoe UI", 9, QFont.Bold))
        ch.addWidget(name_lbl)

        slider = QSlider(Qt.Vertical)
        slider.setRange(min_val, max_val)
        slider.setValue(min_val)
        slider.setTickPosition(QSlider.TicksBothSides)
        slider.setTickInterval(tick_interval)
        slider.setMinimumHeight(120)
        slider.setFixedWidth(60)
        slider.setStyleSheet(FADER_STYLE)
        ch.addWidget(slider, alignment=Qt.AlignHCenter)

        spin = QSpinBox()
        spin.setRange(min_val, max_val)
        spin.setValue(min_val)
        spin.setAlignment(Qt.AlignCenter)
        spin.setFont(QFont("Consolas", 9))
        spin.setMinimumWidth(70)
        ch.addWidget(spin)

        desc_lbl = QLabel(description)
        desc_lbl.setAlignment(Qt.AlignCenter)
        desc_lbl.setStyleSheet("font-size: 9px; color: #888;")
        ch.addWidget(desc_lbl)

        slider.valueChanged.connect(spin.setValue)
        spin.valueChanged.connect(slider.setValue)

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

    # ========================================================================
    # Thresholds group
    # ========================================================================

    def _create_thresholds_group(self) -> QGroupBox:
        group = QGroupBox("Protection Thresholds")
        main_layout = QHBoxLayout(group)
        main_layout.setSpacing(24)

        # OCD fader
        ch, self._ocd_slider, self._ocd_spin = \
            self._create_fader_channel("OCD", "Overcurrent\nthreshold",
                                       0, 31, 8)
        main_layout.addLayout(ch)

        self._ocd_ma_label = QLabel("375 mA")
        self._ocd_ma_label.setAlignment(Qt.AlignCenter)
        self._ocd_ma_label.setStyleSheet("font-size: 10px; color: #ff9900;")
        ch.addWidget(self._ocd_ma_label)

        # Stall fader (powerSTEP01: 5-bit register, 0-31)
        ch2, self._stall_slider, self._stall_spin = \
            self._create_fader_channel("STALL", "Back-EMF\nthreshold",
                                       0, 31, 8)
        main_layout.addLayout(ch2)

        self._stall_mv_label = QLabel("31 mV (BEMF)")
        self._stall_mv_label.setAlignment(Qt.AlignCenter)
        self._stall_mv_label.setStyleSheet("font-size: 10px; color: #ff9900;")
        ch2.addWidget(self._stall_mv_label)

        main_layout.addStretch()

        # Connect for live mA updates
        self._ocd_spin.valueChanged.connect(self._update_ocd_label)
        self._stall_spin.valueChanged.connect(self._update_stall_mv_label)
        self._update_ocd_label()
        self._update_stall_mv_label()

        return group

    def _update_ocd_label(self):
        ma = (self._ocd_spin.value() + 1) * 375
        self._ocd_ma_label.setText(f"{ma} mA")

    def _update_stall_mv_label(self):
        mv = (self._stall_spin.value() + 1) * 31.25
        self._stall_mv_label.setText(f"{mv:.0f} mV (BEMF)")

    # ========================================================================
    # Fault enables group
    # ========================================================================

    def _create_fault_enables_group(self) -> QGroupBox:
        group = QGroupBox("Fault Enables")
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
        self._ocd_spin.valueChanged.connect(self._mark_dirty)
        self._stall_spin.valueChanged.connect(self._mark_dirty)
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
        ocd = self._ocd_spin.value()
        stall = self._stall_spin.value()
        fault_flags = " ".join(
            "1" if self._fault_checks[key].isChecked() else "0"
            for key, _, _ in FAULT_DEFS)
        action = self._fault_action_combo.currentIndex()
        # Keep _dirty=True so intermediate auto-refreshes are blocked.
        # main_window clears dirty when the final APPLY_PROTECTION response
        # arrives, right before triggering the post-apply refresh.
        self.protection_apply_requested.emit([
            f"DRV:CFG:OCD {ocd}",
            f"DRV:CFG:STALL {stall}",
            f"DRV:CFG:FAULT {fault_flags}",
            f"DRV:CFG:FAULT ACTION {action}",
            "DRV:CFG:APPLY",
        ])

    @Slot()
    def _on_refresh(self):
        self._dirty = False  # User explicitly wants fresh data
        self.refresh_requested.emit()

    # ========================================================================
    # Data update
    # ========================================================================

    def update_protection_params(self, response: str):
        """Parse protection fields from MCONFIG response.

        Skipped if the user has unsaved edits (dirty flag set).
        """
        if self._dirty:
            return

        # Block signals so programmatic updates don't re-trigger _mark_dirty
        self._ocd_spin.blockSignals(True)
        self._ocd_slider.blockSignals(True)
        self._stall_spin.blockSignals(True)
        self._stall_slider.blockSignals(True)
        for cb in self._fault_checks.values():
            cb.blockSignals(True)
        self._fault_action_combo.blockSignals(True)

        try:
            # OCD threshold (hex)
            ocd_match = re.search(r'OCD_TH=([0-9A-Fa-f]+)', response)
            if ocd_match:
                val = int(ocd_match.group(1), 16)
                self._ocd_spin.setValue(val)
                self._ocd_slider.setValue(val)

            # Stall threshold (hex)
            stall_match = re.search(r'STALL_TH=([0-9A-Fa-f]+)', response)
            if stall_match:
                val = int(stall_match.group(1), 16)
                self._stall_spin.setValue(val)
                self._stall_slider.setValue(val)

            # Fault enable flags
            fault_match = re.search(
                r'Faults:\s*OCD=(\d+)\s+TH_SD=(\d+)\s+TH_W=(\d+)\s+'
                r'UVLO=(\d+)\s+STALL_A=(\d+)\s+STALL_B=(\d+)\s+CMD=(\d+)',
                response)
            if fault_match:
                keys = [key for key, _, _ in FAULT_DEFS]
                for i, key in enumerate(keys):
                    self._fault_checks[key].setChecked(
                        fault_match.group(i + 1) != "0")

            # Fault action
            action_match = re.search(r'Action:\s*(\w+)', response)
            if action_match:
                idx = {"HardStop": 0, "HardHiZ": 1, "SoftStop": 2}.get(
                    action_match.group(1), 0)
                self._fault_action_combo.setCurrentIndex(idx)

            # Update mA labels (signals are blocked so the slots didn't fire)
            self._update_ocd_label()
            self._update_stall_mv_label()
        finally:
            self._ocd_spin.blockSignals(False)
            self._ocd_slider.blockSignals(False)
            self._stall_spin.blockSignals(False)
            self._stall_slider.blockSignals(False)
            for cb in self._fault_checks.values():
                cb.blockSignals(False)
            self._fault_action_combo.blockSignals(False)

    @Slot()
    def clear(self):
        """Reset all protection controls to defaults."""
        self._dirty = False
        self._ocd_spin.blockSignals(True)
        self._ocd_slider.blockSignals(True)
        self._stall_spin.blockSignals(True)
        self._stall_slider.blockSignals(True)
        for cb in self._fault_checks.values():
            cb.blockSignals(True)
        self._fault_action_combo.blockSignals(True)

        self._ocd_spin.setValue(0)
        self._ocd_slider.setValue(0)
        self._stall_spin.setValue(0)
        self._stall_slider.setValue(0)
        for cb in self._fault_checks.values():
            cb.setChecked(True)
        self._fault_action_combo.setCurrentIndex(0)

        self._update_ocd_label()
        self._update_stall_mv_label()

        self._ocd_spin.blockSignals(False)
        self._ocd_slider.blockSignals(False)
        self._stall_spin.blockSignals(False)
        self._stall_slider.blockSignals(False)
        for cb in self._fault_checks.values():
            cb.blockSignals(False)
        self._fault_action_combo.blockSignals(False)
