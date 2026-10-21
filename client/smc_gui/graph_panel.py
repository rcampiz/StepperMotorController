"""
Real-time graph panel for motor and encoder telemetry.

Plots motor speed and encoder velocity over time using pyqtgraph, and under it the slip: how
far the shaft is from where it was commanded, in degrees (smc_core.slip), on its own scale so a
lost step (1.8 degrees) shows. The time window is 10, 30 or 60 s.
Samples are placed on the board's own clock (the telemetry "tick", microseconds), not on when
the PC happened to read them, and the plots are redrawn at most ~30 times a second however fast
telemetry arrives (50 Hz at 20 ms on a fast link).
Provides configurable display-side filtering for velocity data.

Filter types:
  - None: raw passthrough
  - EMA (Exponential Moving Average): single-pole IIR low-pass, param=alpha (0-255)
  - SMA (Simple Moving Average): FIR window, param=window size (2-32)
  - Median: sorted window, returns middle value (spike rejection)
  - Padé: [1/1] Padé approximant sharpener — removes averaging lag

Firmware-side filtering is configured from the Dashboard tab (Encoder Filter group).
"""

from collections import deque
import math
import statistics
import time

from PySide6.QtWidgets import (
    QPushButton,
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QComboBox,
    QSpinBox, QDoubleSpinBox, QSlider, QStackedWidget, QCheckBox,
)
from PySide6.QtCore import Slot, Signal, Qt, QSettings, QTimer
from PySide6.QtGui import QFont

import numpy as np
import pyqtgraph as pg

from smc_core.slip import SlipTracker
from smc_gui import theme
from smc_gui.encoder_dial import EncoderDial


# Match the dark theme of the rest of the app
pg.setConfigOptions(background=theme.BASE, foreground=theme.TEXT, antialias=True)

# Encoder counts per revolution (quadrature mode: PPR × 4)
ENCODER_CPR = 4000  # 1000 PPR × 4 quadrature

# Display-side filter type names
DISP_FILTER_TYPES = ["None", "EMA", "SMA", "Median", "Padé",
                     "Butterworth", "Notch", "Holt"]

# Display-side sampling rate for cutoff frequency calculation
DISP_SAMPLE_RATE_HZ = 10  # client polls at ~10 Hz


def _ema_cutoff_hz(alpha: int, fs: float) -> float:
    """Approximate -3dB cutoff frequency for EMA filter.

    fc = fs / (2π) * ln((256) / (256 - (256 - alpha)))
    Simplified: fc ≈ fs * (256 - alpha) / (2π * 256)
    """
    if alpha <= 0:
        return fs / 2.0  # Nyquist (no filtering)
    weight = (256 - alpha) / 256.0
    if weight <= 0:
        return 0.0
    # -3dB point of single-pole IIR
    import math
    return -fs / (2.0 * math.pi) * math.log(1.0 - weight)


