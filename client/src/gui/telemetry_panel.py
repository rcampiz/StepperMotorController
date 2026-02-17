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
    QProgressBar,
    QPushButton,
)
from PySide6.QtCore import Qt, Slot, Signal
from PySide6.QtGui import QFont
import re


ENCODER_CPR = 4000  # Counts per revolution (1000 PPR × 4 quadrature)


class TelemetryPanel(QWidget):
    """Panel for displaying telemetry data."""

    clear_fault_requested = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._setup_ui()

    def _setup_ui(self):
        """Set up the user interface."""
        layout = QVBoxLayout(self)
        layout.setSpacing(12)

        layout.addWidget(self._create_motor_group())
        layout.addWidget(self._create_encoder_group())
        layout.addWidget(self._create_status_group())
        layout.addWidget(self._create_link_health_group())
        layout.addWidget(self._create_motor_config_group())
        layout.addWidget(self._create_raw_group())
        layout.addStretch()

    # ========================================================================
    # Motor / Encoder / Status / Link Health groups
    # ========================================================================

    def _create_motor_group(self) -> QGroupBox:
        """Create motor telemetry group."""
        group = QGroupBox("Motor")
        layout = QGridLayout(group)

        layout.addWidget(QLabel("Position:"), 0, 0)
        self._position_label = QLabel("---")
        self._position_label.setFont(QFont("Consolas", 12, QFont.Bold))
        self._position_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(self._position_label, 0, 1)
        layout.addWidget(QLabel("steps"), 0, 2)

        layout.addWidget(QLabel("Speed:"), 1, 0)
        self._speed_label = QLabel("---")
        self._speed_label.setFont(QFont("Consolas", 12, QFont.Bold))
        self._speed_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(self._speed_label, 1, 1)
        layout.addWidget(QLabel("steps/s"), 1, 2)

        self._speed_bar = QProgressBar()
        self._speed_bar.setRange(0, 100000)
        self._speed_bar.setValue(0)
        self._speed_bar.setTextVisible(False)
        layout.addWidget(self._speed_bar, 2, 0, 1, 3)

        layout.setColumnStretch(1, 1)
        return group

    def _create_encoder_group(self) -> QGroupBox:
        """Create encoder telemetry group."""
        group = QGroupBox("Encoder")
        layout = QGridLayout(group)

        layout.addWidget(QLabel("Count:"), 0, 0)
        self._encoder_count_label = QLabel("---")
        self._encoder_count_label.setFont(QFont("Consolas", 12, QFont.Bold))
        self._encoder_count_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(self._encoder_count_label, 0, 1)
        layout.addWidget(QLabel("ticks"), 0, 2)

        layout.addWidget(QLabel("Velocity:"), 1, 0)
        self._encoder_velocity_label = QLabel("---")
        self._encoder_velocity_label.setFont(QFont("Consolas", 12, QFont.Bold))
        self._encoder_velocity_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(self._encoder_velocity_label, 1, 1)
        layout.addWidget(QLabel("ticks/s"), 1, 2)

        layout.addWidget(QLabel("Revolutions:"), 2, 0)
        self._revolutions_label = QLabel("---")
        self._revolutions_label.setFont(QFont("Consolas", 12, QFont.Bold))
        self._revolutions_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(self._revolutions_label, 2, 1)
        layout.addWidget(QLabel("rev"), 2, 2)

        layout.addWidget(QLabel("Rev/s:"), 3, 0)
        self._revs_label = QLabel("---")
        self._revs_label.setFont(QFont("Consolas", 12, QFont.Bold))
        self._revs_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(self._revs_label, 3, 1)
        layout.addWidget(QLabel("rev/s"), 3, 2)

        layout.addWidget(QLabel("RPM:"), 4, 0)
        self._rpm_label = QLabel("---")
        self._rpm_label.setFont(QFont("Consolas", 12, QFont.Bold))
        self._rpm_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(self._rpm_label, 4, 1)
        layout.addWidget(QLabel("rev/min"), 4, 2)

        layout.addWidget(QLabel("Index:"), 5, 0)
        self._index_indicator = QLabel("NOT SEEN")
        self._index_indicator.setAlignment(Qt.AlignCenter)
        self._index_indicator.setStyleSheet(
            "background-color: #444; color: gray; padding: 2px 8px; border-radius: 3px;"
        )
        layout.addWidget(self._index_indicator, 5, 1, 1, 2)

        layout.setColumnStretch(1, 1)
        return group

    def _create_status_group(self) -> QGroupBox:
        """Create status indicators group."""
        group = QGroupBox("Status")
        outer = QVBoxLayout(group)

        row = QHBoxLayout()
        self._busy_indicator = self._create_indicator("BUSY")
        row.addWidget(self._busy_indicator)
        self._hiz_indicator = self._create_indicator("HI-Z")
        row.addWidget(self._hiz_indicator)
        self._ccw_indicator = self._create_indicator("CCW")
        row.addWidget(self._ccw_indicator)
        self._cw_indicator = self._create_indicator("CW")
        row.addWidget(self._cw_indicator)
        self._error_indicator = self._create_indicator("ERROR")
        row.addWidget(self._error_indicator)
        row.addStretch()
        outer.addLayout(row)

        # Motion state indicator (Stopped / Accel / Const / Decel)
        row2 = QHBoxLayout()
        self._motion_indicator = self._create_indicator("STOPPED")
        self._motion_indicator.setFixedWidth(80)
        row2.addWidget(self._motion_indicator)
        row2.addStretch()
        outer.addLayout(row2)

        self._fault_detail_label = QLabel("")
        self._fault_detail_label.setFont(QFont("Consolas", 9, QFont.Bold))
        self._fault_detail_label.setStyleSheet("color: #ff4444; padding: 2px 4px;")
        self._fault_detail_label.setWordWrap(True)
        self._fault_detail_label.setVisible(False)
        outer.addWidget(self._fault_detail_label)

        self._clear_fault_btn = QPushButton("Clear Fault")
        self._clear_fault_btn.setFixedWidth(100)
        self._clear_fault_btn.setVisible(False)
        self._clear_fault_btn.clicked.connect(self.clear_fault_requested.emit)
        outer.addWidget(self._clear_fault_btn)

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

        self._link_indicator = self._create_indicator("LINK")
        layout.addWidget(self._link_indicator)

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
        if missed > 5 or rtt > 200:
            self._set_indicator_active(self._link_indicator, True, "#ff0000")
        elif missed > 0 or rtt > 50:
            self._set_indicator_active(self._link_indicator, True, "#ffaa00")
        elif data.get('acked', 0) > 0:
            self._set_indicator_active(self._link_indicator, True, "#00aa00")
        else:
            self._set_indicator_active(self._link_indicator, False)

    # ========================================================================
    # Motor Config / Raw Data groups
    # ========================================================================

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

    # ========================================================================
    # Indicator helpers
    # ========================================================================

    def _set_indicator_active(self, indicator: QLabel, active: bool,
                              color: str = "green"):
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

    # ========================================================================
    # Telemetry data update
    # ========================================================================

    @Slot(dict)
    def update_data(self, data: dict):
        """Update telemetry display with new data."""
        if not data:
            return

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
                self._set_indicator_active(
                    self._cw_indicator, direction == 1, "#00cc66")
                self._set_indicator_active(
                    self._ccw_indicator, direction == 0, "#3399ff")

            mot_status = motor.get("mot_status")
            if mot_status is not None:
                mot_labels = {0: "STOPPED", 1: "ACCEL", 2: "DECEL", 3: "CONST"}
                mot_colors = {0: None, 1: "#ff9900", 2: "#ff6633", 3: "#00cc66"}
                label = mot_labels.get(mot_status, "?")
                color = mot_colors.get(mot_status)
                self._motion_indicator.setText(label)
                self._set_indicator_active(
                    self._motion_indicator, mot_status != 0,
                    color or "#333")

        encoder = data.get("encoder", {})
        if isinstance(encoder, dict):
            count = encoder.get("count", encoder.get("cnt"))
            if count is not None:
                self._encoder_count_label.setText(f"{count:,}")
            velocity = encoder.get("velocity", encoder.get("vel"))
            if velocity is not None:
                self._encoder_velocity_label.setText(f"{velocity:,}")
            # Compute revolutions from count / CPR
            if count is not None:
                revs = count / ENCODER_CPR
                self._revolutions_label.setText(f"{revs:,.2f}")

            # Compute rev/s and RPM from velocity / CPR
            if velocity is not None:
                rev_s = velocity / ENCODER_CPR
                rpm = rev_s * 60.0
                self._revs_label.setText(f"{rev_s:,.2f}")
                self._rpm_label.setText(f"{rpm:,.1f}")
            else:
                self._revs_label.setText("---")
                self._rpm_label.setText("---")

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

        error = data.get("error", False)
        self._set_indicator_active(self._error_indicator, error, "#ff0000")

        # Show specific fault flags when error is active.
        # Firmware filters STATUS register bits against ALARM_EN config,
        # so only faults enabled in the Protection tab are reported here.
        if error and isinstance(motor, dict):
            faults = []
            if motor.get("ocd"):
                faults.append("OCD (overcurrent)")
            if motor.get("thermal_sd"):
                faults.append("TH_SD (thermal shutdown)")
            if motor.get("thermal_warn"):
                faults.append("TH_WARN (thermal warning)")
            if motor.get("uvlo"):
                faults.append("UVLO (undervoltage)")
            if motor.get("stall_a"):
                faults.append("STALL_A")
            if motor.get("stall_b"):
                faults.append("STALL_B")
            if motor.get("cmd_err"):
                faults.append("CMD_ERR")
            if faults:
                self._fault_detail_label.setText(
                    "STATUS flags: " + ", ".join(faults))
                self._fault_detail_label.setVisible(True)
                self._clear_fault_btn.setVisible(True)
            else:
                self._fault_detail_label.setVisible(False)
                self._clear_fault_btn.setVisible(False)
        else:
            self._fault_detail_label.setVisible(False)
            self._clear_fault_btn.setVisible(False)

        self._update_raw_display(data)

    def _update_raw_display(self, data: dict):
        """Update raw data display."""
        lines = []
        motor = data.get("motor", {})
        if motor:
            lines.append(
                f"Motor: pos={motor.get('position', '?')} "
                f"spd={motor.get('speed', '?')}")
        encoder = data.get("encoder", {})
        if encoder:
            lines.append(
                f"Encoder: cnt={encoder.get('count', '?')} "
                f"vel={encoder.get('velocity', '?')}")
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
        self._revolutions_label.setText("---")
        self._revs_label.setText("---")
        self._rpm_label.setText("---")

        self._index_indicator.setText("NOT SEEN")
        self._index_indicator.setStyleSheet(
            "background-color: #444; color: gray; "
            "padding: 2px 8px; border-radius: 3px;"
        )

        self._set_indicator_active(self._busy_indicator, False)
        self._set_indicator_active(self._hiz_indicator, False)
        self._set_indicator_active(self._error_indicator, False)
        self._fault_detail_label.setText("")
        self._fault_detail_label.setVisible(False)
        self._clear_fault_btn.setVisible(False)
        self._set_indicator_active(self._cw_indicator, False)
        self._set_indicator_active(self._ccw_indicator, False)
        self._motion_indicator.setText("STOPPED")
        self._set_indicator_active(self._motion_indicator, False)

        self._set_indicator_active(self._link_indicator, False)
        self._link_indicator.setText("LINK")
        self._rtt_label.setText("-- ms")
        self._hb_rate_label.setText("-- Hz")
        self._missed_label.setText("0")
        self._watchdog_label.setText("OFF")

        self._raw_label.setText("No data")

    def update_encoder_data(self, raw: str | None, error: str | None = None):
        """Update encoder display from ENC command response."""
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
            self._encoder_count_label.setText(
                f"{int(count_match.group(1)):,}")
        if vel_match:
            self._encoder_velocity_label.setText(
                f"{int(vel_match.group(1)):,}")
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
