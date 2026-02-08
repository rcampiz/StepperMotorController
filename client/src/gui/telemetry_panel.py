"""
Telemetry panel widget.

Displays motor and encoder telemetry data from the device.
"""

from PySide6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QGridLayout,
    QLabel,
    QGroupBox,
    QFrame,
    QProgressBar,
    QPushButton,
    QSpinBox,
)
from PySide6.QtCore import Qt, Slot, Signal
from PySide6.QtGui import QFont, QValidator
import re


class HexSpinBox(QSpinBox):
    """QSpinBox that displays values in hex (0x00-0xFF)."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setRange(0, 255)
        self.setPrefix("0x")

    def textFromValue(self, value: int) -> str:
        return f"{value:02X}"

    def valueFromText(self, text: str) -> int:
        clean = text.replace("0x", "").replace("0X", "")
        return int(clean, 16)

    def validate(self, text: str, pos: int):
        clean = text.replace("0x", "").replace("0X", "")
        if not clean:
            return (QValidator.State.Intermediate, text, pos)
        try:
            val = int(clean, 16)
            if 0 <= val <= 255:
                return (QValidator.State.Acceptable, text, pos)
        except ValueError:
            pass
        return (QValidator.State.Invalid, text, pos)


class TelemetryPanel(QWidget):
    """Panel for displaying telemetry data."""

    # Signal to request parameter refresh
    refresh_requested = Signal()
    # Signal to apply KVAL values (emits MCONFIG_KVAL command string)
    kval_apply_requested = Signal(str)
    # Signal to apply motion params (emits list of command strings)
    param_apply_requested = Signal(list)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._setup_ui()

    def _setup_ui(self):
        """Set up the user interface."""
        layout = QVBoxLayout(self)
        layout.setSpacing(12)

        # Motor status group
        layout.addWidget(self._create_motor_group())

        # Encoder group
        layout.addWidget(self._create_encoder_group())

        # Status indicators group
        layout.addWidget(self._create_status_group())

        # Link health group
        layout.addWidget(self._create_link_health_group())

        # Driver parameters group
        layout.addWidget(self._create_driver_params_group())

        # Motor config (flash) group
        layout.addWidget(self._create_motor_config_group())

        # Raw data group (collapsible)
        layout.addWidget(self._create_raw_group())

        layout.addStretch()

    def _create_motor_group(self) -> QGroupBox:
        """Create motor telemetry group."""
        group = QGroupBox("Motor")
        layout = QGridLayout(group)

        # Position
        layout.addWidget(QLabel("Position:"), 0, 0)
        self._position_label = QLabel("---")
        self._position_label.setFont(QFont("Consolas", 12, QFont.Bold))
        self._position_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(self._position_label, 0, 1)
        layout.addWidget(QLabel("steps"), 0, 2)

        # Speed
        layout.addWidget(QLabel("Speed:"), 1, 0)
        self._speed_label = QLabel("---")
        self._speed_label.setFont(QFont("Consolas", 12, QFont.Bold))
        self._speed_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(self._speed_label, 1, 1)
        layout.addWidget(QLabel("steps/s"), 1, 2)

        # Speed bar
        self._speed_bar = QProgressBar()
        self._speed_bar.setRange(0, 100000)  # Visual range for usability
        self._speed_bar.setValue(0)
        self._speed_bar.setTextVisible(False)
        layout.addWidget(self._speed_bar, 2, 0, 1, 3)

        # Set column stretch
        layout.setColumnStretch(1, 1)

        return group

    def _create_encoder_group(self) -> QGroupBox:
        """Create encoder telemetry group."""
        group = QGroupBox("Encoder")
        layout = QGridLayout(group)

        # Count
        layout.addWidget(QLabel("Count:"), 0, 0)
        self._encoder_count_label = QLabel("---")
        self._encoder_count_label.setFont(QFont("Consolas", 12, QFont.Bold))
        self._encoder_count_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(self._encoder_count_label, 0, 1)
        layout.addWidget(QLabel("ticks"), 0, 2)

        # Velocity
        layout.addWidget(QLabel("Velocity:"), 1, 0)
        self._encoder_velocity_label = QLabel("---")
        self._encoder_velocity_label.setFont(QFont("Consolas", 12, QFont.Bold))
        self._encoder_velocity_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(self._encoder_velocity_label, 1, 1)
        layout.addWidget(QLabel("ticks/s"), 1, 2)

        # Index seen
        layout.addWidget(QLabel("Index:"), 2, 0)
        self._index_indicator = QLabel("NOT SEEN")
        self._index_indicator.setAlignment(Qt.AlignCenter)
        self._index_indicator.setStyleSheet(
            "background-color: #444; color: gray; padding: 2px 8px; border-radius: 3px;"
        )
        layout.addWidget(self._index_indicator, 2, 1, 1, 2)

        # Set column stretch
        layout.setColumnStretch(1, 1)

        return group

    def _create_status_group(self) -> QGroupBox:
        """Create status indicators group."""
        group = QGroupBox("Status")
        layout = QHBoxLayout(group)

        # Busy indicator
        self._busy_indicator = self._create_indicator("BUSY")
        layout.addWidget(self._busy_indicator)

        # Hi-Z indicator
        self._hiz_indicator = self._create_indicator("HI-Z")
        layout.addWidget(self._hiz_indicator)

        # Direction indicator
        self._dir_indicator = self._create_indicator("DIR")
        layout.addWidget(self._dir_indicator)

        # Error indicator
        self._error_indicator = self._create_indicator("ERROR")
        layout.addWidget(self._error_indicator)

        layout.addStretch()

        return group

    def _create_indicator(self, text: str) -> QLabel:
        """Create a status indicator label."""
        label = QLabel(text)
        label.setAlignment(Qt.AlignCenter)
        label.setFixedWidth(60)
        label.setStyleSheet(
            "background-color: #333; color: #666; "
            "padding: 4px 8px; border-radius: 4px; font-weight: bold;"
        )
        return label

    def _create_link_health_group(self) -> QGroupBox:
        """Create link health display group."""
        group = QGroupBox("Link Health")
        layout = QHBoxLayout(group)

        # LINK indicator
        self._link_indicator = self._create_indicator("LINK")
        layout.addWidget(self._link_indicator)

        # RTT display
        rtt_layout = QVBoxLayout()
        rtt_header = QLabel("RTT")
        rtt_header.setAlignment(Qt.AlignCenter)
        rtt_header.setStyleSheet("font-size: 9px; color: #888;")
        rtt_layout.addWidget(rtt_header)
        self._rtt_label = QLabel("-- ms")
        self._rtt_label.setAlignment(Qt.AlignCenter)
        self._rtt_label.setFont(QFont("Consolas", 10, QFont.Bold))
        rtt_layout.addWidget(self._rtt_label)
        layout.addLayout(rtt_layout)

        # Heartbeat rate display
        rate_layout = QVBoxLayout()
        rate_header = QLabel("Rate")
        rate_header.setAlignment(Qt.AlignCenter)
        rate_header.setStyleSheet("font-size: 9px; color: #888;")
        rate_layout.addWidget(rate_header)
        self._hb_rate_label = QLabel("-- Hz")
        self._hb_rate_label.setAlignment(Qt.AlignCenter)
        self._hb_rate_label.setFont(QFont("Consolas", 10))
        rate_layout.addWidget(self._hb_rate_label)
        layout.addLayout(rate_layout)

        # Missed count
        missed_layout = QVBoxLayout()
        missed_header = QLabel("Missed")
        missed_header.setAlignment(Qt.AlignCenter)
        missed_header.setStyleSheet("font-size: 9px; color: #888;")
        missed_layout.addWidget(missed_header)
        self._missed_label = QLabel("0")
        self._missed_label.setAlignment(Qt.AlignCenter)
        self._missed_label.setFont(QFont("Consolas", 10))
        missed_layout.addWidget(self._missed_label)
        layout.addLayout(missed_layout)

        # Watchdog remaining
        wd_layout = QVBoxLayout()
        wd_header = QLabel("Watchdog")
        wd_header.setAlignment(Qt.AlignCenter)
        wd_header.setStyleSheet("font-size: 9px; color: #888;")
        wd_layout.addWidget(wd_header)
        self._watchdog_label = QLabel("OFF")
        self._watchdog_label.setAlignment(Qt.AlignCenter)
        self._watchdog_label.setFont(QFont("Consolas", 10))
        wd_layout.addWidget(self._watchdog_label)
        layout.addLayout(wd_layout)

        layout.addStretch()
        return group

    @Slot(dict)
    def update_link_health(self, data: dict):
        """Update link health display from heartbeat ACK data."""
        rtt = data.get('avg_rtt_ms', 0)
        self._rtt_label.setText(f"{rtt:.1f} ms")

        missed = data.get('missed', 0)
        self._missed_label.setText(str(missed))

        remaining = data.get('remaining_ms', 0)
        if remaining > 0:
            self._watchdog_label.setText(f"{remaining} ms")
        else:
            self._watchdog_label.setText("OFF")

        interval_ms = data.get('interval_ms', 50)
        if interval_ms > 0:
            self._hb_rate_label.setText(f"{1000.0 / interval_ms:.0f} Hz")

        # LINK indicator color based on health
        if missed > 5 or rtt > 200:
            self._set_indicator_active(self._link_indicator, True, "#ff0000")
        elif missed > 0 or rtt > 50:
            self._set_indicator_active(self._link_indicator, True, "#ffaa00")
        elif data.get('acked', 0) > 0:
            self._set_indicator_active(self._link_indicator, True, "#00aa00")
        else:
            self._set_indicator_active(self._link_indicator, False)

    def _create_driver_params_group(self) -> QGroupBox:
        """Create driver parameters group with editable spinboxes."""
        group = QGroupBox("Driver Parameters (powerSTEP01)")
        layout = QGridLayout(group)

        # KVAL row (hex spinboxes 0x00-0xFF)
        layout.addWidget(QLabel("KVAL:"), 0, 0)

        layout.addWidget(QLabel("H:"), 0, 1)
        self._kval_hold_spin = HexSpinBox()
        self._kval_hold_spin.setToolTip("KVAL_HOLD - Holding current")
        layout.addWidget(self._kval_hold_spin, 0, 2)

        layout.addWidget(QLabel("R:"), 0, 3)
        self._kval_run_spin = HexSpinBox()
        self._kval_run_spin.setToolTip("KVAL_RUN - Running current")
        layout.addWidget(self._kval_run_spin, 0, 4)

        layout.addWidget(QLabel("A:"), 0, 5)
        self._kval_acc_spin = HexSpinBox()
        self._kval_acc_spin.setToolTip("KVAL_ACC - Acceleration current")
        layout.addWidget(self._kval_acc_spin, 0, 6)

        layout.addWidget(QLabel("D:"), 0, 7)
        self._kval_dec_spin = HexSpinBox()
        self._kval_dec_spin.setToolTip("KVAL_DEC - Deceleration current")
        layout.addWidget(self._kval_dec_spin, 0, 8)

        self._apply_kval_btn = QPushButton("Apply")
        self._apply_kval_btn.setToolTip("Send KVAL values to driver")
        self._apply_kval_btn.clicked.connect(self._on_apply_kval)
        layout.addWidget(self._apply_kval_btn, 0, 9)

        # Motion parameters row (decimal spinboxes)
        layout.addWidget(QLabel("Motion:"), 1, 0)

        layout.addWidget(QLabel("ACC:"), 1, 1)
        self._acc_spin = QSpinBox()
        self._acc_spin.setRange(1, 4095)
        self._acc_spin.setToolTip("Acceleration register value")
        layout.addWidget(self._acc_spin, 1, 2)

        layout.addWidget(QLabel("DEC:"), 1, 3)
        self._dec_spin = QSpinBox()
        self._dec_spin.setRange(1, 4095)
        self._dec_spin.setToolTip("Deceleration register value")
        layout.addWidget(self._dec_spin, 1, 4)

        layout.addWidget(QLabel("MAX:"), 1, 5)
        self._maxspd_spin = QSpinBox()
        self._maxspd_spin.setRange(1, 1023)
        self._maxspd_spin.setToolTip("MAX_SPEED register value")
        layout.addWidget(self._maxspd_spin, 1, 6)

        self._apply_params_btn = QPushButton("Apply")
        self._apply_params_btn.setToolTip("Send motion parameters to driver")
        self._apply_params_btn.clicked.connect(self._on_apply_params)
        layout.addWidget(self._apply_params_btn, 1, 7)

        # Refresh button
        self._refresh_btn = QPushButton("Refresh")
        self._refresh_btn.setToolTip("Read current driver parameters")
        self._refresh_btn.clicked.connect(self._on_refresh_clicked)
        layout.addWidget(self._refresh_btn, 1, 9)

        return group

    @Slot()
    def _on_apply_kval(self):
        """Apply KVAL values to driver."""
        h = self._kval_hold_spin.value()
        r = self._kval_run_spin.value()
        a = self._kval_acc_spin.value()
        d = self._kval_dec_spin.value()
        self.kval_apply_requested.emit(
            f"MCONFIG_KVAL {h:02X} {r:02X} {a:02X} {d:02X}")

    @Slot()
    def _on_apply_params(self):
        """Apply motion parameters to driver."""
        commands = [
            f"ACCEL {self._acc_spin.value()}",
            f"DECEL {self._dec_spin.value()}",
            f"MAXSPD {self._maxspd_spin.value()}",
        ]
        self.param_apply_requested.emit(commands)

    @Slot()
    def _on_refresh_clicked(self):
        """Handle refresh button click."""
        self.refresh_requested.emit()

    def update_driver_params(self, response: str):
        """Parse and update driver parameters from MOTOR_DEBUG response."""
        # Parse response like:
        # STATUS=7E03 (HiZ Idle)
        # KVAL: HOLD=20 RUN=30 ACC=40 DEC=40
        # ACC=138 DEC=138 MAXSPD=65 POS=0

        # Parse KVAL values (hex) → populate spinboxes
        kval_match = re.search(
            r'KVAL:\s*HOLD=([0-9A-Fa-f]+)\s*RUN=([0-9A-Fa-f]+)'
            r'\s*ACC=([0-9A-Fa-f]+)\s*DEC=([0-9A-Fa-f]+)', response)
        if kval_match:
            self._kval_hold_spin.setValue(int(kval_match.group(1), 16))
            self._kval_run_spin.setValue(int(kval_match.group(2), 16))
            self._kval_acc_spin.setValue(int(kval_match.group(3), 16))
            self._kval_dec_spin.setValue(int(kval_match.group(4), 16))

        # Parse motion parameters (decimal) → populate spinboxes
        # Line format: ACC=138 DEC=138 MAXSPD=65 POS=0
        motion_match = re.search(r'ACC=(\d+)\s+DEC=(\d+)\s+MAXSPD=(\d+)', response)
        if motion_match:
            self._acc_spin.setValue(int(motion_match.group(1)))
            self._dec_spin.setValue(int(motion_match.group(2)))
            self._maxspd_spin.setValue(int(motion_match.group(3)))

    def _create_motor_config_group(self) -> QGroupBox:
        """Create motor config (flash) display group."""
        group = QGroupBox("Motor Config (Flash)")
        layout = QVBoxLayout(group)

        self._mconfig_label = QLabel("Not loaded")
        self._mconfig_label.setFont(QFont("Consolas", 9))
        self._mconfig_label.setWordWrap(True)
        self._mconfig_label.setStyleSheet(
            "background-color: #1e1e1e; color: #d4d4d4; "
            "padding: 8px; border-radius: 4px;"
        )
        layout.addWidget(self._mconfig_label)

        return group

    def update_motor_config(self, response: str):
        """Update motor config display from MCONFIG_SHOW response."""
        self._mconfig_label.setText(response.strip())

    def _create_raw_group(self) -> QGroupBox:
        """Create raw data display group."""
        group = QGroupBox("Raw Data")
        layout = QVBoxLayout(group)

        self._raw_label = QLabel("No data")
        self._raw_label.setFont(QFont("Consolas", 9))
        self._raw_label.setWordWrap(True)
        self._raw_label.setStyleSheet(
            "background-color: #1e1e1e; color: #d4d4d4; "
            "padding: 8px; border-radius: 4px;"
        )
        self._raw_label.setMinimumHeight(80)
        layout.addWidget(self._raw_label)

        return group

    def _set_indicator_active(self, indicator: QLabel, active: bool, color: str = "green"):
        """Set indicator active/inactive state."""
        if active:
            indicator.setStyleSheet(
                f"background-color: {color}; color: white; "
                "padding: 4px 8px; border-radius: 4px; font-weight: bold;"
            )
        else:
            indicator.setStyleSheet(
                "background-color: #333; color: #666; "
                "padding: 4px 8px; border-radius: 4px; font-weight: bold;"
            )

    @Slot(dict)
    def update_data(self, data: dict):
        """Update telemetry display with new data."""
        if not data:
            return

        # Motor data
        motor = data.get("motor", {})
        if isinstance(motor, dict):
            pos = motor.get("position", motor.get("pos"))
            if pos is not None:
                self._position_label.setText(f"{pos:,}")

            speed = motor.get("speed", motor.get("spd"))
            if speed is not None:
                self._speed_label.setText(f"{speed:,}")
                self._speed_bar.setValue(min(abs(speed), 100000))

            busy = motor.get("busy")
            if busy is not None:
                self._set_indicator_active(self._busy_indicator, busy, "#ff9900")

            hiz = motor.get("hi_z", motor.get("hiz"))
            if hiz is not None:
                self._set_indicator_active(self._hiz_indicator, hiz, "#ff3333")

            direction = motor.get("direction", motor.get("dir"))
            if direction is not None:
                self._set_indicator_active(self._dir_indicator, direction == 1, "#3399ff")
                self._dir_indicator.setText("CW" if direction == 1 else "CCW")

        # Encoder data
        encoder = data.get("encoder", {})
        if isinstance(encoder, dict):
            count = encoder.get("count", encoder.get("cnt"))
            if count is not None:
                self._encoder_count_label.setText(f"{count:,}")

            velocity = encoder.get("velocity", encoder.get("vel"))
            if velocity is not None:
                self._encoder_velocity_label.setText(f"{velocity:,}")

            index_seen = encoder.get("index_seen", encoder.get("idx"))
            if index_seen is not None:
                if index_seen:
                    self._index_indicator.setText("SEEN")
                    self._index_indicator.setStyleSheet(
                        "background-color: #00aa00; color: white; "
                        "padding: 2px 8px; border-radius: 3px;"
                    )
                else:
                    self._index_indicator.setText("NOT SEEN")
                    self._index_indicator.setStyleSheet(
                        "background-color: #444; color: gray; "
                        "padding: 2px 8px; border-radius: 3px;"
                    )

        # Error status
        error = data.get("error", False)
        self._set_indicator_active(self._error_indicator, error, "#ff0000")

        # Raw data display
        self._update_raw_display(data)

    def _update_raw_display(self, data: dict):
        """Update raw data display."""
        lines = []

        motor = data.get("motor", {})
        if motor:
            lines.append(f"Motor: pos={motor.get('position', '?')} spd={motor.get('speed', '?')}")

        encoder = data.get("encoder", {})
        if encoder:
            lines.append(f"Encoder: cnt={encoder.get('count', '?')} vel={encoder.get('velocity', '?')}")

        tick = data.get("tick")
        if tick is not None:
            lines.append(f"Tick: {tick}")

        self._raw_label.setText("\n".join(lines) if lines else "No data")

    @Slot()
    def clear(self):
        """Clear all telemetry displays."""
        self._position_label.setText("---")
        self._speed_label.setText("---")
        self._speed_bar.setValue(0)
        self._encoder_count_label.setText("---")
        self._encoder_velocity_label.setText("---")

        self._index_indicator.setText("NOT SEEN")
        self._index_indicator.setStyleSheet(
            "background-color: #444; color: gray; padding: 2px 8px; border-radius: 3px;"
        )

        self._set_indicator_active(self._busy_indicator, False)
        self._set_indicator_active(self._hiz_indicator, False)
        self._set_indicator_active(self._error_indicator, False)
        self._dir_indicator.setText("DIR")
        self._set_indicator_active(self._dir_indicator, False)

        # Clear link health
        self._set_indicator_active(self._link_indicator, False)
        self._link_indicator.setText("LINK")
        self._rtt_label.setText("-- ms")
        self._hb_rate_label.setText("-- Hz")
        self._missed_label.setText("0")
        self._watchdog_label.setText("OFF")

        self._raw_label.setText("No data")

    def update_encoder_data(self, raw: str | None, error: str | None = None):
        """Update encoder display from ENC command response.

        Response format: 'count=<n> vel=<n> idx=<0|1> idx_tick=<n>'
        """
        if error or raw is None:
            self._encoder_count_label.setText("N/A")
            self._encoder_velocity_label.setText("N/A")
            self._index_indicator.setText("NOT PRESENT")
            self._index_indicator.setStyleSheet(
                "background-color: #663300; color: #ffaa00; "
                "padding: 2px 8px; border-radius: 3px;"
            )
            return

        count_match = re.search(r'count=(-?\d+)', raw)
        vel_match = re.search(r'vel=(-?\d+)', raw)
        idx_match = re.search(r'idx=([01])', raw)

        if count_match:
            self._encoder_count_label.setText(f"{int(count_match.group(1)):,}")
        if vel_match:
            self._encoder_velocity_label.setText(f"{int(vel_match.group(1)):,}")
        if idx_match:
            if idx_match.group(1) == "1":
                self._index_indicator.setText("SEEN")
                self._index_indicator.setStyleSheet(
                    "background-color: #00aa00; color: white; "
                    "padding: 2px 8px; border-radius: 3px;"
                )
            else:
                self._index_indicator.setText("NOT SEEN")
                self._index_indicator.setStyleSheet(
                    "background-color: #444; color: gray; "
                    "padding: 2px 8px; border-radius: 3px;"
                )

    def show_raw_response(self, command: str, response: str):
        """Show raw command/response in the raw data display."""
        text = f"> {command}\n{response}"
        self._raw_label.setText(text)
