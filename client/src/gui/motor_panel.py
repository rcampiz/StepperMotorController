"""
Motor control panel widget.

Provides controls for motor motion, speed, and configuration
with vertical fader sliders matching the Driver tab aesthetic.
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
)
from PySide6.QtCore import Qt, Signal, Slot, QTimer
from PySide6.QtGui import QFont
import logging
import time

log = logging.getLogger("motor_panel")


# State color mapping
_STATE_COLORS = {
    "IDLE": "#888888",
    "ARMED": "#ddaa00",
    "RUNNING": "#00aa00",
    "FAULT": "#ff3333",
    "ESTOP": "#ff3333",
}

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
        background: #4a9eff;
        border-radius: 6px;
    }
    QSlider::sub-page:vertical {
        background: #2d2d2d;
        border-radius: 6px;
    }
"""

FADER_STYLE_ORANGE = FADER_STYLE.replace('#4a9eff', '#ff9900')


class MotorControlPanel(QWidget):
    """Panel for motor control."""

    # Signal emitted when a command should be sent
    command_requested = Signal(str)

    # Minimum jog duration (ms) — ensures each tap produces visible motion
    # even if the button press/release is very quick.
    # Set to 500ms to account for command queue delay (~100ms worst case)
    # so the motor gets at least ~400ms of actual run time.
    MIN_JOG_DURATION_MS = 500

    def __init__(self, parent=None):
        super().__init__(parent)
        self._motion_buttons = []  # Buttons disabled during FAULT/ESTOP
        self._motor_enabled = False  # Track Hi-Z state from telemetry
        self._is_fault = False

        # Jog minimum-duration timer
        self._jog_timer = QTimer(self)
        self._jog_timer.setSingleShot(True)
        self._jog_timer.timeout.connect(self._on_jog_timer_expired)
        self._jog_release_pending = False
        self._jog_active = False
        self._jog_start_time = 0.0

        self._setup_ui()

    def _setup_ui(self):
        """Set up the user interface."""
        layout = QVBoxLayout(self)
        layout.setSpacing(12)

        layout.addWidget(self._create_state_group())
        layout.addWidget(self._create_speed_group())
        layout.addWidget(self._create_motion_group())
        layout.addWidget(self._create_config_group())
        layout.addWidget(self._create_estop_group())
        layout.addStretch()

    # =========================================================================
    # Controller State
    # =========================================================================

    def _create_state_group(self) -> QGroupBox:
        """Create state indicator with CLEAR_FAULT button."""
        group = QGroupBox("Controller State")
        outer = QVBoxLayout(group)

        row = QHBoxLayout()
        self._state_label = QLabel("---")
        self._state_label.setFont(QFont("Consolas", 14, QFont.Bold))
        self._state_label.setAlignment(Qt.AlignCenter)
        self._state_label.setMinimumWidth(120)
        self._state_label.setStyleSheet(
            "background-color: #333; color: #888; "
            "padding: 8px 16px; border-radius: 6px;"
        )
        row.addWidget(self._state_label)

        row.addStretch()

        self._clear_fault_btn = QPushButton("CLEAR FAULT")
        self._clear_fault_btn.setStyleSheet(
            "background-color: #cc6600; color: white; "
            "font-weight: bold; padding: 8px 16px;"
        )
        self._clear_fault_btn.setToolTip("Clear fault state and return to IDLE")
        self._clear_fault_btn.clicked.connect(self._on_clear_fault)
        self._clear_fault_btn.setVisible(False)
        row.addWidget(self._clear_fault_btn)

        self._force_clear_btn = QPushButton("FORCE CLEAR")
        self._force_clear_btn.setStyleSheet(
            "background-color: #996600; color: white; "
            "font-weight: bold; padding: 8px 16px;"
        )
        self._force_clear_btn.setToolTip(
            "Force clear fault even if hardware faults are still active (bench/dev use)")
        self._force_clear_btn.clicked.connect(self._on_force_clear_fault)
        self._force_clear_btn.setVisible(False)
        row.addWidget(self._force_clear_btn)
        outer.addLayout(row)

        self._fault_error_label = QLabel("")
        self._fault_error_label.setFont(QFont("Consolas", 9))
        self._fault_error_label.setWordWrap(True)
        self._fault_error_label.setStyleSheet(
            "background-color: #4d1a1a; color: #ff6666; "
            "padding: 6px 8px; border-radius: 4px;"
        )
        self._fault_error_label.setVisible(False)
        outer.addWidget(self._fault_error_label)

        return group

    def show_fault_error(self, message: str):
        """Show a persistent fault error message."""
        self._fault_error_label.setText(message)
        self._fault_error_label.setVisible(True)

    def clear_fault_error(self):
        """Hide the fault error message."""
        self._fault_error_label.setText("")
        self._fault_error_label.setVisible(False)

    # =========================================================================
    # Speed & Run — vertical fader
    # =========================================================================

    def _create_speed_group(self) -> QGroupBox:
        """Create speed control with vertical fader and run buttons."""
        group = QGroupBox("Speed & Run")
        main_layout = QHBoxLayout(group)

        # Left: vertical fader column
        fader_col = QVBoxLayout()
        fader_col.setSpacing(4)

        speed_lbl = QLabel("SPEED")
        speed_lbl.setAlignment(Qt.AlignCenter)
        speed_lbl.setFont(QFont("Segoe UI", 9, QFont.Bold))
        fader_col.addWidget(speed_lbl)

        self._speed_slider = QSlider(Qt.Vertical)
        self._speed_slider.setRange(0, 15609)
        self._speed_slider.setValue(500)
        self._speed_slider.setTickPosition(QSlider.TicksBothSides)
        self._speed_slider.setTickInterval(2500)
        self._speed_slider.setMinimumHeight(120)
        self._speed_slider.setFixedWidth(60)
        self._speed_slider.setStyleSheet(FADER_STYLE_ORANGE)
        fader_col.addWidget(self._speed_slider, alignment=Qt.AlignHCenter)

        self._speed_spin = QSpinBox()
        self._speed_spin.setRange(0, 15609)
        self._speed_spin.setValue(500)
        self._speed_spin.setSuffix(" steps/s")
        self._speed_spin.setAlignment(Qt.AlignCenter)
        self._speed_spin.setFont(QFont("Consolas", 9))
        self._speed_spin.setMinimumWidth(90)
        fader_col.addWidget(self._speed_spin)

        self._maxspd_note = QLabel("Max: 15609 (from MAX SPD)")
        self._maxspd_note.setAlignment(Qt.AlignCenter)
        self._maxspd_note.setStyleSheet("font-size: 8px; color: #888;")
        fader_col.addWidget(self._maxspd_note)

        # Bidirectional sync
        self._speed_slider.valueChanged.connect(self._speed_spin.setValue)
        self._speed_spin.valueChanged.connect(self._speed_slider.setValue)

        main_layout.addLayout(fader_col)

        # Right: Run buttons and STOP
        btn_col = QVBoxLayout()
        btn_col.addStretch()

        run_row = QHBoxLayout()
        self._run_ccw_btn = QPushButton("Run CCW")
        self._run_ccw_btn.setToolTip("Continuous rotation CCW")
        self._run_ccw_btn.setMinimumWidth(90)
        self._run_ccw_btn.clicked.connect(lambda: self._on_run(0))
        run_row.addWidget(self._run_ccw_btn)

        self._run_cw_btn = QPushButton("Run CW")
        self._run_cw_btn.setToolTip("Continuous rotation CW")
        self._run_cw_btn.setMinimumWidth(90)
        self._run_cw_btn.clicked.connect(lambda: self._on_run(1))
        run_row.addWidget(self._run_cw_btn)
        btn_col.addLayout(run_row)

        self._stop_btn = QPushButton("STOP")
        self._stop_btn.setToolTip("Stop motion (soft stop)")
        self._stop_btn.setStyleSheet(
            "background-color: #ffcc00; font-weight: bold;")
        self._stop_btn.clicked.connect(self._on_stop)
        btn_col.addWidget(self._stop_btn)

        btn_col.addStretch()
        main_layout.addLayout(btn_col)

        self._motion_buttons = [self._run_ccw_btn, self._run_cw_btn]

        return group

    @Slot(int)
    def set_max_speed(self, value: int):
        """Update speed fader range from Driver panel's MAX SPD."""
        value = max(1, value)
        current = self._speed_spin.value()
        self._speed_slider.setRange(0, value)
        self._speed_spin.setRange(0, value)
        if current > value:
            self._speed_spin.setValue(value)
        self._maxspd_note.setText(f"Max: {value} (from MAX SPD)")

    # =========================================================================
    # Motion Control — step fader + buttons
    # =========================================================================

    def _create_motion_group(self) -> QGroupBox:
        """Create motion control with step count fader and buttons."""
        group = QGroupBox("Motion Control")
        main_layout = QHBoxLayout(group)

        # Left: step count fader
        fader_col = QVBoxLayout()
        fader_col.setSpacing(4)

        steps_lbl = QLabel("STEPS")
        steps_lbl.setAlignment(Qt.AlignCenter)
        steps_lbl.setFont(QFont("Segoe UI", 9, QFont.Bold))
        fader_col.addWidget(steps_lbl)

        self._move_slider = QSlider(Qt.Vertical)
        self._move_slider.setRange(1, 100000)
        self._move_slider.setValue(1000)
        self._move_slider.setTickPosition(QSlider.TicksBothSides)
        self._move_slider.setTickInterval(20000)
        self._move_slider.setMinimumHeight(120)
        self._move_slider.setFixedWidth(60)
        self._move_slider.setStyleSheet(FADER_STYLE)
        fader_col.addWidget(self._move_slider, alignment=Qt.AlignHCenter)

        self._move_steps = QSpinBox()
        self._move_steps.setRange(1, 2097151)
        self._move_steps.setValue(1000)
        self._move_steps.setSuffix(" steps")
        self._move_steps.setAlignment(Qt.AlignCenter)
        self._move_steps.setFont(QFont("Consolas", 9))
        self._move_steps.setMinimumWidth(90)
        fader_col.addWidget(self._move_steps)

        desc_lbl = QLabel("Relative\nmove")
        desc_lbl.setAlignment(Qt.AlignCenter)
        desc_lbl.setStyleSheet("font-size: 9px; color: #888;")
        fader_col.addWidget(desc_lbl)

        # Bidirectional sync (slider range is capped at 100k, spinbox goes to 2M)
        self._move_slider.valueChanged.connect(self._move_steps.setValue)
        self._move_steps.valueChanged.connect(
            lambda v: self._move_slider.setValue(min(v, 100000)))

        main_layout.addLayout(fader_col)

        # Right: button grid
        btn_col = QVBoxLayout()

        # Jog row
        jog_row = QHBoxLayout()
        jog_lbl = QLabel("Jog:")
        jog_lbl.setFixedWidth(35)
        jog_row.addWidget(jog_lbl)
        self._jog_ccw_btn = QPushButton("CCW")
        self._jog_ccw_btn.setToolTip("Jog counter-clockwise")
        self._jog_ccw_btn.pressed.connect(lambda: self._start_jog(0))
        self._jog_ccw_btn.released.connect(self._stop_jog)
        jog_row.addWidget(self._jog_ccw_btn)
        self._jog_cw_btn = QPushButton("CW")
        self._jog_cw_btn.setToolTip("Jog clockwise")
        self._jog_cw_btn.pressed.connect(lambda: self._start_jog(1))
        self._jog_cw_btn.released.connect(self._stop_jog)
        jog_row.addWidget(self._jog_cw_btn)
        btn_col.addLayout(jog_row)

        # Move row
        move_row = QHBoxLayout()
        move_lbl = QLabel("Move:")
        move_lbl.setFixedWidth(35)
        move_row.addWidget(move_lbl)
        self._move_ccw_btn = QPushButton("CCW")
        self._move_ccw_btn.setToolTip("Move counter-clockwise by step count")
        self._move_ccw_btn.clicked.connect(lambda: self._on_move(0))
        move_row.addWidget(self._move_ccw_btn)
        self._move_cw_btn = QPushButton("CW")
        self._move_cw_btn.setToolTip("Move clockwise by step count")
        self._move_cw_btn.clicked.connect(lambda: self._on_move(1))
        move_row.addWidget(self._move_cw_btn)
        btn_col.addLayout(move_row)

        # GoTo row
        goto_row = QHBoxLayout()
        goto_lbl = QLabel("GoTo:")
        goto_lbl.setFixedWidth(35)
        goto_row.addWidget(goto_lbl)
        self._goto_pos = QSpinBox()
        self._goto_pos.setRange(-2097152, 2097151)
        self._goto_pos.setValue(0)
        self._goto_pos.setToolTip("Target position")
        goto_row.addWidget(self._goto_pos)
        self._goto_btn = QPushButton("Go")
        self._goto_btn.setToolTip("Go to absolute position")
        self._goto_btn.clicked.connect(self._on_goto)
        self._goto_btn.setFixedWidth(50)
        goto_row.addWidget(self._goto_btn)
        btn_col.addLayout(goto_row)

        # Home / Zero Motor row
        home_row = QHBoxLayout()
        home_row.addWidget(QLabel(""))  # spacer matching label width
        self._home_btn = QPushButton("Home")
        self._home_btn.setToolTip("Go to home position")
        self._home_btn.clicked.connect(self._on_home)
        home_row.addWidget(self._home_btn)
        self._zero_btn = QPushButton("Zero Motor")
        self._zero_btn.setToolTip("Reset motor ABS_POS to zero")
        self._zero_btn.clicked.connect(self._on_zero)
        home_row.addWidget(self._zero_btn)
        btn_col.addLayout(home_row)

        # Zero Encoder / Zero All row
        zero_row = QHBoxLayout()
        zero_row.addWidget(QLabel(""))  # spacer matching label width
        self._zero_enc_btn = QPushButton("Zero Encoder")
        self._zero_enc_btn.setToolTip("Reset encoder count to zero")
        self._zero_enc_btn.clicked.connect(self._on_zero_encoder)
        zero_row.addWidget(self._zero_enc_btn)
        self._zero_all_btn = QPushButton("Zero All")
        self._zero_all_btn.setToolTip("Reset both motor position and encoder count")
        self._zero_all_btn.clicked.connect(self._on_zero_all)
        zero_row.addWidget(self._zero_all_btn)
        btn_col.addLayout(zero_row)

        # Hold toggle button
        hold_row = QHBoxLayout()
        hold_row.addWidget(QLabel(""))  # spacer matching label width
        self._hold_toggle_btn = QPushButton("Hold")
        self._hold_toggle_btn.setToolTip(
            "Toggle: energize motor to hold position / release to Hi-Z")
        self._hold_toggle_btn.clicked.connect(self._on_hold_toggle)
        hold_row.addWidget(self._hold_toggle_btn)
        btn_col.addLayout(hold_row)

        main_layout.addLayout(btn_col)

        # Track motion buttons for state-aware enable/disable
        self._motion_buttons.extend([
            self._jog_ccw_btn, self._jog_cw_btn,
            self._move_ccw_btn, self._move_cw_btn,
            self._goto_btn, self._home_btn, self._zero_btn,
            self._zero_enc_btn, self._zero_all_btn,
        ])

        return group

    # =========================================================================
    # Output (Enable/Disable)
    # =========================================================================

    def _create_config_group(self) -> QGroupBox:
        """Create configuration group (enable/disable motor outputs)."""
        group = QGroupBox("Output")
        layout = QHBoxLayout(group)

        self._enable_btn = QPushButton("Enable")
        self._enable_btn.setToolTip("Enable motor outputs")
        self._enable_btn.clicked.connect(self._on_enable)
        layout.addWidget(self._enable_btn)

        self._disable_btn = QPushButton("Disable (Hi-Z)")
        self._disable_btn.setToolTip("Disable motor outputs")
        self._disable_btn.clicked.connect(self._on_disable)
        layout.addWidget(self._disable_btn)

        return group

    # =========================================================================
    # Emergency Stop
    # =========================================================================

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

    # =========================================================================
    # State management
    # =========================================================================

    @Slot(str)
    def update_state(self, state: str):
        """Update controller state display and button enables."""
        color = _STATE_COLORS.get(state, "#888888")

        self._state_label.setText(state)
        self._state_label.setStyleSheet(
            f"background-color: {color}; color: white; "
            "padding: 8px 16px; border-radius: 6px; font-weight: bold;"
        )

        # Show CLEAR_FAULT / FORCE CLEAR only in fault states
        self._is_fault = state in ("FAULT", "ESTOP")
        self._clear_fault_btn.setVisible(self._is_fault)
        self._force_clear_btn.setVisible(self._is_fault)
        if not self._is_fault:
            self.clear_fault_error()

        self._update_motion_buttons()

    @Slot(bool)
    def update_hiz(self, hiz: bool):
        """Update motor enabled state from telemetry Hi-Z flag."""
        self._motor_enabled = not hiz
        self._update_motion_buttons()

        # Visual feedback on enable/disable and hold toggle buttons
        if self._motor_enabled:
            self._enable_btn.setStyleSheet(
                "background-color: #2a7a2a; color: white; font-weight: bold;")
            self._disable_btn.setStyleSheet("")
            self._hold_toggle_btn.setText("Release")
            self._hold_toggle_btn.setStyleSheet(
                "background-color: #2a7a2a; color: white; font-weight: bold;")
        else:
            self._enable_btn.setStyleSheet("")
            self._disable_btn.setStyleSheet(
                "background-color: #7a2a2a; color: white; font-weight: bold;")
            self._hold_toggle_btn.setText("Hold")
            self._hold_toggle_btn.setStyleSheet("")

    def _update_motion_buttons(self):
        """Enable/disable motion buttons based on fault and Hi-Z state."""
        # Motion requires: not in fault AND motor enabled (not Hi-Z)
        can_move = not self._is_fault and self._motor_enabled
        for btn in self._motion_buttons:
            btn.setEnabled(can_move)

        # Enable button: available unless in fault
        self._enable_btn.setEnabled(not self._is_fault)

    # =========================================================================
    # Command handlers
    # =========================================================================

    def _start_jog(self, direction: int):
        """Start jogging in direction.

        Uses a minimum duration timer to ensure each tap produces
        visible motion even if the button press is very brief.
        """
        if self._jog_active:
            log.debug("Jog already active — ignoring repeated press")
            return
        speed = self._speed_spin.value()
        self._jog_active = True
        self._jog_release_pending = False
        self._jog_start_time = time.monotonic()
        log.debug("Jog START: RUN %d dir=%d (min %dms)",
                  speed, direction, self.MIN_JOG_DURATION_MS)
        self.command_requested.emit(f"RUN {speed} {direction}")
        self._jog_timer.start(self.MIN_JOG_DURATION_MS)

    def _stop_jog(self):
        """Stop jogging (on button release).

        If the minimum jog duration hasn't elapsed, defer the STOP
        until the timer fires. Otherwise send STOP immediately.
        """
        elapsed_ms = (time.monotonic() - self._jog_start_time) * 1000
        if self._jog_timer.isActive():
            # Button released before minimum time — defer STOP
            self._jog_release_pending = True
            log.debug("Jog release at %.0fms — deferred (timer has %dms left)",
                      elapsed_ms, self._jog_timer.remainingTime())
        else:
            log.debug("Jog release at %.0fms — sending STOP now", elapsed_ms)
            self._jog_active = False
            self.command_requested.emit("STOP")

    def _on_jog_timer_expired(self):
        """Minimum jog duration elapsed."""
        elapsed_ms = (time.monotonic() - self._jog_start_time) * 1000
        if self._jog_release_pending:
            # Button was already released — send deferred STOP
            self._jog_release_pending = False
            self._jog_active = False
            log.debug("Jog timer expired at %.0fms — sending deferred STOP",
                      elapsed_ms)
            self.command_requested.emit("STOP")
        else:
            log.debug("Jog timer expired at %.0fms — button still held",
                      elapsed_ms)

    @Slot()
    def _on_stop(self):
        """Stop motion."""
        self.command_requested.emit("STOP")

    def _on_move(self, direction: int):
        """Execute relative move at the current speed fader value."""
        speed = self._speed_spin.value()
        steps = self._move_steps.value()
        if speed > 0:
            self.command_requested.emit(f"MOVE {steps} {direction} {speed}")
        else:
            self.command_requested.emit(f"MOVE {steps} {direction}")

    @Slot()
    def _on_goto(self):
        """Execute absolute move at the current speed fader value."""
        speed = self._speed_spin.value()
        pos = self._goto_pos.value()
        if speed > 0:
            self.command_requested.emit(f"GOTO {pos} {speed}")
        else:
            self.command_requested.emit(f"GOTO {pos}")

    @Slot()
    def _on_home(self):
        """Go to home position at the current speed fader value."""
        speed = self._speed_spin.value()
        if speed > 0:
            self.command_requested.emit(f"HOME {speed}")
        else:
            self.command_requested.emit("HOME")

    @Slot()
    def _on_zero(self):
        """Reset motor ABS_POS to zero."""
        self.command_requested.emit("ZERO")

    @Slot()
    def _on_zero_encoder(self):
        """Reset encoder count to zero."""
        self.command_requested.emit("ENC_ZERO")

    @Slot()
    def _on_zero_all(self):
        """Reset both motor position and encoder count."""
        self.command_requested.emit("ZERO_ALL")

    def _on_run(self, direction: int):
        """Start continuous run."""
        speed = self._speed_spin.value()
        self.command_requested.emit(f"RUN {speed} {direction}")

    @Slot()
    def _on_hold_toggle(self):
        """Toggle between hold (enable) and release (Hi-Z)."""
        if self._motor_enabled:
            self.command_requested.emit("DISABLE")
        else:
            self.command_requested.emit("ENABLE")

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

    @Slot()
    def _on_force_clear_fault(self):
        """Force clear fault state, bypassing hardware fault check."""
        self.command_requested.emit("FORCE_CLEAR_FAULT")
