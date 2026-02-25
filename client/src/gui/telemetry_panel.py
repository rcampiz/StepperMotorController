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
    QSpinBox,
    QCheckBox,
)
from PySide6.QtCore import Qt, Slot, Signal
from PySide6.QtGui import QFont
import re


ENCODER_CPR = 4000  # Counts per revolution (1000 PPR × 4 quadrature)


class TelemetryPanel(QWidget):
    """Panel for displaying telemetry data."""

    clear_fault_requested = Signal()
    enc_filter_command = Signal(str)  # SCPI command string for encoder filter

    def __init__(self, parent=None):
        super().__init__(parent)
        self._setup_ui()

    def _setup_ui(self):
        """Set up the user interface."""
        layout = QVBoxLayout(self)
        layout.setSpacing(12)

        layout.addWidget(self._create_motor_group())
        layout.addWidget(self._create_encoder_group())
        layout.addWidget(self._create_encoder_filter_group())
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
        self._speed_bar.setRange(0, 15625)  # powerSTEP01 max speed in steps/s
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

        layout.addWidget(QLabel("Vel Quality:"), 5, 0)
        self._vel_quality_label = QLabel("---")
        self._vel_quality_label.setFont(QFont("Consolas", 10, QFont.Bold))
        self._vel_quality_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(self._vel_quality_label, 5, 1, 1, 2)

        layout.addWidget(QLabel("Index:"), 6, 0)
        self._index_indicator = QLabel("NOT SEEN")
        self._index_indicator.setAlignment(Qt.AlignCenter)
        self._index_indicator.setStyleSheet(
            "background-color: #444; color: gray; padding: 2px 8px; border-radius: 3px;"
        )
        layout.addWidget(self._index_indicator, 6, 1, 1, 2)

        layout.setColumnStretch(1, 1)
        return group

    def _create_encoder_filter_group(self) -> QGroupBox:
        """Create encoder filter configuration group."""
        group = QGroupBox("Encoder Filter")
        layout = QGridLayout(group)

        # Row 0: Measurement window + Sample rate
        layout.addWidget(QLabel("Meas. Window:"), 0, 0)
        self._filt_window_spin = QSpinBox()
        self._filt_window_spin.setRange(1, 255)
        self._filt_window_spin.setValue(40)
        self._filt_window_spin.setSuffix(" ms")
        self._filt_window_spin.setFixedWidth(80)
        self._filt_window_spin.setFont(QFont("Consolas", 9))
        layout.addWidget(self._filt_window_spin, 0, 1)

        layout.addWidget(QLabel("Sample Rate:"), 0, 2)
        self._filt_rate_spin = QSpinBox()
        self._filt_rate_spin.setRange(100, 10000)
        self._filt_rate_spin.setValue(1000)
        self._filt_rate_spin.setSingleStep(100)
        self._filt_rate_spin.setSuffix(" Hz")
        self._filt_rate_spin.setFixedWidth(100)
        self._filt_rate_spin.setFont(QFont("Consolas", 9))
        layout.addWidget(self._filt_rate_spin, 0, 3)

        # Row 1: EMA checkbox + alpha
        self._filt_ema_check = QCheckBox("EMA")
        self._filt_ema_check.setStyleSheet(
            "QCheckBox { color: #00ccff; font-weight: bold; }")
        self._filt_ema_check.setChecked(True)
        layout.addWidget(self._filt_ema_check, 1, 0)
        lbl_alpha = QLabel("Alpha:")
        lbl_alpha.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(lbl_alpha, 1, 1)
        self._filt_ema_alpha = QSpinBox()
        self._filt_ema_alpha.setRange(0, 255)
        self._filt_ema_alpha.setValue(200)
        self._filt_ema_alpha.setFixedWidth(70)
        self._filt_ema_alpha.setFont(QFont("Consolas", 9))
        layout.addWidget(self._filt_ema_alpha, 1, 2)

        # Row 2: SMA checkbox + window
        self._filt_sma_check = QCheckBox("SMA")
        self._filt_sma_check.setStyleSheet(
            "QCheckBox { color: #66ff99; font-weight: bold; }")
        self._filt_sma_check.setChecked(True)
        layout.addWidget(self._filt_sma_check, 2, 0)
        lbl_win = QLabel("Window:")
        lbl_win.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(lbl_win, 2, 1)
        self._filt_sma_window = QSpinBox()
        self._filt_sma_window.setRange(0, 32)
        self._filt_sma_window.setValue(8)
        self._filt_sma_window.setFixedWidth(70)
        self._filt_sma_window.setFont(QFont("Consolas", 9))
        self._filt_sma_window.setToolTip("0 = speed-adaptive (auto)")
        layout.addWidget(self._filt_sma_window, 2, 2)
        adapt_lbl = QLabel("(0 = adaptive)")
        adapt_lbl.setStyleSheet("color: #888; font-size: 9px;")
        layout.addWidget(adapt_lbl, 2, 3)

        # Row 3: Padé sharpener checkbox + gain + max correction
        self._filt_pade_check = QCheckBox("Padé")
        self._filt_pade_check.setStyleSheet(
            "QCheckBox { color: #ffcc00; font-weight: bold; }")
        self._filt_pade_check.setChecked(True)
        self._filt_pade_check.setToolTip(
            "Padé [1/1] approximant lag compensation.\n"
            "Removes averaging blur from EMA/SMA output.")
        layout.addWidget(self._filt_pade_check, 3, 0)

        lbl_gain = QLabel("Gain %:")
        lbl_gain.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(lbl_gain, 3, 1)
        self._filt_pade_gain = QSpinBox()
        self._filt_pade_gain.setRange(1, 100)
        self._filt_pade_gain.setValue(25)
        self._filt_pade_gain.setSuffix("%")
        self._filt_pade_gain.setFixedWidth(70)
        self._filt_pade_gain.setFont(QFont("Consolas", 9))
        self._filt_pade_gain.setToolTip("Correction strength (lower = less overshoot)")
        layout.addWidget(self._filt_pade_gain, 3, 2)

        # Row 4: Padé max correction
        lbl_max = QLabel("Max Corr:")
        lbl_max.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(lbl_max, 4, 1)
        self._filt_pade_max = QSpinBox()
        self._filt_pade_max.setRange(1, 255)
        self._filt_pade_max.setValue(50)
        self._filt_pade_max.setSuffix(" tps")
        self._filt_pade_max.setFixedWidth(80)
        self._filt_pade_max.setFont(QFont("Consolas", 9))
        self._filt_pade_max.setToolTip("Max absolute correction per sample (clamps spikes)")
        layout.addWidget(self._filt_pade_max, 4, 2)

        # Row 5: Butterworth 2nd-order IIR low-pass
        self._filt_biquad_check = QCheckBox("Butterworth")
        self._filt_biquad_check.setStyleSheet(
            "QCheckBox { color: #ff9966; font-weight: bold; }")
        self._filt_biquad_check.setChecked(True)
        self._filt_biquad_check.setToolTip(
            "2nd-order Butterworth IIR low-pass filter.\n"
            "Removes high-frequency noise with flat passband.")
        layout.addWidget(self._filt_biquad_check, 5, 0)
        lbl_bq_cut = QLabel("Cutoff:")
        lbl_bq_cut.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(lbl_bq_cut, 5, 1)
        self._filt_biquad_cutoff = QSpinBox()
        self._filt_biquad_cutoff.setRange(1, 50)
        self._filt_biquad_cutoff.setValue(5)
        self._filt_biquad_cutoff.setSuffix(" Hz")
        self._filt_biquad_cutoff.setFixedWidth(80)
        self._filt_biquad_cutoff.setFont(QFont("Consolas", 9))
        self._filt_biquad_cutoff.setToolTip("Cutoff frequency (lower = smoother, more lag)")
        layout.addWidget(self._filt_biquad_cutoff, 5, 2)

        # Row 6: Notch (band-reject) filter
        self._filt_notch_check = QCheckBox("Notch")
        self._filt_notch_check.setStyleSheet(
            "QCheckBox { color: #cc99ff; font-weight: bold; }")
        self._filt_notch_check.setToolTip(
            "Band-reject (notch) filter.\n"
            "Removes a specific resonance frequency.")
        layout.addWidget(self._filt_notch_check, 6, 0)
        lbl_nt_ctr = QLabel("Center:")
        lbl_nt_ctr.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(lbl_nt_ctr, 6, 1)
        self._filt_notch_center = QSpinBox()
        self._filt_notch_center.setRange(1, 50)
        self._filt_notch_center.setValue(25)
        self._filt_notch_center.setSuffix(" Hz")
        self._filt_notch_center.setFixedWidth(80)
        self._filt_notch_center.setFont(QFont("Consolas", 9))
        self._filt_notch_center.setToolTip("Center frequency to reject")
        layout.addWidget(self._filt_notch_center, 6, 2)

        # Row 7: Notch Q factor
        lbl_nt_q = QLabel("Q (x10):")
        lbl_nt_q.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(lbl_nt_q, 7, 1)
        self._filt_notch_q = QSpinBox()
        self._filt_notch_q.setRange(1, 100)
        self._filt_notch_q.setValue(50)
        self._filt_notch_q.setFixedWidth(70)
        self._filt_notch_q.setFont(QFont("Consolas", 9))
        self._filt_notch_q.setToolTip("Q factor x10 (higher = narrower notch, 50 = Q=5.0)")
        layout.addWidget(self._filt_notch_q, 7, 2)

        # Row 8: Holt's double exponential smoothing
        self._filt_holt_check = QCheckBox("Holt")
        self._filt_holt_check.setStyleSheet(
            "QCheckBox { color: #ff66cc; font-weight: bold; }")
        self._filt_holt_check.setToolTip(
            "Holt's double exponential smoothing.\n"
            "Tracks level + trend for smooth prediction.")
        layout.addWidget(self._filt_holt_check, 8, 0)
        lbl_h_a = QLabel("Alpha:")
        lbl_h_a.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(lbl_h_a, 8, 1)
        self._filt_holt_alpha = QSpinBox()
        self._filt_holt_alpha.setRange(0, 255)
        self._filt_holt_alpha.setValue(51)
        self._filt_holt_alpha.setFixedWidth(70)
        self._filt_holt_alpha.setFont(QFont("Consolas", 9))
        self._filt_holt_alpha.setToolTip("Level smoothing (0-255, lower = smoother)")
        layout.addWidget(self._filt_holt_alpha, 8, 2)

        # Row 9: Holt beta
        lbl_h_b = QLabel("Beta:")
        lbl_h_b.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(lbl_h_b, 9, 1)
        self._filt_holt_beta = QSpinBox()
        self._filt_holt_beta.setRange(0, 255)
        self._filt_holt_beta.setValue(13)
        self._filt_holt_beta.setFixedWidth(70)
        self._filt_holt_beta.setFont(QFont("Consolas", 9))
        self._filt_holt_beta.setToolTip("Trend smoothing (0-255, lower = smoother trend)")
        layout.addWidget(self._filt_holt_beta, 9, 2)

        # Row 10: Buttons
        btn_row = QHBoxLayout()
        self._filt_apply_btn = QPushButton("Apply")
        self._filt_apply_btn.setFixedWidth(70)
        self._filt_apply_btn.clicked.connect(self._on_filter_apply)
        btn_row.addWidget(self._filt_apply_btn)

        self._filt_save_btn = QPushButton("Save to Flash")
        self._filt_save_btn.setFixedWidth(100)
        self._filt_save_btn.clicked.connect(self._on_filter_save)
        btn_row.addWidget(self._filt_save_btn)

        self._filt_reset_btn = QPushButton("Reset")
        self._filt_reset_btn.setFixedWidth(60)
        self._filt_reset_btn.clicked.connect(self._on_filter_reset)
        btn_row.addWidget(self._filt_reset_btn)

        btn_row.addStretch()
        layout.addLayout(btn_row, 10, 0, 1, 4)

        layout.setColumnStretch(3, 1)
        return group

    @Slot()
    def _on_filter_apply(self):
        """Send all filter settings to firmware."""
        window = self._filt_window_spin.value()
        self.enc_filter_command.emit(f"CTRL:ENC:FILT:WINDOW {window}")
        ema_en = 1 if self._filt_ema_check.isChecked() else 0
        alpha = self._filt_ema_alpha.value()
        self.enc_filter_command.emit(f"CTRL:ENC:FILT:EMA {ema_en} {alpha}")
        sma_en = 1 if self._filt_sma_check.isChecked() else 0
        win = self._filt_sma_window.value()
        self.enc_filter_command.emit(f"CTRL:ENC:FILT:SMA {sma_en} {win}")
        pade_en = 1 if self._filt_pade_check.isChecked() else 0
        pade_gain = self._filt_pade_gain.value()
        pade_max = self._filt_pade_max.value()
        self.enc_filter_command.emit(
            f"CTRL:ENC:FILT:PADE {pade_en} {pade_gain} {pade_max}")
        bq_en = 1 if self._filt_biquad_check.isChecked() else 0
        bq_cut = self._filt_biquad_cutoff.value()
        self.enc_filter_command.emit(f"CTRL:ENC:FILT:BIQUAD {bq_en} {bq_cut}")
        nt_en = 1 if self._filt_notch_check.isChecked() else 0
        nt_ctr = self._filt_notch_center.value()
        nt_q = self._filt_notch_q.value()
        self.enc_filter_command.emit(
            f"CTRL:ENC:FILT:NOTCH {nt_en} {nt_ctr} {nt_q}")
        holt_en = 1 if self._filt_holt_check.isChecked() else 0
        holt_a = self._filt_holt_alpha.value()
        holt_b = self._filt_holt_beta.value()
        self.enc_filter_command.emit(
            f"CTRL:ENC:FILT:HOLT {holt_en} {holt_a} {holt_b}")
        rate = self._filt_rate_spin.value()
        self.enc_filter_command.emit(f"CTRL:ENC:FILT:RATE {rate}")

    @Slot()
    def _on_filter_save(self):
        """Persist current filter config to flash."""
        self._on_filter_apply()
        self.enc_filter_command.emit("CTRL:ENC:FILT:SAVE")

    @Slot()
    def _on_filter_reset(self):
        """Reset filter config to defaults."""
        self.enc_filter_command.emit("CTRL:ENC:FILT:RESET")
        # Reset local controls to defaults
        self._filt_window_spin.setValue(40)
        self._filt_rate_spin.setValue(1000)
        self._filt_ema_check.setChecked(True)
        self._filt_ema_alpha.setValue(200)
        self._filt_sma_check.setChecked(True)
        self._filt_sma_window.setValue(8)
        self._filt_pade_check.setChecked(True)
        self._filt_pade_gain.setValue(25)
        self._filt_pade_max.setValue(50)
        self._filt_biquad_check.setChecked(True)
        self._filt_biquad_cutoff.setValue(5)
        self._filt_notch_check.setChecked(False)
        self._filt_notch_center.setValue(25)
        self._filt_notch_q.setValue(50)
        self._filt_holt_check.setChecked(False)
        self._filt_holt_alpha.setValue(51)
        self._filt_holt_beta.setValue(13)

    @Slot(dict)
    def update_filter_config(self, data: dict):
        """Update filter controls from firmware query response."""
        self._filt_window_spin.setValue(data.get("meas_window_ms", 40))
        self._filt_rate_spin.setValue(data.get("sample_rate_hz", 1000))
        self._filt_ema_check.setChecked(data.get("ema_enabled", True))
        self._filt_ema_alpha.setValue(data.get("ema_alpha", 200))
        self._filt_sma_check.setChecked(data.get("sma_enabled", True))
        self._filt_sma_window.setValue(data.get("sma_window", 8))
        self._filt_pade_check.setChecked(data.get("pade_enabled", True))
        self._filt_pade_gain.setValue(data.get("pade_gain", 25))
        self._filt_pade_max.setValue(data.get("pade_max_corr", 50))
        self._filt_biquad_check.setChecked(data.get("biquad_enabled", True))
        self._filt_biquad_cutoff.setValue(data.get("biquad_cutoff_hz", 5))
        self._filt_notch_check.setChecked(data.get("notch_enabled", False))
        self._filt_notch_center.setValue(data.get("notch_center_hz", 25))
        self._filt_notch_q.setValue(data.get("notch_q10", 50))
        self._filt_holt_check.setChecked(data.get("holt_enabled", False))
        self._filt_holt_alpha.setValue(data.get("holt_alpha", 51))
        self._filt_holt_beta.setValue(data.get("holt_beta", 13))

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
                self._speed_bar.setValue(min(abs(speed), self._speed_bar.maximum()))
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

            # Velocity quality indicator
            vel_quality = encoder.get("vel_quality")
            if vel_quality is not None:
                vq_names = {0: "GOOD", 1: "LOW", 2: "STALE", 3: "INVALID"}
                vq_colors = {0: "#00aa00", 1: "#ccaa44", 2: "#ff6633", 3: "#ff0000"}
                vq_str = vq_names.get(vel_quality, "?")
                vq_color = vq_colors.get(vel_quality, "#888")
                self._vel_quality_label.setText(vq_str)
                self._vel_quality_label.setStyleSheet(
                    f"color: {vq_color}; font-weight: bold;")

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

    @Slot(int)
    def set_max_speed(self, value: int):
        """Update speed bar range from Driver panel's MAX SPD."""
        value = max(1, value)
        self._speed_bar.setMaximum(value)

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

        self._vel_quality_label.setText("---")
        self._vel_quality_label.setStyleSheet("")

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

        # Reset encoder filter controls
        self._filt_window_spin.setValue(40)
        self._filt_rate_spin.setValue(1000)
        self._filt_ema_check.setChecked(True)
        self._filt_ema_alpha.setValue(200)
        self._filt_sma_check.setChecked(True)
        self._filt_sma_window.setValue(8)
        self._filt_pade_check.setChecked(True)
        self._filt_pade_gain.setValue(25)
        self._filt_pade_max.setValue(50)
        self._filt_biquad_check.setChecked(True)
        self._filt_biquad_cutoff.setValue(5)
        self._filt_notch_check.setChecked(False)
        self._filt_notch_center.setValue(25)
        self._filt_notch_q.setValue(50)
        self._filt_holt_check.setChecked(False)
        self._filt_holt_alpha.setValue(51)
        self._filt_holt_beta.setValue(13)

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
