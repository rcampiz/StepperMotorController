"""
Motion control panel with physical-unit inputs.

Provides revolution/RPM/degree-based motion controls alongside
the existing step-based motor panel. Unit conversion is done
client-side using motor config queried from the device.

Includes visual motion profile preview and signal flow diagram
for intuitive understanding of the control path.
"""

from PySide6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QGridLayout,
    QLabel,
    QPushButton,
    QDoubleSpinBox,
    QSpinBox,
    QGroupBox,
    QComboBox,
    QFrame,
    QScrollArea,
)
from PySide6.QtCore import Qt, Signal, Slot
from PySide6.QtGui import QFont, QPainter, QColor, QPen, QBrush, QPainterPath
import pyqtgraph as pg
import logging
import re

log = logging.getLogger("motion_panel")

# Default motor configuration (overridden on connect)
DEFAULT_FULL_STEPS_PER_REV = 200
DEFAULT_STEP_MODE = 0        # Full step (conservative default until queried)
DEFAULT_ENCODER_PPR = 4000

# Unit indices for combo boxes
SPEED_UNITS = ["RPM", "rev/s", "steps/s"]
POSITION_UNITS = ["rev", "deg", "steps"]
ACCEL_UNITS = ["rev/s\u00b2", "steps/s\u00b2"]

# ─────────────────────────────────────────────────────────────────────────────
# Signal flow diagram widget
# ─────────────────────────────────────────────────────────────────────────────

# Colors
_BOX_BG = "#2a3a4a"
_BOX_BORDER = "#4a6a8a"
_BOX_TEXT = "#ddeeff"
_ARROW_COLOR = "#4a9eff"
_FEEDBACK_COLOR = "#ffaa00"
_MONITOR_COLOR = "#666666"
_LABEL_COLOR = "#aabbcc"

_BOX_STYLE = (
    "background-color: {bg}; color: {fg}; border: 2px solid {border}; "
    "border-radius: 6px; padding: 4px 10px; font-weight: bold; "
    "font-size: 10px;"
)
_ARROW_STYLE = (
    "color: {color}; font-size: 18px; font-weight: bold; padding: 0 4px;"
)


class FlowDiagram(QWidget):
    """Visual signal flow diagram for the control path.

    Shows the signal path from user command through motor to encoder,
    with different styling for open-loop (monitoring) vs closed-loop
    (feedback) paths.
    """

    def __init__(self, parent=None):
        super().__init__(parent)
        self._mode = "OPEN_LOOP"
        self._setup_ui()

    def _make_box(self, text: str, bg=_BOX_BG, border=_BOX_BORDER,
                  fg=_BOX_TEXT) -> QLabel:
        lbl = QLabel(text)
        lbl.setAlignment(Qt.AlignCenter)
        lbl.setStyleSheet(_BOX_STYLE.format(bg=bg, border=border, fg=fg))
        lbl.setMinimumHeight(32)
        return lbl

    def _make_arrow(self, text="\u25ba", color=_ARROW_COLOR) -> QLabel:
        lbl = QLabel(text)
        lbl.setAlignment(Qt.AlignCenter)
        lbl.setStyleSheet(_ARROW_STYLE.format(color=color))
        return lbl

    def _setup_ui(self):
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(2)

        # ── Forward path ──
        fwd = QHBoxLayout()
        fwd.setSpacing(0)

        self._cmd_box = self._make_box("Command\n(RPM / rev)")
        fwd.addWidget(self._cmd_box)

        fwd.addWidget(self._make_arrow())

        self._conv_box = self._make_box("Unit\nConvert",
                                        bg="#2a3a2a", border="#4a8a4a")
        fwd.addWidget(self._conv_box)

        fwd.addWidget(self._make_arrow())

        self._driver_box = self._make_box("powerSTEP01\nDriver",
                                          bg="#3a2a4a", border="#6a4a8a")
        fwd.addWidget(self._driver_box)

        fwd.addWidget(self._make_arrow())

        self._motor_box = self._make_box("Stepper\nMotor",
                                         bg="#4a3a2a", border="#8a6a4a")
        fwd.addWidget(self._motor_box)

        outer.addLayout(fwd)

        # ── Feedback path ──
        fb = QHBoxLayout()
        fb.setSpacing(0)

        self._fb_label = QLabel("monitoring only (no correction)")
        self._fb_label.setAlignment(Qt.AlignCenter)
        self._fb_label.setStyleSheet(
            f"color: {_MONITOR_COLOR}; font-size: 9px; font-style: italic;")
        fb.addWidget(self._fb_label, 3)

        self._fb_arrow = self._make_arrow("\u25c4 \u25c4 \u25c4",
                                          color=_MONITOR_COLOR)
        fb.addWidget(self._fb_arrow)

        self._enc_box = self._make_box("Encoder\nFeedback",
                                       bg="#2a2a2a", border="#555555")
        fb.addWidget(self._enc_box)

        outer.addLayout(fb)

    def set_mode(self, mode: str):
        """Update flow diagram for current control mode.

        Supports: OPEN_LOOP, CLOSED_LOOP_MONITOR, SPEED_TRIM
        """
        self._mode = mode
        if "TRIM" in mode or "PID" in mode:
            # Speed trim mode: active correction feedback (green tint)
            self._fb_label.setText("speed trim correction (active feedback)")
            self._fb_label.setStyleSheet(
                f"color: {_FEEDBACK_COLOR}; font-size: 9px; "
                "font-weight: bold;")
            self._fb_arrow.setStyleSheet(
                _ARROW_STYLE.format(color=_FEEDBACK_COLOR))
            self._enc_box.setStyleSheet(
                _BOX_STYLE.format(bg="#3a3a1a", border="#8a8a3a",
                                  fg=_BOX_TEXT))
        elif "MONITOR" in mode:
            # Monitor mode: slip detection, no trim (yellow tint)
            self._fb_label.setText("slip detection (no trim correction)")
            self._fb_label.setStyleSheet(
                "color: #ccaa44; font-size: 9px; font-weight: bold;")
            self._fb_arrow.setStyleSheet(
                _ARROW_STYLE.format(color="#ccaa44"))
            self._enc_box.setStyleSheet(
                _BOX_STYLE.format(bg="#3a3a2a", border="#8a8a4a",
                                  fg=_BOX_TEXT))
        else:
            # Open loop: monitoring only (dim)
            self._fb_label.setText("monitoring only (no correction)")
            self._fb_label.setStyleSheet(
                f"color: {_MONITOR_COLOR}; font-size: 9px; "
                "font-style: italic;")
            self._fb_arrow.setStyleSheet(
                _ARROW_STYLE.format(color=_MONITOR_COLOR))
            self._enc_box.setStyleSheet(
                _BOX_STYLE.format(bg="#2a2a2a", border="#555555",
                                  fg=_BOX_TEXT))


