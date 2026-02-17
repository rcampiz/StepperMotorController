"""
Real-time graph panel for motor and encoder telemetry.

Plots motor speed and encoder velocity over time using pyqtgraph.
"""

from collections import deque
import time

from PySide6.QtWidgets import QWidget, QVBoxLayout
from PySide6.QtCore import Slot

import pyqtgraph as pg


# Match the dark theme of the rest of the app
pg.setConfigOptions(background="#1e1e1e", foreground="#d4d4d4", antialias=True)

# Encoder counts per revolution (quadrature mode: PPR × 4)
ENCODER_CPR = 4000  # 1000 PPR × 4 quadrature


class GraphPanel(QWidget):
    """Panel with real-time plots for motor speed and encoder velocity."""

    MAX_POINTS = 600  # ~60 seconds at 0.1s polling

    def __init__(self, parent=None):
        super().__init__(parent)

        self._t0 = None  # First sample timestamp (for relative X axis)
        self._timestamps = deque(maxlen=self.MAX_POINTS)
        self._motor_speed = deque(maxlen=self.MAX_POINTS)
        self._encoder_velocity = deque(maxlen=self.MAX_POINTS)

        self._setup_ui()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(4)

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
        self._encoder_velocity.append(vel)

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
