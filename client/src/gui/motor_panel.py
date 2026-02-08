"""
Motor control panel widget.

Provides controls for motor motion, speed, and configuration.
"""

from PySide6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QGridLayout,
    QLabel,
    QPushButton,
    QSpinBox,
    QSlider,
    QGroupBox,
    QFrame,
    QComboBox,
)
from PySide6.QtCore import Qt, Signal, Slot
from PySide6.QtGui import QFont


# State color mapping
_STATE_COLORS = {
    "IDLE": "#888888",
    "ARMED": "#ddaa00",
    "RUNNING": "#00aa00",
    "FAULT": "#ff3333",
    "ESTOP": "#ff3333",
}


class MotorControlPanel(QWidget):
    """Panel for motor control."""

    # Signal emitted when a command should be sent
    command_requested = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._motion_buttons = []  # Buttons disabled during FAULT/ESTOP
        self._setup_ui()

    def _setup_ui(self):
        """Set up the user interface."""
        layout = QVBoxLayout(self)
        layout.setSpacing(12)

        # State indicator + CLEAR_FAULT
        layout.addWidget(self._create_state_group())

        # Motion controls
        layout.addWidget(self._create_motion_group())

        # Speed/Rate controls
        layout.addWidget(self._create_speed_group())

        # Configuration controls
        layout.addWidget(self._create_config_group())

        # Emergency stop (prominent)
        layout.addWidget(self._create_estop_group())

        layout.addStretch()

    def _create_state_group(self) -> QGroupBox:
        """Create state indicator with CLEAR_FAULT button."""
        group = QGroupBox("Controller State")
        layout = QHBoxLayout(group)

        self._state_label = QLabel("---")
        self._state_label.setFont(QFont("Consolas", 14, QFont.Bold))
        self._state_label.setAlignment(Qt.AlignCenter)
        self._state_label.setMinimumWidth(120)
        self._state_label.setStyleSheet(
            "background-color: #333; color: #888; "
            "padding: 8px 16px; border-radius: 6px;"
        )
        layout.addWidget(self._state_label)

        layout.addStretch()

        self._clear_fault_btn = QPushButton("CLEAR FAULT")
        self._clear_fault_btn.setStyleSheet(
            "background-color: #cc6600; color: white; "
            "font-weight: bold; padding: 8px 16px;"
        )
        self._clear_fault_btn.setToolTip("Clear fault state and return to IDLE")
        self._clear_fault_btn.clicked.connect(self._on_clear_fault)
        self._clear_fault_btn.setVisible(False)
        layout.addWidget(self._clear_fault_btn)

        return group

    def _create_motion_group(self) -> QGroupBox:
        """Create motion control group."""
        group = QGroupBox("Motion Control")
        layout = QGridLayout(group)

        # Jog controls
        layout.addWidget(QLabel("Jog:"), 0, 0)

        self._jog_ccw_btn = QPushButton("CCW")
        self._jog_ccw_btn.setToolTip("Jog counter-clockwise")
        self._jog_ccw_btn.pressed.connect(lambda: self._start_jog(0))
        self._jog_ccw_btn.released.connect(self._stop_jog)
        layout.addWidget(self._jog_ccw_btn, 0, 1)

        self._jog_cw_btn = QPushButton("CW")
        self._jog_cw_btn.setToolTip("Jog clockwise")
        self._jog_cw_btn.pressed.connect(lambda: self._start_jog(1))
        self._jog_cw_btn.released.connect(self._stop_jog)
        layout.addWidget(self._jog_cw_btn, 0, 2)

        # Stop button
        self._stop_btn = QPushButton("STOP")
        self._stop_btn.setToolTip("Stop motion (soft stop)")
        self._stop_btn.setStyleSheet("background-color: #ffcc00; font-weight: bold;")
        self._stop_btn.clicked.connect(self._on_stop)
        layout.addWidget(self._stop_btn, 0, 3)

        # Relative move
        layout.addWidget(QLabel("Move:"), 1, 0)

        self._move_steps = QSpinBox()
        self._move_steps.setRange(1, 2097151)
        self._move_steps.setValue(1000)
        self._move_steps.setToolTip("Steps to move")
        layout.addWidget(self._move_steps, 1, 1)

        self._move_ccw_btn = QPushButton("Move CCW")
        self._move_ccw_btn.clicked.connect(lambda: self._on_move(0))
        layout.addWidget(self._move_ccw_btn, 1, 2)

        self._move_cw_btn = QPushButton("Move CW")
        self._move_cw_btn.clicked.connect(lambda: self._on_move(1))
        layout.addWidget(self._move_cw_btn, 1, 3)

        # Absolute move
        layout.addWidget(QLabel("GoTo:"), 2, 0)

        self._goto_pos = QSpinBox()
        self._goto_pos.setRange(-2097152, 2097151)
        self._goto_pos.setValue(0)
        self._goto_pos.setToolTip("Target position")
        layout.addWidget(self._goto_pos, 2, 1)

        self._goto_btn = QPushButton("Go To Position")
        self._goto_btn.clicked.connect(self._on_goto)
        layout.addWidget(self._goto_btn, 2, 2, 1, 2)

        # Home and Zero
        self._home_btn = QPushButton("Home")
        self._home_btn.setToolTip("Go to home position")
        self._home_btn.clicked.connect(self._on_home)
        layout.addWidget(self._home_btn, 3, 1)

        self._zero_btn = QPushButton("Zero")
        self._zero_btn.setToolTip("Set current position as zero")
        self._zero_btn.clicked.connect(self._on_zero)
        layout.addWidget(self._zero_btn, 3, 2)

        # Track motion buttons for state-aware enable/disable
        self._motion_buttons = [
            self._jog_ccw_btn, self._jog_cw_btn,
            self._move_ccw_btn, self._move_cw_btn,
            self._goto_btn, self._home_btn, self._zero_btn,
        ]

        return group

    def _create_speed_group(self) -> QGroupBox:
        """Create speed control group."""
        group = QGroupBox("Speed Control")
        layout = QGridLayout(group)

        # Speed slider
        layout.addWidget(QLabel("Speed:"), 0, 0)

        self._speed_slider = QSlider(Qt.Horizontal)
        self._speed_slider.setRange(0, 15625)  # Max 15625 steps/s per powerSTEP01
        self._speed_slider.setValue(500)
        self._speed_slider.setTickPosition(QSlider.TicksBelow)
        self._speed_slider.setTickInterval(3000)
        self._speed_slider.valueChanged.connect(self._on_speed_slider_changed)
        layout.addWidget(self._speed_slider, 0, 1)

        self._speed_spin = QSpinBox()
        self._speed_spin.setRange(0, 15625)  # Max 15625 steps/s per powerSTEP01
        self._speed_spin.setValue(500)
        self._speed_spin.setSuffix(" steps/s")
        self._speed_spin.valueChanged.connect(self._on_speed_spin_changed)
        layout.addWidget(self._speed_spin, 0, 2)

        # Continuous run
        layout.addWidget(QLabel("Run:"), 1, 0)

        self._run_ccw_btn = QPushButton("Run CCW")
        self._run_ccw_btn.setToolTip("Continuous rotation CCW")
        self._run_ccw_btn.clicked.connect(lambda: self._on_run(0))
        layout.addWidget(self._run_ccw_btn, 1, 1)

        self._run_cw_btn = QPushButton("Run CW")
        self._run_cw_btn.setToolTip("Continuous rotation CW")
        self._run_cw_btn.clicked.connect(lambda: self._on_run(1))
        layout.addWidget(self._run_cw_btn, 1, 2)

        # Add run buttons to motion buttons list
        self._motion_buttons.extend([self._run_ccw_btn, self._run_cw_btn])

        return group

    def _create_config_group(self) -> QGroupBox:
        """Create configuration group."""
        group = QGroupBox("Configuration")
        layout = QGridLayout(group)

        # Acceleration
        layout.addWidget(QLabel("Accel:"), 0, 0)
        self._accel_spin = QSpinBox()
        self._accel_spin.setRange(1, 4095)
        self._accel_spin.setValue(100)
        layout.addWidget(self._accel_spin, 0, 1)

        self._accel_btn = QPushButton("Set")
        self._accel_btn.clicked.connect(self._on_set_accel)
        layout.addWidget(self._accel_btn, 0, 2)

        # Deceleration
        layout.addWidget(QLabel("Decel:"), 1, 0)
        self._decel_spin = QSpinBox()
        self._decel_spin.setRange(1, 4095)
        self._decel_spin.setValue(100)
        layout.addWidget(self._decel_spin, 1, 1)

        self._decel_btn = QPushButton("Set")
        self._decel_btn.clicked.connect(self._on_set_decel)
        layout.addWidget(self._decel_btn, 1, 2)

        # Max speed
        layout.addWidget(QLabel("Max Spd:"), 2, 0)
        self._maxspd_spin = QSpinBox()
        self._maxspd_spin.setRange(1, 1023)
        self._maxspd_spin.setValue(500)
        layout.addWidget(self._maxspd_spin, 2, 1)

        self._maxspd_btn = QPushButton("Set")
        self._maxspd_btn.clicked.connect(self._on_set_maxspd)
        layout.addWidget(self._maxspd_btn, 2, 2)

        # Enable/Disable
        self._enable_btn = QPushButton("Enable")
        self._enable_btn.setToolTip("Enable motor outputs")
        self._enable_btn.clicked.connect(self._on_enable)
        layout.addWidget(self._enable_btn, 3, 0, 1, 2)

        self._disable_btn = QPushButton("Disable (Hi-Z)")
        self._disable_btn.setToolTip("Disable motor outputs")
        self._disable_btn.clicked.connect(self._on_disable)
        layout.addWidget(self._disable_btn, 3, 2)

        return group

    def _create_estop_group(self) -> QGroupBox:
        """Create emergency stop group."""
        group = QGroupBox("Emergency")
        layout = QHBoxLayout(group)

        self._estop_btn = QPushButton("EMERGENCY STOP")
        self._estop_btn.setMinimumHeight(50)
        self._estop_btn.setStyleSheet(
            "background-color: #ff3333; color: white; "
            "font-size: 16px; font-weight: bold;"
        )
        self._estop_btn.setToolTip("Emergency stop - disables outputs immediately")
        self._estop_btn.clicked.connect(self._on_estop)
        layout.addWidget(self._estop_btn)

        return group

    # -------------------------------------------------------------------------
    # State management
    # -------------------------------------------------------------------------

    @Slot(str)
    def update_state(self, state: str):
        """Update controller state display and button enables."""
        color = _STATE_COLORS.get(state, "#888888")

        self._state_label.setText(state)
        self._state_label.setStyleSheet(
            f"background-color: {color}; color: white; "
            "padding: 8px 16px; border-radius: 6px; font-weight: bold;"
        )

        # Show CLEAR_FAULT only in fault states
        is_fault = state in ("FAULT", "ESTOP")
        self._clear_fault_btn.setVisible(is_fault)

        # Disable motion buttons in fault states
        for btn in self._motion_buttons:
            btn.setEnabled(not is_fault)

        # Enable/disable based on state
        self._enable_btn.setEnabled(not is_fault)

    # -------------------------------------------------------------------------
    # Command handlers
    # -------------------------------------------------------------------------

    def _start_jog(self, direction: int):
        """Start jogging in direction."""
        speed = self._speed_spin.value()
        self.command_requested.emit(f"RUN {speed} {direction}")

    def _stop_jog(self):
        """Stop jogging."""
        self.command_requested.emit("STOP")

    @Slot()
    def _on_stop(self):
        """Stop motion."""
        self.command_requested.emit("STOP")

    def _on_move(self, direction: int):
        """Execute relative move."""
        steps = self._move_steps.value()
        self.command_requested.emit(f"MOVE {steps} {direction}")

    @Slot()
    def _on_goto(self):
        """Execute absolute move."""
        pos = self._goto_pos.value()
        self.command_requested.emit(f"GOTO {pos}")

    @Slot()
    def _on_home(self):
        """Go to home position."""
        self.command_requested.emit("HOME")

    @Slot()
    def _on_zero(self):
        """Set current position as zero."""
        self.command_requested.emit("ZERO")

    def _on_run(self, direction: int):
        """Start continuous run."""
        speed = self._speed_spin.value()
        self.command_requested.emit(f"RUN {speed} {direction}")

    @Slot()
    def _on_set_accel(self):
        """Set acceleration."""
        val = self._accel_spin.value()
        self.command_requested.emit(f"ACCEL {val}")

    @Slot()
    def _on_set_decel(self):
        """Set deceleration."""
        val = self._decel_spin.value()
        self.command_requested.emit(f"DECEL {val}")

    @Slot()
    def _on_set_maxspd(self):
        """Set max speed."""
        val = self._maxspd_spin.value()
        self.command_requested.emit(f"MAXSPD {val}")

    @Slot()
    def _on_enable(self):
        """Enable motor outputs."""
        self.command_requested.emit("ENABLE")

    @Slot()
    def _on_disable(self):
        """Disable motor outputs."""
        self.command_requested.emit("DISABLE")

    @Slot()
    def _on_estop(self):
        """Emergency stop."""
        self.command_requested.emit("ESTOP")

    @Slot()
    def _on_clear_fault(self):
        """Clear fault state."""
        self.command_requested.emit("CLEAR_FAULT")

    @Slot(int)
    def _on_speed_slider_changed(self, value: int):
        """Sync spinner with slider."""
        self._speed_spin.blockSignals(True)
        self._speed_spin.setValue(value)
        self._speed_spin.blockSignals(False)

    @Slot(int)
    def _on_speed_spin_changed(self, value: int):
        """Sync slider with spinner."""
        self._speed_slider.blockSignals(True)
        self._speed_slider.setValue(value)
        self._speed_slider.blockSignals(False)
