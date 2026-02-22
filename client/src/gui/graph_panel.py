"""
Real-time graph panel for motor and encoder telemetry.

Plots motor speed and encoder velocity over time using pyqtgraph.
Provides configurable multi-type filtering for both firmware and display sides.

Filter types:
  - None: raw passthrough
  - EMA (Exponential Moving Average): single-pole IIR low-pass, param=alpha (0-255)
  - SMA (Simple Moving Average): FIR window, param=window size (2-32)
  - Median (display-only): sorted window, returns middle value (spike rejection)
"""

from collections import deque
import statistics
import time

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QComboBox,
    QSpinBox, QSlider, QStackedWidget,
)
from PySide6.QtCore import Slot, Signal, Qt, QSettings
from PySide6.QtGui import QFont

import pyqtgraph as pg


# Match the dark theme of the rest of the app
pg.setConfigOptions(background="#1e1e1e", foreground="#d4d4d4", antialias=True)

# Encoder counts per revolution (quadrature mode: PPR × 4)
ENCODER_CPR = 4000  # 1000 PPR × 4 quadrature

# Filter type names (shared between firmware and display)
FW_FILTER_TYPES = ["None", "EMA", "SMA"]
DISP_FILTER_TYPES = ["None", "EMA", "SMA", "Median"]

# Firmware sampling rate for cutoff frequency calculation
FW_SAMPLE_RATE_HZ = 100  # encoder_task runs at 100 Hz
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

    def reset(self):
        """Reset to None filter."""
        self.blockSignals(True)
        self._type_combo.setCurrentIndex(0)
        self._param_stack.setCurrentIndex(0)
        self._ema_spin.setValue(128)
        self._sma_spin.setValue(8)
        if hasattr(self, '_med_spin'):
            self._med_spin.setValue(5)
        self._update_description()
        self.blockSignals(False)