class _FilterControlRow(QWidget):
    """Reusable filter control row: type combo + parameter widget + description."""

    filter_changed = Signal()  # emitted when type or param changes

    def __init__(self, label: str, filter_types: list, sample_rate: float,
                 parent=None):
        super().__init__(parent)
        self._filter_types = filter_types
        self._sample_rate = sample_rate

        row = QHBoxLayout(self)
        row.setContentsMargins(0, 0, 0, 0)
        row.setSpacing(8)

        # Label
        lbl = QLabel(f"{label}:")
        lbl.setFont(QFont("Segoe UI", 9, QFont.Bold))
        lbl.setFixedWidth(55)
        row.addWidget(lbl)

        # Type combo
        self._type_combo = QComboBox()
        for ft in filter_types:
            self._type_combo.addItem(ft)
        self._type_combo.setFixedWidth(80)
        self._type_combo.currentIndexChanged.connect(self._on_type_changed)
        row.addWidget(self._type_combo)

        # Stacked parameter widgets (one per filter type)
        self._param_stack = QStackedWidget()

        # Page 0: None — empty placeholder
        self._param_stack.addWidget(QWidget())

        # Page 1: EMA — alpha slider + spinbox
        ema_page = QWidget()
        ema_lay = QHBoxLayout(ema_page)
        ema_lay.setContentsMargins(0, 0, 0, 0)
        ema_lay.setSpacing(4)
        ema_lbl = QLabel("α:")
        ema_lbl.setFont(QFont("Consolas", 9))
        ema_lay.addWidget(ema_lbl)
        self._ema_slider = QSlider(Qt.Horizontal)
        self._ema_slider.setRange(0, 255)
        self._ema_slider.setValue(128)
        self._ema_slider.setFixedWidth(120)
        ema_lay.addWidget(self._ema_slider)
        self._ema_spin = QSpinBox()
        self._ema_spin.setRange(0, 255)
        self._ema_spin.setValue(128)
        self._ema_spin.setFixedWidth(55)
        self._ema_spin.setFont(QFont("Consolas", 9))
        ema_lay.addWidget(self._ema_spin)
        self._ema_slider.valueChanged.connect(self._ema_spin.setValue)
        self._ema_spin.valueChanged.connect(self._ema_slider.setValue)
        self._ema_spin.valueChanged.connect(lambda _: self._on_param_changed())
        self._param_stack.addWidget(ema_page)

        # Page 2: SMA — window spinbox
        sma_page = QWidget()
        sma_lay = QHBoxLayout(sma_page)
        sma_lay.setContentsMargins(0, 0, 0, 0)
        sma_lay.setSpacing(4)
        sma_lbl = QLabel("Window:")
        sma_lbl.setFont(QFont("Consolas", 9))
        sma_lay.addWidget(sma_lbl)
        self._sma_spin = QSpinBox()
        self._sma_spin.setRange(2, 32)
        self._sma_spin.setValue(8)
        self._sma_spin.setFixedWidth(55)
        self._sma_spin.setFont(QFont("Consolas", 9))
        self._sma_spin.valueChanged.connect(lambda _: self._on_param_changed())
        sma_lay.addWidget(self._sma_spin)
        sma_lay.addStretch()
        self._param_stack.addWidget(sma_page)

        # Page 3: Median — window spinbox (only for display, odd values)
        if "Median" in filter_types:
            med_page = QWidget()
            med_lay = QHBoxLayout(med_page)
            med_lay.setContentsMargins(0, 0, 0, 0)
            med_lay.setSpacing(4)
            med_lbl = QLabel("Window:")
            med_lbl.setFont(QFont("Consolas", 9))
            med_lay.addWidget(med_lbl)
            self._med_spin = QSpinBox()
            self._med_spin.setRange(3, 15)
            self._med_spin.setSingleStep(2)
            self._med_spin.setValue(5)
            self._med_spin.setFixedWidth(55)
            self._med_spin.setFont(QFont("Consolas", 9))
            self._med_spin.valueChanged.connect(lambda _: self._on_param_changed())
            med_lay.addWidget(self._med_spin)
            med_lay.addStretch()
            self._param_stack.addWidget(med_page)

        # Page 4: Padé — no parameters (empty placeholder)
        if "Padé" in filter_types:
            self._param_stack.addWidget(QWidget())

        # Page 5: Butterworth — cutoff frequency spinbox
        if "Butterworth" in filter_types:
            bq_page = QWidget()
            bq_lay = QHBoxLayout(bq_page)
            bq_lay.setContentsMargins(0, 0, 0, 0)
            bq_lay.setSpacing(4)
            bq_lbl = QLabel("Cutoff:")
            bq_lbl.setFont(QFont("Consolas", 9))
            bq_lay.addWidget(bq_lbl)
            self._bq_spin = QSpinBox()
            self._bq_spin.setRange(1, 50)
            self._bq_spin.setValue(10)
            self._bq_spin.setSuffix(" Hz")
            self._bq_spin.setFixedWidth(70)
            self._bq_spin.setFont(QFont("Consolas", 9))
            self._bq_spin.valueChanged.connect(lambda _: self._on_param_changed())
            bq_lay.addWidget(self._bq_spin)
            bq_lay.addStretch()
            self._param_stack.addWidget(bq_page)

        # Page 6: Notch — center Hz + Q spinboxes
        if "Notch" in filter_types:
            nt_page = QWidget()
            nt_lay = QHBoxLayout(nt_page)
            nt_lay.setContentsMargins(0, 0, 0, 0)
            nt_lay.setSpacing(4)
            nt_ctr_lbl = QLabel("Ctr:")
            nt_ctr_lbl.setFont(QFont("Consolas", 9))
            nt_lay.addWidget(nt_ctr_lbl)
            self._nt_center_spin = QSpinBox()
            self._nt_center_spin.setRange(1, 50)
            self._nt_center_spin.setValue(25)
            self._nt_center_spin.setSuffix(" Hz")
            self._nt_center_spin.setFixedWidth(70)
            self._nt_center_spin.setFont(QFont("Consolas", 9))
            self._nt_center_spin.valueChanged.connect(lambda _: self._on_param_changed())
            nt_lay.addWidget(self._nt_center_spin)
            nt_q_lbl = QLabel("Q:")
            nt_q_lbl.setFont(QFont("Consolas", 9))
            nt_lay.addWidget(nt_q_lbl)
            self._nt_q_spin = QDoubleSpinBox()
            self._nt_q_spin.setRange(0.1, 10.0)
            self._nt_q_spin.setValue(5.0)
            self._nt_q_spin.setSingleStep(0.5)
            self._nt_q_spin.setFixedWidth(60)
            self._nt_q_spin.setFont(QFont("Consolas", 9))
            self._nt_q_spin.valueChanged.connect(lambda _: self._on_param_changed())
            nt_lay.addWidget(self._nt_q_spin)
            nt_lay.addStretch()
            self._param_stack.addWidget(nt_page)

        # Page 7: Holt — alpha + beta spinboxes
        if "Holt" in filter_types:
            hl_page = QWidget()
            hl_lay = QHBoxLayout(hl_page)
            hl_lay.setContentsMargins(0, 0, 0, 0)
            hl_lay.setSpacing(4)
            hl_a_lbl = QLabel("α:")
            hl_a_lbl.setFont(QFont("Consolas", 9))
            hl_lay.addWidget(hl_a_lbl)
            self._hl_alpha_spin = QDoubleSpinBox()
            self._hl_alpha_spin.setRange(0.01, 1.0)
            self._hl_alpha_spin.setValue(0.20)
            self._hl_alpha_spin.setSingleStep(0.05)
            self._hl_alpha_spin.setFixedWidth(60)
            self._hl_alpha_spin.setFont(QFont("Consolas", 9))
            self._hl_alpha_spin.valueChanged.connect(lambda _: self._on_param_changed())
            hl_lay.addWidget(self._hl_alpha_spin)
            hl_b_lbl = QLabel("β:")
            hl_b_lbl.setFont(QFont("Consolas", 9))
            hl_lay.addWidget(hl_b_lbl)
            self._hl_beta_spin = QDoubleSpinBox()
            self._hl_beta_spin.setRange(0.01, 1.0)
            self._hl_beta_spin.setValue(0.05)
            self._hl_beta_spin.setSingleStep(0.01)
            self._hl_beta_spin.setFixedWidth(60)
            self._hl_beta_spin.setFont(QFont("Consolas", 9))
            self._hl_beta_spin.valueChanged.connect(lambda _: self._on_param_changed())
            hl_lay.addWidget(self._hl_beta_spin)
            hl_lay.addStretch()
            self._param_stack.addWidget(hl_page)

        row.addWidget(self._param_stack)

        # Description label
        self._desc_label = QLabel("")
        self._desc_label.setFont(QFont("Consolas", 8))
        self._desc_label.setStyleSheet("color: #999;")
        self._desc_label.setMinimumWidth(180)
        row.addWidget(self._desc_label)

        row.addStretch()

        # Initialize state
        self._param_stack.setCurrentIndex(0)
        self._update_description()

    def filter_type(self) -> str:
        return self._type_combo.currentText()

    def filter_param(self) -> int:
        ft = self.filter_type()
        if ft == "EMA":
            return self._ema_spin.value()
        elif ft == "SMA":
            return self._sma_spin.value()
        elif ft == "Median":
            return self._med_spin.value()
        elif ft == "Butterworth":
            return self._bq_spin.value() if hasattr(self, '_bq_spin') else 10
        return 0

    def set_filter(self, filter_type: str, param: int):
        """Set filter type and parameter programmatically (no signal)."""
        self.blockSignals(True)
        idx = self._filter_types.index(filter_type) if filter_type in self._filter_types else 0
        self._type_combo.setCurrentIndex(idx)
        self._param_stack.setCurrentIndex(idx)
        if filter_type == "EMA":
            self._ema_spin.setValue(param)
        elif filter_type == "SMA":
            self._sma_spin.setValue(max(2, min(32, param)))
        elif filter_type == "Median":
            val = max(3, min(15, param))
            if val % 2 == 0:
                val += 1
            self._med_spin.setValue(val)
        self._update_description()
        self.blockSignals(False)

    def _on_type_changed(self, index: int):
        self._param_stack.setCurrentIndex(index)
        self._update_description()
        self.filter_changed.emit()

    def _on_param_changed(self):
        self._update_description()
        self.filter_changed.emit()

    def _update_description(self):
        ft = self.filter_type()
        if ft == "None":
            self._desc_label.setText("Raw (no filtering)")
        elif ft == "EMA":
            alpha = self._ema_spin.value()
            fc = _ema_cutoff_hz(alpha, self._sample_rate)
            self._desc_label.setText(
                f"IIR low-pass, α={alpha}, fc≈{fc:.1f} Hz")
        elif ft == "SMA":
            win = self._sma_spin.value()
            duration_ms = win * (1000.0 / self._sample_rate)
            self._desc_label.setText(
                f"FIR average, {win} samples = {duration_ms:.0f}ms window")
        elif ft == "Median":
            win = self._med_spin.value()
            duration_ms = win * (1000.0 / self._sample_rate)
            self._desc_label.setText(
                f"Median filter, {win} samples = {duration_ms:.0f}ms window")
        elif ft == "Padé":
            self._desc_label.setText(
                "Padé [1/1] lag compensator (no params)")
        elif ft == "Butterworth":
            cut = self._bq_spin.value() if hasattr(self, '_bq_spin') else 10
            self._desc_label.setText(
                f"2nd-order IIR low-pass, cutoff={cut} Hz")
        elif ft == "Notch":
            ctr = self._nt_center_spin.value() if hasattr(self, '_nt_center_spin') else 25
            q = self._nt_q_spin.value() if hasattr(self, '_nt_q_spin') else 5.0
            self._desc_label.setText(
                f"Band-reject, center={ctr} Hz, Q={q:.1f}")
        elif ft == "Holt":
            a = self._hl_alpha_spin.value() if hasattr(self, '_hl_alpha_spin') else 0.2
            b = self._hl_beta_spin.value() if hasattr(self, '_hl_beta_spin') else 0.05
            self._desc_label.setText(
                f"Double exp. smooth, α={a:.2f}, β={b:.2f}")

    def reset(self):
        """Reset to None filter."""
        self.blockSignals(True)
        self._type_combo.setCurrentIndex(0)
        self._param_stack.setCurrentIndex(0)
        self._ema_spin.setValue(128)
        self._sma_spin.setValue(8)
        if hasattr(self, '_med_spin'):
            self._med_spin.setValue(5)
        if hasattr(self, '_bq_spin'):
            self._bq_spin.setValue(10)
        if hasattr(self, '_nt_center_spin'):
            self._nt_center_spin.setValue(25)
        if hasattr(self, '_nt_q_spin'):
            self._nt_q_spin.setValue(5.0)
        if hasattr(self, '_hl_alpha_spin'):
            self._hl_alpha_spin.setValue(0.20)
        if hasattr(self, '_hl_beta_spin'):
            self._hl_beta_spin.setValue(0.05)
        self._update_description()
        self.blockSignals(False)


