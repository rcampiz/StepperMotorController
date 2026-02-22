"""
Microstep resolution configuration panel.

Allows selecting the powerSTEP01 STEP_MODE register value (0-7)
to set microstep resolution from full step to 1/128.
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
from PySide6.QtGui import QFont
import re


# (register value, display label, denominator)
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
    """Panel for powerSTEP01 microstep resolution configuration."""

    stepmode_apply_requested = Signal(str)  # command string
    refresh_requested = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._dirty = False
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

    @Slot()
    def _on_apply(self):
        reg_val = self._mode_combo.currentIndex()
        self.stepmode_apply_requested.emit(f"MCONFIG_STEPMODE {reg_val}")

    @Slot()
    def _on_refresh(self):
        self._dirty = False
        self.refresh_requested.emit()

    def update_step_mode(self, response: str):
        """Parse step mode from MCONFIG response.

        Looks for 'StepMode: <n> (1/<d>)' in the MCONFIG output.
        Skipped if the user has unsaved edits (dirty flag set).
        """
        if self._dirty:
            return

        match = re.search(r'StepMode:\s*(\d+)\s*\(1/(\d+)\)', response)
        if not match:
            return

        reg_val = int(match.group(1))
        denom = int(match.group(2))

        self._mode_combo.blockSignals(True)
        try:
            if 0 <= reg_val <= 7:
                self._mode_combo.setCurrentIndex(reg_val)
            self._current_label.setText(f"1/{denom} microstep (reg={reg_val})")
        finally:
            self._mode_combo.blockSignals(False)

    @Slot()
    def clear(self):
        """Reset to defaults on disconnect."""
        self._dirty = False
        self._mode_combo.blockSignals(True)
        self._mode_combo.setCurrentIndex(7)
        self._mode_combo.blockSignals(False)
        self._current_label.setText("--")