class GraphPanel(QWidget):
    """Panel with real-time plots for motor speed and encoder velocity."""

    MAX_POINTS = 600  # ~60 seconds at 0.1s polling

    firmware_filter_changed = Signal(str)  # command string: "EMA 128" / "SMA 8" / "NONE"

    def __init__(self, parent=None):
        super().__init__(parent)

        self._t0 = None  # First sample timestamp (for relative X axis)
        self._timestamps = deque(maxlen=self.MAX_POINTS)
        self._motor_speed = deque(maxlen=self.MAX_POINTS)
        self._encoder_velocity = deque(maxlen=self.MAX_POINTS)

        # Display-side filter state
        self._disp_filter_type = "None"
        self._disp_ema_state = 0.0
        self._disp_sma_buf = deque(maxlen=32)
        self._disp_median_buf = deque(maxlen=15)

        self._setup_ui()
        self._load_filter_settings()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(4)

        # Firmware filter controls
        self._fw_filter = _FilterControlRow(
            "FW", FW_FILTER_TYPES, FW_SAMPLE_RATE_HZ)
        self._fw_filter.filter_changed.connect(self._on_fw_filter_changed)
        layout.addWidget(self._fw_filter)

        # Display filter controls
        self._disp_filter = _FilterControlRow(
            "Disp", DISP_FILTER_TYPES, DISP_SAMPLE_RATE_HZ)
        self._disp_filter.filter_changed.connect(self._on_disp_filter_changed)
        layout.addWidget(self._disp_filter)

        # Motor speed plot
        self._speed_plot = pg.PlotWidget(title="Motor Speed")
        self._speed_plot.setLabel("left", "Speed", units="steps/s")
        self._speed_plot.setLabel("bottom", "Time", units="s")
        self._speed_plot.showGrid(x=True, y=True, alpha=0.3)
        self._speed_curve = self._speed_plot.plot(
            pen=pg.mkPen(color="#ffcc00", width=2))
        layout.addWidget(self._speed_plot)

        # Encoder velocity plot with RPM right-hand axis
        self._vel_plot = pg.PlotWidget(title="Encoder Velocity")
        self._vel_plot.setLabel("left", "Velocity", units="ticks/s")
        self._vel_plot.setLabel("bottom", "Time", units="s")
        self._vel_plot.showGrid(x=True, y=True, alpha=0.3)
        self._vel_curve = self._vel_plot.plot(
            pen=pg.mkPen(color="#00ccff", width=2))

        # RPM right-hand axis (linked ViewBox)
        self._rpm_axis = pg.AxisItem("right")
        self._rpm_axis.setLabel("RPM", color="#ff9966")
        self._vel_plot.plotItem.layout.addItem(self._rpm_axis, 2, 3)
        self._rpm_vb = pg.ViewBox()
        self._rpm_axis.linkToView(self._rpm_vb)
        self._vel_plot.scene().addItem(self._rpm_vb)
        self._rpm_curve = pg.PlotCurveItem(
            pen=pg.mkPen(color="#ff9966", width=2, style=pg.QtCore.Qt.DashLine))
        self._rpm_vb.addItem(self._rpm_curve)

        # Keep RPM ViewBox geometry in sync with the main plot
        self._vel_plot.getViewBox().sigResized.connect(self._sync_rpm_viewbox)

        layout.addWidget(self._vel_plot)

        # Link X axes so zooming/panning stays in sync
        self._vel_plot.setXLink(self._speed_plot)

    # =========================================================================
    # Firmware filter
    # =========================================================================

    @Slot()
    def _on_fw_filter_changed(self):
        ft = self._fw_filter.filter_type()
        param = self._fw_filter.filter_param()
        if ft == "None":
            cmd = "NONE"
        elif ft == "EMA":
            cmd = f"EMA {param}"
        elif ft == "SMA":
            cmd = f"SMA {param}"
        else:
            cmd = "NONE"
        self.firmware_filter_changed.emit(cmd)
        self._save_filter_settings()

    def get_firmware_filter_command(self) -> str:
        """Get the current firmware filter as a command string for restore."""
        ft = self._fw_filter.filter_type()
        param = self._fw_filter.filter_param()
        if ft == "EMA":
            return f"EMA {param}"
        elif ft == "SMA":
            return f"SMA {param}"
        return "NONE"

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

        return raw_vel

    # =========================================================================
    # Settings persistence
    # =========================================================================

    def _save_filter_settings(self):
        s = QSettings()
        s.beginGroup("filters")
        s.setValue("fw_type", self._fw_filter.filter_type())
        s.setValue("fw_param", self._fw_filter.filter_param())
        s.setValue("disp_type", self._disp_filter.filter_type())
        s.setValue("disp_param", self._disp_filter.filter_param())
        s.endGroup()

    def _load_filter_settings(self):
        s = QSettings()
        s.beginGroup("filters")
        fw_type = s.value("fw_type", "None")
        fw_param = int(s.value("fw_param", 128))
        disp_type = s.value("disp_type", "None")
        disp_param = int(s.value("disp_param", 128))
        s.endGroup()

        self._fw_filter.set_filter(fw_type, fw_param)
        self._disp_filter.set_filter(disp_type, disp_param)
        self._disp_filter_type = disp_type

    # =========================================================================
    # Plot updates
    # =========================================================================

    def _sync_rpm_viewbox(self):
        """Keep RPM ViewBox aligned with the main encoder plot area."""
        self._rpm_vb.setGeometry(self._vel_plot.getViewBox().sceneBoundingRect())
        self._rpm_vb.linkedViewChanged(
            self._vel_plot.getViewBox(), self._rpm_vb.XAxis)

    @Slot(dict)
    def update_data(self, data: dict):
        """Append new telemetry sample and redraw plots."""
        now = time.monotonic()
        if self._t0 is None:
            self._t0 = now

        t = now - self._t0
        self._timestamps.append(t)

        motor = data.get("motor", {})
        speed = motor.get("speed", motor.get("spd", 0)) if isinstance(motor, dict) else 0
        self._motor_speed.append(speed)

        encoder = data.get("encoder", {})
        vel = encoder.get("velocity", encoder.get("vel", 0)) if isinstance(encoder, dict) else 0

        # Apply display-side filter
        filtered_vel = self._apply_display_filter(vel)
        self._encoder_velocity.append(filtered_vel)

        # Update curves
        ts = list(self._timestamps)
        vel_list = list(self._encoder_velocity)
        self._speed_curve.setData(ts, list(self._motor_speed))
        self._vel_curve.setData(ts, vel_list)

        # Update RPM curve (RPM = velocity * 60 / CPR)
        rpm_list = [v * 60.0 / ENCODER_CPR for v in vel_list]
        self._rpm_curve.setData(ts, rpm_list)

        # Sync X range so RPM axis follows panning
        self._rpm_vb.setXRange(*self._vel_plot.getViewBox().viewRange()[0],
                               padding=0)

    @Slot()
    def clear(self):
        """Clear all data and reset plots."""
        self._t0 = None
        self._timestamps.clear()
        self._motor_speed.clear()
        self._encoder_velocity.clear()
        self._speed_curve.setData([], [])
        self._vel_curve.setData([], [])
        self._rpm_curve.setData([], [])
        # Reset display filter state
        self._disp_ema_state = 0.0
        self._disp_sma_buf.clear()
        self._disp_median_buf.clear()