class GraphPanel(QWidget):
    capture_requested = Signal()
    """Panel with real-time plots for motor speed and encoder velocity."""

    MAX_POINTS = 3600  # 60 s at 50 Hz (20 ms telemetry on a fast link), with margin
    REDRAW_MS = 33     # plots repaint at most this often; samples are kept as they arrive
    TICK_WRAP = 1 << 32  # the telemetry tick: microseconds, 32 bits (wraps every 71.6 min)

    def __init__(self, parent=None):
        super().__init__(parent)

        self._t0 = None  # First sample timestamp (for relative X axis)
        self._t = 0.0           # seconds since the first sample, on the board's clock
        self._last_tick = None  # previous telemetry tick (us), None: not known
        self._last_host = 0.0   # previous sample's PC arrival time (time.monotonic)
        self._timestamps = deque(maxlen=self.MAX_POINTS)
        self._dirty = False
        self._redraw_timer = QTimer(self)
        self._redraw_timer.setInterval(self.REDRAW_MS)
        self._redraw_timer.timeout.connect(self._redraw)
        self._redraw_timer.start()
        self._motor_speed = deque(maxlen=self.MAX_POINTS)
        self._encoder_velocity = deque(maxlen=self.MAX_POINTS)
        self._full_steps_per_rev = 200  # Updated from DRV:FULL_STEPS? response
        self._slip = SlipTracker()
        self._slip_deg = deque(maxlen=self.MAX_POINTS)
        self._window_s = 30.0

        # Display-side filter state
        self._disp_filter_type = "None"
        self._disp_ema_state = 0.0
        self._disp_sma_buf = deque(maxlen=32)
        self._disp_median_buf = deque(maxlen=15)
        self._disp_pade_history = [0.0, 0.0, 0.0]
        self._disp_pade_count = 0
        # Butterworth display-side state (biquad DF2T)
        self._disp_bq_w0 = 0.0
        self._disp_bq_w1 = 0.0
        self._disp_bq_coeffs = None  # (b0, b1, b2, a1, a2) or None
        # Notch display-side state
        self._disp_nt_w0 = 0.0
        self._disp_nt_w1 = 0.0
        self._disp_nt_coeffs = None
        # Holt display-side state
        self._disp_holt_level = 0.0
        self._disp_holt_trend = 0.0
        self._disp_holt_init = False

        self._setup_ui()
        self._load_filter_settings()

    # Trace definitions: (key, label, color, dash, default_on)
    TRACE_DEFS = [
        ("motor_rpm", "Motor RPM",       "#ffcc00", False, True),
        ("enc_rpm",   "Encoder RPM",     "#00ccff", False, True),
        ("motor_spd", "Motor steps/s",   "#ff9966", True,  False),
        ("enc_vel",   "Encoder ticks/s", "#66ff99", True,  False),
    ]

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(4)

        # Display filter controls
        self._disp_filter = _FilterControlRow(
            "Disp", DISP_FILTER_TYPES, DISP_SAMPLE_RATE_HZ)
        self._disp_filter.filter_changed.connect(self._on_disp_filter_changed)
        layout.addWidget(self._disp_filter)

        # Trace visibility checkboxes
        cb_layout = QHBoxLayout()
        cb_layout.setSpacing(16)
        self._trace_cbs = {}
        for key, label, color, _dash, default_on in self.TRACE_DEFS:
            cb = QCheckBox(label)
            cb.setChecked(default_on)
            cb.setStyleSheet(f"QCheckBox {{ color: {color}; font-weight: bold; }}")
            cb.toggled.connect(self._on_trace_toggled)
            cb_layout.addWidget(cb)
            self._trace_cbs[key] = cb
        cb_layout.addStretch()
        cb_layout.addWidget(QLabel("Window:"))
        self._window_combo = QComboBox()
        for secs in (10, 30, 60):
            self._window_combo.addItem("%d s" % secs, float(secs))
        self._window_combo.setCurrentIndex(1)
        self._window_combo.currentIndexChanged.connect(
            lambda _i: setattr(self, "_window_s", float(self._window_combo.currentData())))
        cb_layout.addWidget(self._window_combo)
        self._slip_label = QLabel("slip --")
        self._slip_label.setStyleSheet("font-family: Consolas; color: %s;" % theme.MAGENTA)
        self._slip_label.setMinimumWidth(170)
        cb_layout.addWidget(self._slip_label)
        zero = QPushButton("Zero slip")
        zero.setToolTip("Start the slip from here (the encoder against the commanded position)")
        zero.clicked.connect(self.zero_slip)
        cb_layout.addWidget(zero)
        layout.addLayout(cb_layout)

        # Dial on the left, the speed / velocity plot on the right
        body = QHBoxLayout()
        side = QVBoxLayout()
        self._dial = EncoderDial()
        self._dial.setFixedSize(220, 220)
        side.addWidget(self._dial)
        self._capture_btn = QPushButton("Capture 2 kHz now")
        self._capture_btn.setToolTip("Run the position_capture scenario at the current speed: encoder and "
                                     "commanded position sampled together at 2 kHz, graphed on the "
                                     "Diagnostics tab (Tests).")
        self._capture_btn.clicked.connect(self.capture_requested.emit)
        side.addWidget(self._capture_btn)
        side.addStretch()
        body.addLayout(side)

        self._plot = pg.PlotWidget()
        self._plot.setLabel("bottom", "Time", units="s")
        # Major and minor grid: a GridItem draws both tick levels, the finer one fainter
        self._grid = pg.GridItem(pen=pg.mkPen(theme.GRID_MAJ), textPen=None)
        self._plot.addItem(self._grid)

        self._curves = {}
        for key, _label, color, dash, default_on in self.TRACE_DEFS:
            style = Qt.DashLine if dash else Qt.SolidLine
            curve = self._plot.plot(
                pen=pg.mkPen(color=color, width=2, style=style))
            curve.setVisible(default_on)
            self._curves[key] = curve

        # Slip under the speed plot, same time axis, its own scale (degrees of shaft)
        self._slip_plot = pg.PlotWidget()
        self._slip_plot.setLabel("left", "slip", units="deg")
        self._slip_plot.setFixedHeight(170)
        self._slip_plot.showGrid(x=True, y=True, alpha=0.25)
        self._slip_plot.setXLink(self._plot)
        self._slip_plot.addItem(pg.InfiniteLine(pos=0, angle=0, pen=pg.mkPen(theme.GRID_MAJ)))
        self._slip_curve = self._slip_plot.plot(pen=pg.mkPen(color=theme.MAGENTA, width=2))
        plots = QVBoxLayout()
        plots.setSpacing(2)
        plots.addWidget(self._plot, 1)
        plots.addWidget(self._slip_plot)
        body.addLayout(plots, 1)
        layout.addLayout(body, 1)

    # =========================================================================
    # Display filter
    # =========================================================================

    @Slot()
    def _on_disp_filter_changed(self):
        self._disp_filter_type = self._disp_filter.filter_type()
        # Reset filter state on change
        self._disp_ema_state = 0.0
        self._disp_sma_buf.clear()
        self._disp_median_buf.clear()
        self._disp_pade_history = [0.0, 0.0, 0.0]
        self._disp_pade_count = 0
        self._disp_bq_w0 = self._disp_bq_w1 = 0.0
        self._disp_bq_coeffs = None
        self._disp_nt_w0 = self._disp_nt_w1 = 0.0
        self._disp_nt_coeffs = None
        self._disp_holt_level = 0.0
        self._disp_holt_trend = 0.0
        self._disp_holt_init = False
        self._save_filter_settings()

    def _apply_display_filter(self, raw_vel: float) -> float:
        """Apply client-side filter to velocity value."""
        ft = self._disp_filter_type
        if ft == "None":
            return raw_vel

        if ft == "EMA":
            alpha = self._disp_filter.filter_param()
            if alpha == 0:
                self._disp_ema_state = raw_vel
            else:
                weight = (256 - alpha) / 256.0
                self._disp_ema_state += (raw_vel - self._disp_ema_state) * weight
            return self._disp_ema_state

        if ft == "SMA":
            win = self._disp_filter.filter_param()
            if self._disp_sma_buf.maxlen != win:
                self._disp_sma_buf = deque(self._disp_sma_buf, maxlen=win)
            self._disp_sma_buf.append(raw_vel)
            return sum(self._disp_sma_buf) / len(self._disp_sma_buf)

        if ft == "Median":
            win = self._disp_filter.filter_param()
            if self._disp_median_buf.maxlen != win:
                self._disp_median_buf = deque(self._disp_median_buf, maxlen=win)
            self._disp_median_buf.append(raw_vel)
            if len(self._disp_median_buf) >= 1:
                return statistics.median(self._disp_median_buf)
            return raw_vel

        if ft == "Padé":
            h = self._disp_pade_history
            h[0], h[1], h[2] = h[1], h[2], raw_vel
            self._disp_pade_count += 1
            if self._disp_pade_count < 3:
                return raw_vel
            d1 = h[1] - h[0]
            d2 = h[2] - h[1]
            curvature = d1 - d2
            if abs(curvature) < 1e-9:
                return raw_vel
            correction = (d1 * d2) / curvature
            # Conservative defaults for display: 50% gain, clamp ±50
            correction *= 0.5
            correction = max(-50.0, min(50.0, correction))
            return h[1] + correction

        if ft == "Butterworth":
            cut = self._disp_filter.filter_param()
            fs = DISP_SAMPLE_RATE_HZ
            # Recompute coefficients if needed
            key = ("bq", cut, fs)
            if self._disp_bq_coeffs is None or getattr(self, '_bq_key', None) != key:
                K = math.tan(math.pi * cut / fs)
                K2 = K * K
                sqrt2 = math.sqrt(2.0)
                norm = 1.0 / (1.0 + sqrt2 * K + K2)
                b0 = K2 * norm
                b1 = 2.0 * b0
                b2 = b0
                a1 = 2.0 * (K2 - 1.0) * norm
                a2 = (1.0 - sqrt2 * K + K2) * norm
                self._disp_bq_coeffs = (b0, b1, b2, a1, a2)
                self._bq_key = key
                self._disp_bq_w0 = self._disp_bq_w1 = 0.0
            b0, b1, b2, a1, a2 = self._disp_bq_coeffs
            y = b0 * raw_vel + self._disp_bq_w0
            self._disp_bq_w0 = b1 * raw_vel - a1 * y + self._disp_bq_w1
            self._disp_bq_w1 = b2 * raw_vel - a2 * y
            return y

        if ft == "Notch":
            ctr = self._disp_filter._nt_center_spin.value() if hasattr(self._disp_filter, '_nt_center_spin') else 25
            Q = self._disp_filter._nt_q_spin.value() if hasattr(self._disp_filter, '_nt_q_spin') else 5.0
            fs = DISP_SAMPLE_RATE_HZ
            key = ("nt", ctr, Q, fs)
            if self._disp_nt_coeffs is None or getattr(self, '_nt_key', None) != key:
                w0 = 2.0 * math.pi * ctr / fs
                alpha = math.sin(w0) / (2.0 * Q)
                norm = 1.0 / (1.0 + alpha)
                b0 = norm
                b1 = -2.0 * math.cos(w0) * norm
                b2 = norm
                a1 = -2.0 * math.cos(w0) * norm
                a2 = (1.0 - alpha) * norm
                self._disp_nt_coeffs = (b0, b1, b2, a1, a2)
                self._nt_key = key
                self._disp_nt_w0 = self._disp_nt_w1 = 0.0
            b0, b1, b2, a1, a2 = self._disp_nt_coeffs
            y = b0 * raw_vel + self._disp_nt_w0
            self._disp_nt_w0 = b1 * raw_vel - a1 * y + self._disp_nt_w1
            self._disp_nt_w1 = b2 * raw_vel - a2 * y
            return y

        if ft == "Holt":
            a = self._disp_filter._hl_alpha_spin.value() if hasattr(self._disp_filter, '_hl_alpha_spin') else 0.2
            b = self._disp_filter._hl_beta_spin.value() if hasattr(self._disp_filter, '_hl_beta_spin') else 0.05
            if not self._disp_holt_init:
                self._disp_holt_level = raw_vel
                self._disp_holt_trend = 0.0
                self._disp_holt_init = True
                return raw_vel
            prev = self._disp_holt_level
            self._disp_holt_level = a * raw_vel + (1.0 - a) * (self._disp_holt_level + self._disp_holt_trend)
            self._disp_holt_trend = b * (self._disp_holt_level - prev) + (1.0 - b) * self._disp_holt_trend
            return self._disp_holt_level + self._disp_holt_trend

        return raw_vel

    # =========================================================================
    # Settings persistence
    # =========================================================================

    def _save_filter_settings(self):
        s = QSettings()
        s.beginGroup("filters")
        s.setValue("disp_type", self._disp_filter.filter_type())
        s.setValue("disp_param", self._disp_filter.filter_param())
        s.endGroup()

    def _load_filter_settings(self):
        s = QSettings()
        s.beginGroup("filters")
        disp_type = s.value("disp_type", "None")
        disp_param = int(s.value("disp_param", 128))
        s.endGroup()

        self._disp_filter.set_filter(disp_type, disp_param)
        self._disp_filter_type = disp_type

    # =========================================================================
    # Plot updates
    # =========================================================================

    @Slot()
    def _on_trace_toggled(self):
        """Show/hide curves based on checkbox state and re-autorange."""
        for key, cb in self._trace_cbs.items():
            self._curves[key].setVisible(cb.isChecked())
        self._plot.enableAutoRange(axis='y')

    @Slot(dict)
    def restyle(self):
        """After a theme change: plot background, grid and the dial."""
        self._plot.setBackground(theme.BASE)
        self._grid.setPen(pg.mkPen(theme.GRID_MAJ))
        self._dial.update()

    def zero_slip(self):
        """The slip starts from here."""
        self._slip.reset()
        self._slip_deg.clear()
        self._slip_deg.extend([float("nan")] * len(self._timestamps))   # no line before the zero

    def update_drv_config(self, data: dict):
        if "encoder_ppr" in data:
            self._dial.set_cpr(int(data["encoder_ppr"]))
        fpr = int(data.get("full_steps_per_rev") or self._full_steps_per_rev)
        usteps = data.get("microsteps_per_rev") or (fpr * int(data["microsteps"]) if data.get("microsteps") else None)
        ppr = data.get("encoder_ppr")
        self._slip.set_geometry(usteps_per_rev=usteps, counts_per_rev=4 * int(ppr) if ppr else None)
        """Update full_steps_per_rev for RPM conversion on motor speed plot."""
        if "full_steps_per_rev" in data:
            self._full_steps_per_rev = data["full_steps_per_rev"]

    def _sample_time(self, data: dict) -> float:
        """Seconds since the first sample: the board's tick deltas (exact spacing), the PC's
        clock when there is no tick or the two disagree by half a second (a board reset, a
        long gap)."""
        now = time.monotonic()
        tick = data.get("tick")
        tick = tick if isinstance(tick, int) else None
        if self._t0 is None:
            self._t0 = now
            self._t = 0.0
        else:
            host_dt = now - self._last_host
            if tick is not None and self._last_tick is not None:
                dt = ((tick - self._last_tick) % self.TICK_WRAP) / 1e6
                self._t += dt if abs(dt - host_dt) < 0.5 else host_dt
            else:
                self._t += host_dt
        self._last_tick = tick
        self._last_host = now
        return self._t

    @Slot(dict)
    def update_data(self, data: dict):
        """Append one telemetry sample; the plots follow on the next redraw."""
        self._dial.update_telemetry(data)
        t = self._sample_time(data)
        self._timestamps.append(t)

        motor = data.get("motor", {})
        speed = motor.get("speed", motor.get("spd", 0)) if isinstance(motor, dict) else 0
        direction = motor.get("direction", motor.get("dir", 1)) if isinstance(motor, dict) else 1
        if direction == 0:  # CCW → negative
            speed = -speed
        self._motor_speed.append(speed)

        encoder = data.get("encoder", {})
        vel = encoder.get("velocity", encoder.get("vel", 0)) if isinstance(encoder, dict) else 0

        # Slip: the encoder against the commanded position (degrees of shaft). motor.enc_at_pos
        # is the count read at the same instant as the position (firmware 0.7); encoder.count
        # can be up to 50 ms apart from it, tens of degrees of false slip while turning.
        pos = motor.get("position") if isinstance(motor, dict) else None
        count = motor.get("enc_at_pos") if isinstance(motor, dict) else None
        if count is None:
            count = encoder.get("count") if isinstance(encoder, dict) else None
        if isinstance(pos, (int, float)) and isinstance(count, (int, float)):
            slip = self._slip.update(int(pos), int(count))
        else:
            slip = float("nan")
        self._slip_deg.append(slip)

        # Apply display-side filter
        filtered_vel = self._apply_display_filter(vel)
        self._encoder_velocity.append(filtered_vel)
        self._dirty = True

    @Slot()
    def _redraw(self):
        """Repaint the curves from the kept samples (the redraw timer; only after new data)."""
        if not self._dirty or not self._timestamps:
            return
        self._dirty = False
        ts = np.fromiter(self._timestamps, float)
        spd = np.fromiter(self._motor_speed, float)
        vel = np.fromiter(self._encoder_velocity, float)
        slip = np.fromiter(self._slip_deg, float)

        if math.isfinite(slip[-1]):
            steps = slip[-1] / (360.0 / self._full_steps_per_rev)
            self._slip_label.setText("slip %+7.2f deg (%+.2f steps)" % (slip[-1], steps))
        self._slip_curve.setData(ts, slip, connect="finite")

        # Motor RPM (steps/s * 60 / full_steps_per_rev), encoder RPM (ticks/s * 60 / CPR)
        self._curves["motor_rpm"].setData(ts, spd * (60.0 / self._full_steps_per_rev))
        self._curves["enc_rpm"].setData(ts, vel * (60.0 / ENCODER_CPR))
        self._curves["motor_spd"].setData(ts, spd)
        self._curves["enc_vel"].setData(ts, vel)

        # Rolling X window (10 / 30 / 60 s), data scrolls from right to left
        t = ts[-1]
        self._plot.setXRange(max(0, t - self._window_s), t, padding=0)

    @Slot()
    def clear(self):
        """Clear all data and reset plots."""
        self._t0 = None
        self._last_tick = None
        self._dirty = False
        self._timestamps.clear()
        self._motor_speed.clear()
        self._encoder_velocity.clear()
        for curve in self._curves.values():
            curve.setData([], [])
        self._slip.reset()
        self._slip_deg.clear()
        self._slip_curve.setData([], [])
        # Reset display filter state
        self._disp_ema_state = 0.0
        self._disp_sma_buf.clear()
        self._disp_median_buf.clear()
        self._disp_pade_history = [0.0, 0.0, 0.0]
        self._disp_pade_count = 0
        self._disp_bq_w0 = self._disp_bq_w1 = 0.0
        self._disp_bq_coeffs = None
        self._disp_nt_w0 = self._disp_nt_w1 = 0.0
        self._disp_nt_coeffs = None
        self._disp_holt_level = 0.0
        self._disp_holt_trend = 0.0
        self._disp_holt_init = False