# ─────────────────────────────────────────────────────────────────────────────
# Motion panel
# ─────────────────────────────────────────────────────────────────────────────

class MotionPanel(QWidget):
    """Panel for physical-unit motion control."""

    # Signals
    command_requested = Signal(str)
    config_refresh_requested = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)

        # Motor configuration (updated from device)
        self._full_steps_per_rev = DEFAULT_FULL_STEPS_PER_REV
        self._step_mode = DEFAULT_STEP_MODE
        self._encoder_ppr = DEFAULT_ENCODER_PPR
        self._transport_delay_ms = 0
        self._mode_change_pending = False  # Suppress telemetry combo override

        self._setup_ui()

    @property
    def _microsteps_per_rev(self) -> int:
        return self._full_steps_per_rev * (1 << self._step_mode)

    # =========================================================================
    # Unit conversion helpers
    # =========================================================================

    def _speed_to_steps_per_sec(self, value: float, unit_index: int) -> int:
        """Convert speed in user units to steps/s (full steps).

        The firmware RUN command and SPEED register use full steps/s,
        not microsteps/s.  Use full_steps_per_rev for conversion.
        """
        fpr = self._full_steps_per_rev
        if unit_index == 0:    # RPM
            return round(value * fpr / 60.0)
        elif unit_index == 1:  # rev/s
            return round(value * fpr)
        else:                  # steps/s
            return round(value)

    def _position_to_microsteps(self, value: float, unit_index: int) -> int:
        """Convert position in user units to microsteps."""
        mpr = self._microsteps_per_rev
        if unit_index == 0:    # rev
            return round(value * mpr)
        elif unit_index == 1:  # deg
            return round(value / 360.0 * mpr)
        else:                  # steps
            return round(value)

    def _accel_to_steps_per_sec_sq(self, value: float,
                                    unit_index: int) -> int:
        """Convert acceleration in user units to steps/s^2 (full steps).

        ACC/DEC registers use full steps/s^2 (same as SPEED register).
        """
        fpr = self._full_steps_per_rev
        if unit_index == 0:    # rev/s^2
            return round(value * fpr)
        else:                  # steps/s^2
            return round(value)

    def _microsteps_to_position(self, usteps: int, unit_index: int) -> float:
        """Convert microsteps to display position."""
        mpr = self._microsteps_per_rev
        if mpr == 0:
            return 0.0
        if unit_index == 0:    # rev
            return usteps / mpr
        elif unit_index == 1:  # deg
            return usteps / mpr * 360.0
        else:                  # steps
            return float(usteps)

    def _steps_per_sec_to_speed(self, sps: int, unit_index: int) -> float:
        """Convert steps/s (full steps) to display speed.

        GET_STATUS speed is in full steps/s — use full_steps_per_rev.
        """
        fpr = self._full_steps_per_rev
        if fpr == 0:
            return 0.0
        if unit_index == 0:    # RPM
            return sps * 60.0 / fpr
        elif unit_index == 1:  # rev/s
            return sps / fpr
        else:                  # steps/s
            return float(sps)

    # =========================================================================
    # UI Setup
    # =========================================================================

    def _setup_ui(self):
        # Wrap everything in a scroll area so it fits on smaller screens
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)

        inner = QWidget()
        layout = QVBoxLayout(inner)
        layout.setSpacing(8)

        layout.addWidget(self._create_mode_header())
        layout.addWidget(self._create_flow_diagram())
        layout.addWidget(self._create_run_group())
        layout.addWidget(self._create_position_group())
        layout.addWidget(self._create_config_group())
        layout.addWidget(self._create_trim_group())
        layout.addWidget(self._create_readback_group())
        layout.addWidget(self._create_timing_group())
        layout.addStretch()

        scroll.setWidget(inner)

        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.addWidget(scroll)

    # =========================================================================
    # Mode Header
    # =========================================================================

    def _create_mode_header(self) -> QWidget:
        w = QWidget()
        row = QHBoxLayout(w)
        row.setContentsMargins(0, 0, 0, 0)

        title = QLabel("Motion Control")
        title.setFont(QFont("Segoe UI", 11, QFont.Bold))
        row.addWidget(title)

        row.addStretch()

        # Mode selector combo
        row.addWidget(QLabel("Mode:"))
        self._mode_combo = QComboBox()
        self._mode_combo.addItems(["Open Loop", "Monitor", "Speed Trim"])
        self._mode_combo.setCurrentIndex(0)
        self._mode_combo.setMinimumWidth(170)
        self._mode_combo.setFont(QFont("Consolas", 9, QFont.Bold))
        self._mode_combo.setToolTip(
            "Open Loop: direct speed command to driver\n"
            "Monitor: encoder detects slip, Tier 2/3 response (no trim)\n"
            "Speed Trim: PI speed-trim correction + fault detection")
        self._mode_combo.currentIndexChanged.connect(self._on_mode_changed)
        row.addWidget(self._mode_combo)

        # Config info
        self._config_label = QLabel("")
        self._config_label.setFont(QFont("Consolas", 8))
        self._config_label.setStyleSheet("color: #888;")
        row.addWidget(self._config_label)

        return w

    # =========================================================================
    # Flow Diagram
    # =========================================================================

    def _create_flow_diagram(self) -> QGroupBox:
        group = QGroupBox("Signal Flow")
        layout = QVBoxLayout(group)
        layout.setContentsMargins(8, 8, 8, 4)

        self._flow_diagram = FlowDiagram()
        layout.addWidget(self._flow_diagram)

        return group

    # =========================================================================
    # Continuous Run
    # =========================================================================

    def _create_run_group(self) -> QGroupBox:
        group = QGroupBox("Continuous Run")
        layout = QVBoxLayout(group)

        # Speed row
        speed_row = QHBoxLayout()
        speed_row.addWidget(QLabel("Speed:"))

        self._run_speed = QDoubleSpinBox()
        self._run_speed.setRange(0.0, 99999.0)
        self._run_speed.setValue(60.0)
        self._run_speed.setDecimals(2)
        self._run_speed.setAlignment(Qt.AlignRight)
        self._run_speed.setFont(QFont("Consolas", 10))
        self._run_speed.setMinimumWidth(120)
        speed_row.addWidget(self._run_speed)

        self._run_unit = QComboBox()
        self._run_unit.addItems(SPEED_UNITS)
        self._run_unit.setCurrentIndex(0)  # RPM default
        self._run_unit.setMinimumWidth(80)
        speed_row.addWidget(self._run_unit)

        speed_row.addStretch()
        layout.addLayout(speed_row)

        # Direction + stop buttons
        btn_row = QHBoxLayout()
        self._run_ccw_btn = QPushButton("\u25c4 CCW")
        self._run_ccw_btn.setToolTip("Run counter-clockwise")
        self._run_ccw_btn.clicked.connect(lambda: self._on_run(0))
        btn_row.addWidget(self._run_ccw_btn)

        self._run_cw_btn = QPushButton("CW \u25ba")
        self._run_cw_btn.setToolTip("Run clockwise")
        self._run_cw_btn.clicked.connect(lambda: self._on_run(1))
        btn_row.addWidget(self._run_cw_btn)

        self._stop_btn = QPushButton("\u25a0 STOP")
        self._stop_btn.setStyleSheet(
            "background-color: #ffcc00; font-weight: bold;")
        self._stop_btn.clicked.connect(self._on_stop)
        btn_row.addWidget(self._stop_btn)

        layout.addLayout(btn_row)
        return group

    # =========================================================================
    # Positioning
    # =========================================================================

    def _create_position_group(self) -> QGroupBox:
        group = QGroupBox("Positioning")
        layout = QVBoxLayout(group)

        # Move row
        move_row = QHBoxLayout()
        move_row.addWidget(QLabel("Move:"))

        self._move_value = QDoubleSpinBox()
        self._move_value.setRange(0.0, 999999.0)
        self._move_value.setValue(1.0)
        self._move_value.setDecimals(3)
        self._move_value.setAlignment(Qt.AlignRight)
        self._move_value.setFont(QFont("Consolas", 10))
        self._move_value.setMinimumWidth(120)
        move_row.addWidget(self._move_value)

        self._move_unit = QComboBox()
        self._move_unit.addItems(POSITION_UNITS)
        self._move_unit.setCurrentIndex(0)  # rev default
        self._move_unit.setMinimumWidth(80)
        move_row.addWidget(self._move_unit)

        move_row.addStretch()
        layout.addLayout(move_row)

        # Move direction buttons
        move_btn_row = QHBoxLayout()
        self._move_ccw_btn = QPushButton("\u25c4 Move CCW")
        self._move_ccw_btn.clicked.connect(lambda: self._on_move(0))
        move_btn_row.addWidget(self._move_ccw_btn)

        self._move_cw_btn = QPushButton("Move CW \u25ba")
        self._move_cw_btn.clicked.connect(lambda: self._on_move(1))
        move_btn_row.addWidget(self._move_cw_btn)
        layout.addLayout(move_btn_row)

        # GoTo row
        goto_row = QHBoxLayout()
        goto_row.addWidget(QLabel("GoTo:"))

        self._goto_value = QDoubleSpinBox()
        self._goto_value.setRange(-999999.0, 999999.0)
        self._goto_value.setValue(0.0)
        self._goto_value.setDecimals(3)
        self._goto_value.setAlignment(Qt.AlignRight)
        self._goto_value.setFont(QFont("Consolas", 10))
        self._goto_value.setMinimumWidth(120)
        goto_row.addWidget(self._goto_value)

        self._goto_unit = QComboBox()
        self._goto_unit.addItems(POSITION_UNITS)
        self._goto_unit.setCurrentIndex(0)  # rev default
        self._goto_unit.setMinimumWidth(80)
        goto_row.addWidget(self._goto_unit)

        self._goto_btn = QPushButton("Go")
        self._goto_btn.setFixedWidth(60)
        self._goto_btn.clicked.connect(self._on_goto)
        goto_row.addWidget(self._goto_btn)

        goto_row.addStretch()
        layout.addLayout(goto_row)

        return group

    # =========================================================================
    # Configuration + Motion Profile
    # =========================================================================

    def _create_config_group(self) -> QGroupBox:
        group = QGroupBox("Configuration")
        main = QVBoxLayout(group)

        grid = QGridLayout()

        # Max Speed
        grid.addWidget(QLabel("Max Speed:"), 0, 0)
        self._cfg_maxspd = QDoubleSpinBox()
        self._cfg_maxspd.setRange(0.01, 99999.0)
        self._cfg_maxspd.setValue(120.0)
        self._cfg_maxspd.setDecimals(2)
        self._cfg_maxspd.setAlignment(Qt.AlignRight)
        self._cfg_maxspd.setFont(QFont("Consolas", 10))
        grid.addWidget(self._cfg_maxspd, 0, 1)

        self._cfg_maxspd_unit = QComboBox()
        self._cfg_maxspd_unit.addItems(SPEED_UNITS)
        self._cfg_maxspd_unit.setCurrentIndex(0)  # RPM
        grid.addWidget(self._cfg_maxspd_unit, 0, 2)

        # Acceleration
        grid.addWidget(QLabel("Accel:"), 1, 0)
        self._cfg_accel = QDoubleSpinBox()
        self._cfg_accel.setRange(0.01, 99999.0)
        self._cfg_accel.setValue(5.0)
        self._cfg_accel.setDecimals(2)
        self._cfg_accel.setAlignment(Qt.AlignRight)
        self._cfg_accel.setFont(QFont("Consolas", 10))
        grid.addWidget(self._cfg_accel, 1, 1)

        self._cfg_accel_unit = QComboBox()
        self._cfg_accel_unit.addItems(ACCEL_UNITS)
        self._cfg_accel_unit.setCurrentIndex(0)  # rev/s^2
        grid.addWidget(self._cfg_accel_unit, 1, 2)

        # Deceleration
        grid.addWidget(QLabel("Decel:"), 2, 0)
        self._cfg_decel = QDoubleSpinBox()
        self._cfg_decel.setRange(0.01, 99999.0)
        self._cfg_decel.setValue(5.0)
        self._cfg_decel.setDecimals(2)
        self._cfg_decel.setAlignment(Qt.AlignRight)
        self._cfg_decel.setFont(QFont("Consolas", 10))
        grid.addWidget(self._cfg_decel, 2, 1)

        self._cfg_decel_unit = QComboBox()
        self._cfg_decel_unit.addItems(ACCEL_UNITS)
        self._cfg_decel_unit.setCurrentIndex(0)  # rev/s^2
        grid.addWidget(self._cfg_decel_unit, 2, 2)

        # Apply button
        self._cfg_apply_btn = QPushButton("Apply")
        self._cfg_apply_btn.setToolTip(
            "Send motion parameters to driver (uses MOT:CFG:* SCPI commands)")
        self._cfg_apply_btn.clicked.connect(self._on_config_apply)
        grid.addWidget(self._cfg_apply_btn, 3, 1, 1, 2)

        main.addLayout(grid)

        # ── Motion profile preview graph ──
        self._profile_plot = pg.PlotWidget()
        self._profile_plot.setFixedHeight(130)
        self._profile_plot.setMouseEnabled(x=False, y=False)
        self._profile_plot.hideButtons()
        self._profile_plot.setMenuEnabled(False)
        self._profile_plot.setBackground('#1e1e1e')

        pi = self._profile_plot.getPlotItem()
        pi.showAxis('left')
        pi.getAxis('left').setWidth(50)
        pi.getAxis('left').setStyle(tickFont=QFont("Consolas", 7))
        pi.getAxis('left').setPen('#666')
        pi.showAxis('bottom')
        pi.getAxis('bottom').setStyle(tickFont=QFont("Consolas", 7))
        pi.getAxis('bottom').setPen('#666')
        self._profile_plot.setLabel('left', 'speed',
                                    **{'font-size': '8pt', 'color': '#888'})
        self._profile_plot.setLabel('bottom', 'time (s)',
                                    **{'font-size': '8pt', 'color': '#888'})

        # Speed curve
        self._profile_curve = self._profile_plot.plot(
            pen=pg.mkPen(color='#4a9eff', width=2))
        self._profile_fill = self._profile_plot.plot(
            pen=pg.mkPen(None), brush=pg.mkBrush(74, 158, 255, 40),
            fillLevel=0)

        # Phase labels
        self._phase_labels = []
        for text in ("ACC", "CRUISE", "DEC"):
            lbl = pg.TextItem(text, color='#aaa', anchor=(0.5, 1))
            lbl.setFont(QFont("Segoe UI", 8, QFont.Bold))
            self._profile_plot.addItem(lbl)
            self._phase_labels.append(lbl)

        # Phase separators
        self._phase_seps = []
        for _ in range(2):
            sep = pg.InfiniteLine(pos=0, angle=90,
                                  pen=pg.mkPen('#444', width=1,
                                               style=Qt.DotLine))
            self._profile_plot.addItem(sep)
            self._phase_seps.append(sep)

        # Unit label (updated when units change)
        self._profile_unit_label = pg.TextItem("", color='#666',
                                               anchor=(1, 0))
        self._profile_unit_label.setFont(QFont("Consolas", 8))
        self._profile_plot.addItem(self._profile_unit_label)

        main.addWidget(self._profile_plot)

        # Connect spinbox changes to profile update
        self._cfg_maxspd.valueChanged.connect(self._update_profile)
        self._cfg_accel.valueChanged.connect(self._update_profile)
        self._cfg_decel.valueChanged.connect(self._update_profile)
        self._cfg_maxspd_unit.currentIndexChanged.connect(self._update_profile)
        self._cfg_accel_unit.currentIndexChanged.connect(self._update_profile)
        self._cfg_decel_unit.currentIndexChanged.connect(self._update_profile)
        self._update_profile()

        return group

    def _update_profile(self, _=None):
        """Redraw the trapezoidal motion profile from current config values."""
        # Get values in physical units (steps/s, steps/s^2)
        maxspd = self._speed_to_steps_per_sec(
            self._cfg_maxspd.value(), self._cfg_maxspd_unit.currentIndex())
        acc = self._accel_to_steps_per_sec_sq(
            self._cfg_accel.value(), self._cfg_accel_unit.currentIndex())
        dec = self._accel_to_steps_per_sec_sq(
            self._cfg_decel.value(), self._cfg_decel_unit.currentIndex())

        acc = max(acc, 1)
        dec = max(dec, 1)
        maxspd = max(maxspd, 1)

        # Convert to display units for Y axis
        spd_unit_idx = self._cfg_maxspd_unit.currentIndex()
        display_maxspd = self._cfg_maxspd.value()

        # Compute times in seconds
        t_acc = maxspd / acc
        t_cruise = max(t_acc, 0.3)
        t_dec = maxspd / dec

        x = [0, t_acc, t_acc + t_cruise, t_acc + t_cruise + t_dec]
        y = [0, display_maxspd, display_maxspd, 0]

        self._profile_curve.setData(x, y)
        self._profile_fill.setData(x, y)

        # Update Y range
        y_max = display_maxspd * 1.15 if display_maxspd > 0 else 100
        self._profile_plot.setYRange(0, y_max)
        self._profile_plot.setXRange(0, x[-1] * 1.05 if x[-1] > 0 else 1)

        # Phase labels
        label_y = display_maxspd * 1.08
        self._phase_labels[0].setPos(t_acc / 2, label_y)  # ACC
        self._phase_labels[1].setPos(t_acc + t_cruise / 2, label_y)  # CRUISE
        self._phase_labels[2].setPos(
            t_acc + t_cruise + t_dec / 2, label_y)  # DEC

        # Phase separators
        self._phase_seps[0].setValue(t_acc)
        self._phase_seps[1].setValue(t_acc + t_cruise)

        # Update Y axis label with current unit
        unit_name = SPEED_UNITS[spd_unit_idx]
        self._profile_plot.setLabel('left', unit_name,
                                    **{'font-size': '8pt', 'color': '#888'})

        # Summary text in top-right
        total_time = x[-1]
        self._profile_unit_label.setText(
            f"{display_maxspd:.1f} {unit_name} peak, {total_time:.2f}s total")
        self._profile_unit_label.setPos(x[-1] * 0.98, y_max * 0.95)

    # =========================================================================
    # Speed Trim Tuning
    # =========================================================================

    def _create_trim_group(self) -> QGroupBox:
        group = QGroupBox("Speed Trim (Trim mode)")
        group.setEnabled(False)  # Disabled until Trim mode is active
        self._pid_group = group
        main = QVBoxLayout(group)

        grid = QGridLayout()

        # Kp
        grid.addWidget(QLabel("Kp:"), 0, 0)
        self._pid_kp = QDoubleSpinBox()
        self._pid_kp.setRange(0.0, 100.0)
        self._pid_kp.setValue(1.0)
        self._pid_kp.setDecimals(2)
        self._pid_kp.setSingleStep(0.1)
        self._pid_kp.setAlignment(Qt.AlignRight)
        self._pid_kp.setFont(QFont("Consolas", 10))
        grid.addWidget(self._pid_kp, 0, 1)

        # Ki
        grid.addWidget(QLabel("Ki:"), 0, 2)
        self._pid_ki = QDoubleSpinBox()
        self._pid_ki.setRange(0.0, 100.0)
        self._pid_ki.setValue(0.1)
        self._pid_ki.setDecimals(2)
        self._pid_ki.setSingleStep(0.01)
        self._pid_ki.setAlignment(Qt.AlignRight)
        self._pid_ki.setFont(QFont("Consolas", 10))
        grid.addWidget(self._pid_ki, 0, 3)

        # Trim max % (replaces Kd)
        grid.addWidget(QLabel("Max %:"), 0, 4)
        self._trim_max_pct = QSpinBox()
        self._trim_max_pct.setRange(1, 50)
        self._trim_max_pct.setValue(8)
        self._trim_max_pct.setSuffix(" %")
        self._trim_max_pct.setToolTip("Max trim as percentage of base speed (1-50%)")
        self._trim_max_pct.setAlignment(Qt.AlignRight)
        self._trim_max_pct.setFont(QFont("Consolas", 10))
        grid.addWidget(self._trim_max_pct, 0, 5)

        # Output limit
        grid.addWidget(QLabel("Out Limit:"), 1, 0)
        self._pid_out_limit = QSpinBox()
        self._pid_out_limit.setRange(1, 50000)
        self._pid_out_limit.setValue(500)
        self._pid_out_limit.setSuffix(" tps")
        self._pid_out_limit.setToolTip("Max trim output in encoder ticks/sec")
        self._pid_out_limit.setAlignment(Qt.AlignRight)
        self._pid_out_limit.setFont(QFont("Consolas", 10))
        grid.addWidget(self._pid_out_limit, 1, 1, 1, 2)

        # Integral limit
        grid.addWidget(QLabel("I Limit:"), 1, 3)
        self._pid_i_limit = QSpinBox()
        self._pid_i_limit.setRange(0, 50000)
        self._pid_i_limit.setValue(0)
        self._pid_i_limit.setToolTip("0 = use output limit")
        self._pid_i_limit.setSuffix(" tps")
        self._pid_i_limit.setAlignment(Qt.AlignRight)
        self._pid_i_limit.setFont(QFont("Consolas", 10))
        grid.addWidget(self._pid_i_limit, 1, 4, 1, 2)

        main.addLayout(grid)

        # Buttons row
        btn_row = QHBoxLayout()

        self._pid_apply_btn = QPushButton("Apply")
        self._pid_apply_btn.setToolTip("Send trim gains to firmware (CTRL:TRIM:GAINS)")
        self._pid_apply_btn.clicked.connect(self._on_trim_apply)
        btn_row.addWidget(self._pid_apply_btn)

        self._pid_save_btn = QPushButton("Save to Flash")
        self._pid_save_btn.setToolTip("Persist gains to MCU flash (CTRL:TRIM:SAVE)")
        self._pid_save_btn.clicked.connect(self._on_trim_save)
        btn_row.addWidget(self._pid_save_btn)

        self._pid_reset_btn = QPushButton("Reset Integrator")
        self._pid_reset_btn.setToolTip("Clear trim integrator (CTRL:TRIM:RESET)")
        self._pid_reset_btn.clicked.connect(self._on_trim_reset)
        btn_row.addWidget(self._pid_reset_btn)

        main.addLayout(btn_row)

        # Live trim readback label
        self._pid_readback = QLabel("Trim: --- (inactive)")
        self._pid_readback.setFont(QFont("Consolas", 9))
        self._pid_readback.setStyleSheet("color: #aaa;")
        main.addWidget(self._pid_readback)

        return group

    # =========================================================================
    # Live Readback
    # =========================================================================

    def _create_readback_group(self) -> QGroupBox:
        group = QGroupBox("Live Readback")
        grid = QGridLayout(group)
        mono = QFont("Consolas", 10)

        grid.addWidget(QLabel("Position:"), 0, 0)
        self._rb_position = QLabel("---")
        self._rb_position.setFont(mono)
        grid.addWidget(self._rb_position, 0, 1)

        grid.addWidget(QLabel("Speed:"), 1, 0)
        self._rb_speed = QLabel("---")
        self._rb_speed.setFont(mono)
        grid.addWidget(self._rb_speed, 1, 1)

        grid.addWidget(QLabel("Following:"), 2, 0)
        self._rb_following = QLabel("---")
        self._rb_following.setFont(mono)
        grid.addWidget(self._rb_following, 2, 1)

        grid.addWidget(QLabel("Mode:"), 3, 0)
        self._rb_mode = QLabel("OPEN_LOOP")
        self._rb_mode.setFont(mono)
        grid.addWidget(self._rb_mode, 3, 1)

        grid.addWidget(QLabel("Supervisor:"), 4, 0)
        self._rb_supervisor = QLabel("IDLE / Tier 0")
        self._rb_supervisor.setFont(mono)
        self._rb_supervisor.setStyleSheet("color: #888;")
        grid.addWidget(self._rb_supervisor, 4, 1)

        return group

    # =========================================================================
    # Timing
    # =========================================================================

    def _create_timing_group(self) -> QGroupBox:
        group = QGroupBox("Timing")
        layout = QHBoxLayout(group)

        layout.addWidget(QLabel("Transport Delay:"))

        self._delay_spin = QSpinBox()
        self._delay_spin.setRange(0, 1000)
        self._delay_spin.setValue(0)
        self._delay_spin.setSuffix(" ms")
        self._delay_spin.setAlignment(Qt.AlignRight)
        self._delay_spin.setFont(QFont("Consolas", 10))
        layout.addWidget(self._delay_spin)

        self._delay_apply_btn = QPushButton("Apply")
        self._delay_apply_btn.setToolTip("Set SYST:DELAY on device")
        self._delay_apply_btn.clicked.connect(self._on_delay_apply)
        layout.addWidget(self._delay_apply_btn)

        self._delay_auto_btn = QPushButton("Auto (RTT/2)")
        self._delay_auto_btn.setToolTip(
            "Set delay to half of measured round-trip time")
        self._delay_auto_btn.clicked.connect(self._on_delay_auto)
        layout.addWidget(self._delay_auto_btn)

        self._rtt_label = QLabel("RTT: --- ms")
        self._rtt_label.setFont(QFont("Consolas", 9))
        self._rtt_label.setStyleSheet("color: #888;")
        layout.addWidget(self._rtt_label)

        layout.addStretch()
        return group

    # =========================================================================
    # Command handlers
    # =========================================================================

    def _on_run(self, direction: int):
        speed_sps = self._speed_to_steps_per_sec(
            self._run_speed.value(), self._run_unit.currentIndex())
        log.info("RUN: %.2f %s -> %d steps/s (full_steps/rev=%d, dir=%d)",
                 self._run_speed.value(),
                 SPEED_UNITS[self._run_unit.currentIndex()],
                 speed_sps, self._full_steps_per_rev, direction)
        if speed_sps <= 0:
            return
        self.command_requested.emit(f"RUN {speed_sps} {direction}")

    @Slot()
    def _on_stop(self):
        self.command_requested.emit("STOP")

    def _on_move(self, direction: int):
        usteps = self._position_to_microsteps(
            self._move_value.value(), self._move_unit.currentIndex())
        if usteps <= 0:
            return
        speed_sps = self._speed_to_steps_per_sec(
            self._run_speed.value(), self._run_unit.currentIndex())
        if speed_sps > 0:
            self.command_requested.emit(
                f"MOVE {usteps} {direction} {speed_sps}")
        else:
            self.command_requested.emit(f"MOVE {usteps} {direction}")

    @Slot()
    def _on_goto(self):
        usteps = self._position_to_microsteps(
            self._goto_value.value(), self._goto_unit.currentIndex())
        speed_sps = self._speed_to_steps_per_sec(
            self._run_speed.value(), self._run_unit.currentIndex())
        if speed_sps > 0:
            self.command_requested.emit(f"GOTO {usteps} {speed_sps}")
        else:
            self.command_requested.emit(f"GOTO {usteps}")

    @Slot()
    def _on_config_apply(self):
        """Send MOT:CFG commands for accel, decel, max speed."""
        acc_sps2 = self._accel_to_steps_per_sec_sq(
            self._cfg_accel.value(), self._cfg_accel_unit.currentIndex())
        dec_sps2 = self._accel_to_steps_per_sec_sq(
            self._cfg_decel.value(), self._cfg_decel_unit.currentIndex())
        maxspd_sps = self._speed_to_steps_per_sec(
            self._cfg_maxspd.value(), self._cfg_maxspd_unit.currentIndex())

        # Use MOT:CFG:* commands which accept physical units (steps/s, steps/s^2)
        self.command_requested.emit(f"MOT:CFG:ACCEL {acc_sps2}")
        self.command_requested.emit(f"MOT:CFG:DECEL {dec_sps2}")
        self.command_requested.emit(f"MOT:CFG:MAXSPD {maxspd_sps}")

    @Slot()
    def _on_delay_apply(self):
        ms = self._delay_spin.value()
        self.command_requested.emit(f"SYST:DELAY {ms}")

    @Slot()
    def _on_delay_auto(self):
        """Set transport delay to RTT/2 from heartbeat stats."""
        if self._last_avg_rtt_ms > 0:
            half_rtt = max(1, round(self._last_avg_rtt_ms / 2.0))
            self._delay_spin.setValue(half_rtt)
            self._on_delay_apply()

    # =========================================================================
    # Mode change + Trim command handlers
    # =========================================================================

    def _on_mode_changed(self, index: int):
        """Send CTRL:MODE command when user changes mode selector."""
        modes = ["OPEN_LOOP", "CLOSED_LOOP_MONITOR", "SPEED_TRIM"]
        if 0 <= index < len(modes):
            self._mode_change_pending = True  # Block telemetry from reverting combo
            self.command_requested.emit(f"CTRL:MODE {modes[index]}")

    @Slot()
    def _on_trim_apply(self):
        """Send trim gains + limits to firmware."""
        kp = self._pid_kp.value()
        ki = self._pid_ki.value()
        self.command_requested.emit(f"CTRL:TRIM:GAINS {kp:.2f} {ki:.2f}")
        out_limit = self._pid_out_limit.value()
        i_limit = self._pid_i_limit.value()
        self.command_requested.emit(f"CTRL:TRIM:LIMITS {out_limit} {i_limit}")
        max_pct = self._trim_max_pct.value()
        self.command_requested.emit(f"CTRL:TRIM:MAXPCT {max_pct}")

    @Slot()
    def _on_trim_save(self):
        """Persist trim gains to MCU flash."""
        self.command_requested.emit("CTRL:TRIM:SAVE")

    @Slot()
    def _on_trim_reset(self):
        """Reset trim integrator."""
        self.command_requested.emit("CTRL:TRIM:RESET")

    # =========================================================================
    # Data update slots (called from main_window)
    # =========================================================================

    _last_avg_rtt_ms = 0.0

    def update_telemetry(self, data: dict):
        """Update live readback from telemetry polling data."""
        motor = data.get("motor", {})
        if not isinstance(motor, dict):
            return

        # Position (microsteps -> user units)
        position_usteps = motor.get("position", 0)
        pos_rev = self._microsteps_to_position(position_usteps, 0)  # rev
        self._rb_position.setText(
            f"{pos_rev:.3f} rev  ({position_usteps} \u00b5steps)")

        # Speed (steps/s -> user units)
        speed_sps = motor.get("speed", 0)
        speed_rpm = self._steps_per_sec_to_speed(speed_sps, 0)  # RPM
        self._rb_speed.setText(
            f"{speed_rpm:.1f} RPM  ({speed_sps} steps/s)")

        # Control telemetry (following error + trim)
        control = data.get("control", {})
        if isinstance(control, dict):
            follow_ticks = control.get("following_error", 0)
            if self._encoder_ppr > 0:
                follow_rev = follow_ticks / self._encoder_ppr
                self._rb_following.setText(
                    f"{follow_rev:.4f} rev  ({follow_ticks} ticks)")
            else:
                self._rb_following.setText(f"{follow_ticks} ticks")

            # Mode from control telemetry (0=OPEN_LOOP, 1=MONITOR, 2=SPEED_TRIM)
            mode = control.get("mode", 0)
            tracking = control.get("tracking", False)
            state = data.get("state", "")

            mode_names = {0: "OPEN_LOOP", 1: "CLOSED_LOOP_MONITOR",
                          2: "SPEED_TRIM"}
            mode_str = mode_names.get(mode, "OPEN_LOOP")

            # Sync combo to firmware mode (0/1/2 maps directly to combo index)
            combo_idx = min(mode, 2)
            if not self._mode_change_pending:
                self._mode_combo.blockSignals(True)
                self._mode_combo.setCurrentIndex(combo_idx)
                self._mode_combo.blockSignals(False)

            self._rb_mode.setText(
                f"{mode_str} ({state})" if state else mode_str)
            self._flow_diagram.set_mode(mode_str)

            # Enable trim group only in SPEED_TRIM mode
            self._pid_group.setEnabled(mode == 2)

            # Supervisor readback
            sup_state = control.get("sup_state", 0)
            tier = control.get("tier", 0)
            retries = control.get("retries", 0)
            vel_error = control.get("vel_error", 0)
            state_names = ["IDLE", "MOVING", "HOLDING", "RECOVERY", "FAULT"]
            tier_names = ["Observe", "Trim Correct", "Recovery", "Fault"]
            s_name = state_names[sup_state] if sup_state < 5 else "?"
            t_name = tier_names[tier] if tier < 4 else "?"
            sup_text = f"{s_name} / {t_name}"
            if retries > 0:
                sup_text += f" (retry {retries})"
            if vel_error != 0:
                sup_text += f"  vel_err={vel_error}"
            self._rb_supervisor.setText(sup_text)

            # Color supervisor label by state
            if sup_state == 4:  # FAULT
                self._rb_supervisor.setStyleSheet("color: #ff4444; font-weight: bold;")
            elif sup_state == 3:  # RECOVERY
                self._rb_supervisor.setStyleSheet("color: #ffaa00; font-weight: bold;")
            elif tier == 1:  # Trim active
                self._rb_supervisor.setStyleSheet("color: #88ff88;")
            else:
                self._rb_supervisor.setStyleSheet("color: #888;")

            # Trim readback (firmware sends "trim_out" instead of "pid_out")
            trim_out = control.get("trim_out", control.get("pid_out", 0))
            p_term = control.get("p", 0)
            i_term = control.get("i", 0)
            setpoint = control.get("setpoint", 0)
            base_spd = control.get("base_spd", 0)
            trim_spd = control.get("trim_spd", 0)
            final_spd = control.get("final_spd", 0)
            trim_frozen = control.get("trim_frozen", 0)
            vel_quality = control.get("vel_quality", 0)

            vel_q_names = {0: "GOOD", 1: "LOW", 2: "STALE", 3: "INVALID"}
            vel_q_str = vel_q_names.get(vel_quality, "?")

            if tracking:
                frozen_tag = " [FROZEN]" if trim_frozen else ""
                self._pid_readback.setText(
                    f"Trim: P={p_term}  I={i_term}  Out={trim_out} tps  "
                    f"Base={base_spd}  Trim={trim_spd:+d}  "
                    f"Final={final_spd}  Vel:{vel_q_str}{frozen_tag}")
                if trim_frozen:
                    self._pid_readback.setStyleSheet("color: #ffaa00;")
                else:
                    self._pid_readback.setStyleSheet("color: #88ff88;")
            elif mode == 2:
                self._pid_readback.setText("Trim: armed (not tracking)")
                self._pid_readback.setStyleSheet("color: #ffaa00;")
            elif mode == 1:
                self._pid_readback.setText("Trim: N/A (monitor mode)")
                self._pid_readback.setStyleSheet("color: #888;")
            else:
                self._pid_readback.setText("Trim: --- (open loop)")
                self._pid_readback.setStyleSheet("color: #aaa;")
        else:
            # No control section — fallback
            state = data.get("state", "")
            if state:
                self._rb_mode.setText(f"OPEN_LOOP ({state})")

    def update_link_health(self, data: dict):
        """Update RTT display from heartbeat stats."""
        avg_rtt = data.get("avg_rtt_ms", 0.0)
        self._last_avg_rtt_ms = avg_rtt
        if avg_rtt > 0:
            self._rtt_label.setText(f"RTT: {avg_rtt:.1f} ms")
        else:
            self._rtt_label.setText("RTT: --- ms")

    def update_drv_config(self, data: dict):
        """Update motor configuration from DRV:* query responses.

        Called with JSON response data from DRV:STEP_MODE?, DRV:FULL_STEPS?,
        DRV:ENC_PPR?, or SYST:DELAY? queries.
        """
        if "step_mode" in data:
            self._step_mode = data["step_mode"]
            log.info("Step mode updated: %d (1/%d, %d usteps/rev)",
                     self._step_mode, 1 << self._step_mode,
                     self._microsteps_per_rev)

        if "full_steps_per_rev" in data:
            self._full_steps_per_rev = data["full_steps_per_rev"]
            log.info("Full steps/rev: %d (usteps/rev: %d)",
                     self._full_steps_per_rev, self._microsteps_per_rev)

        if "microsteps_per_rev" in data:
            # Cross-check: if provided, verify against computed value
            reported = data["microsteps_per_rev"]
            computed = self._microsteps_per_rev
            if reported != computed:
                log.warning("microsteps_per_rev mismatch: "
                            "reported=%d computed=%d", reported, computed)

        if "encoder_ppr" in data:
            self._encoder_ppr = data["encoder_ppr"]
            log.info("Encoder PPR: %d", self._encoder_ppr)

        if "transport_delay_ms" in data:
            self._transport_delay_ms = data["transport_delay_ms"]
            self._delay_spin.setValue(self._transport_delay_ms)
            log.info("Transport delay: %d ms", self._transport_delay_ms)

        # Update config info label
        self._config_label.setText(
            f"{self._full_steps_per_rev} steps/rev "
            f"\u00d7 1/{1 << self._step_mode} = "
            f"{self._microsteps_per_rev} \u00b5steps/rev")

        # Re-draw the motion profile with updated conversion factors
        self._update_profile()

    def update_pid_config(self, data: dict):
        """Update trim tuning controls from CTRL:TRIM? response.

        Firmware returns gains as string values ("kp":"1.50") — parse with float().
        Also supports legacy kp_100 integer keys for backward compatibility.
        """
        if "kp" in data:
            self._pid_kp.setValue(float(data["kp"]))
        elif "kp_100" in data:
            self._pid_kp.setValue(data["kp_100"] / 100.0)
        if "ki" in data:
            self._pid_ki.setValue(float(data["ki"]))
        elif "ki_100" in data:
            self._pid_ki.setValue(data["ki_100"] / 100.0)
        if "max_pct" in data:
            self._trim_max_pct.setValue(int(data["max_pct"]))
        if "out_limit" in data:
            self._pid_out_limit.setValue(int(data["out_limit"]))
        if "i_limit" in data:
            self._pid_i_limit.setValue(int(data["i_limit"]))
        log.info("Trim config: Kp=%.2f Ki=%.2f MaxPct=%d outLim=%d iLim=%d",
                 self._pid_kp.value(), self._pid_ki.value(),
                 self._trim_max_pct.value(), self._pid_out_limit.value(),
                 self._pid_i_limit.value())

    def update_mode_response(self, success: bool, data: dict):
        """Handle response from CTRL:MODE command.

        If mode change failed (e.g., no encoder), revert the combo.
        """
        self._mode_change_pending = False  # Allow telemetry to sync combo again
        if not success:
            # Revert combo to Open Loop
            self._mode_combo.blockSignals(True)
            self._mode_combo.setCurrentIndex(0)
            self._mode_combo.blockSignals(False)
            self._flow_diagram.set_mode("OPEN_LOOP")

    def update_mconfig(self, response: str):
        """Parse motor config from MCONFIG raw text response.

        This fires early during the connect refresh cycle, giving us the
        step_mode before the user interacts with the panel. The MCONFIG
        response includes lines like:
            StepMode: 1 (1/2)
            ACC=15 DEC=15 MAXSPD=79
        """
        # Parse step mode
        sm = re.search(r'StepMode:\s*(\d+)\s*\(1/(\d+)\)', response)
        if sm:
            self._step_mode = int(sm.group(1))
            log.info("MCONFIG step_mode=%d (1/%s), usteps/rev=%d",
                     self._step_mode, sm.group(2), self._microsteps_per_rev)

        # Update config info label and profile
        self._config_label.setText(
            f"{self._full_steps_per_rev} steps/rev "
            f"\u00d7 1/{1 << self._step_mode} = "
            f"{self._microsteps_per_rev} \u00b5steps/rev")
        self._update_profile()

    @Slot()
    def clear(self):
        """Reset on disconnect."""
        self._rb_position.setText("---")
        self._rb_speed.setText("---")
        self._rb_following.setText("---")
        self._rb_mode.setText("OPEN_LOOP")
        self._rtt_label.setText("RTT: --- ms")
        self._last_avg_rtt_ms = 0.0
        self._config_label.setText("")
        self._mode_change_pending = False
        self._mode_combo.blockSignals(True)
        self._mode_combo.setCurrentIndex(0)
        self._mode_combo.blockSignals(False)
        self._flow_diagram.set_mode("OPEN_LOOP")
        self._pid_readback.setText("Trim: --- (inactive)")
        self._pid_readback.setStyleSheet("color: #aaa;")
        self._pid_group.setEnabled(False)
        self._rb_supervisor.setText("IDLE / Tier 0")
        self._rb_supervisor.setStyleSheet("color: #888;")
