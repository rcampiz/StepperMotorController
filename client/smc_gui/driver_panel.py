"""
Driver configuration panel.

KVAL fader sliders and motion parameter controls with live preview graphs.
"""

from PySide6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QLabel,
    QGroupBox,
    QPushButton,
    QSpinBox,
    QDoubleSpinBox,
    QSlider,
    QTabWidget,
)
from PySide6.QtCore import Qt, Slot, Signal
from PySide6.QtGui import QFont
import pyqtgraph as pg
import re


# powerSTEP01 register <-> physical unit conversion factors
ACC_DEC_FACTOR = 14.5519    # steps/s per raw ACC/DEC register unit
MAXSPD_FACTOR = 15.2588     # steps/s per raw MAXSPD register unit

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

FADER_STYLE_GREEN = FADER_STYLE.replace('#4a9eff', '#00cc66')


class DriverPanel(QWidget):
    """Panel for driver KVAL and motion parameter configuration."""

    refresh_requested = Signal()
    kval_apply_requested = Signal(str)
    param_apply_requested = Signal(list)
    maxspd_changed = Signal(int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._full_steps_per_rev = 200
        self._syncing = False
        self._setup_ui()

    # ========================================================================
    # Fader channel helper
    # ========================================================================

    def _create_fader_channel(self, name, description, min_val, max_val,
                              suffix, tick_interval, style=None):
        """Create a vertical fader with a synced spinbox.

        Returns (layout, slider, spinbox).
        """
        ch = QVBoxLayout()
        ch.setSpacing(4)

        name_lbl = QLabel(name)
        name_lbl.setAlignment(Qt.AlignCenter)
        name_lbl.setFont(QFont("Segoe UI", 9, QFont.Bold))
        ch.addWidget(name_lbl)

        slider = QSlider(Qt.Vertical)
        slider.setRange(min_val, max_val)
        slider.setValue(min_val)
        slider.setTickPosition(QSlider.TicksBothSides)
        slider.setTickInterval(tick_interval)
        slider.setMinimumHeight(120)
        slider.setFixedWidth(60)
        slider.setStyleSheet(style or FADER_STYLE)
        ch.addWidget(slider, alignment=Qt.AlignHCenter)

        spin = QSpinBox()
        spin.setRange(min_val, max_val)
        spin.setValue(min_val)
        spin.setSuffix(suffix)
        spin.setAlignment(Qt.AlignCenter)
        spin.setFont(QFont("Consolas", 9))
        spin.setMinimumWidth(70)
        ch.addWidget(spin)

        desc_lbl = QLabel(description)
        desc_lbl.setAlignment(Qt.AlignCenter)
        desc_lbl.setStyleSheet("font-size: 9px; color: #888;")
        ch.addWidget(desc_lbl)

        # Bidirectional sync (no loop: setValue is a no-op if value unchanged)
        slider.valueChanged.connect(spin.setValue)
        spin.valueChanged.connect(slider.setValue)

        return ch, slider, spin

    def _create_rpm_spinbox(self, min_val, max_val, suffix):
        """Create a QDoubleSpinBox for RPM / RPM/s display."""
        spin = QDoubleSpinBox()
        spin.setRange(min_val, max_val)
        spin.setDecimals(1)
        spin.setSuffix(suffix)
        spin.setAlignment(Qt.AlignCenter)
        spin.setFont(QFont("Consolas", 9))
        spin.setMinimumWidth(70)
        return spin

    # ========================================================================
    # Setup
    # ========================================================================

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setSpacing(12)

        layout.addWidget(self._create_kval_group())
        layout.addWidget(self._create_motion_params_group())
        layout.addStretch()

        # Cross-connect: motion params also affect the KVAL drive profile timing
        self._acc_spin.valueChanged.connect(self._update_kval_profile)
        self._dec_spin.valueChanged.connect(self._update_kval_profile)
        self._maxspd_spin.valueChanged.connect(self._update_kval_profile)
        self._update_kval_profile()

    # ========================================================================
    # KVAL fader group
    # ========================================================================

    def _create_kval_group(self) -> QGroupBox:
        group = QGroupBox("Current Drive (KVAL) \u2014 % of supply voltage")
        main_layout = QVBoxLayout(group)

        # Faders + apply button
        top_row = QHBoxLayout()
        faders = QHBoxLayout()
        faders.setSpacing(16)

        ch, self._kval_hold_slider, self._kval_hold_spin = \
            self._create_fader_channel("HOLD", "Holding\ncurrent",
                                       0, 100, "% Vs", 25)
        faders.addLayout(ch)

        ch, self._kval_run_slider, self._kval_run_spin = \
            self._create_fader_channel("RUN", "Running\ncurrent",
                                       0, 100, "% Vs", 25)
        faders.addLayout(ch)

        ch, self._kval_acc_slider, self._kval_acc_spin = \
            self._create_fader_channel("ACC", "Accel\nboost",
                                       0, 100, "% Vs", 25)
        faders.addLayout(ch)

        ch, self._kval_dec_slider, self._kval_dec_spin = \
            self._create_fader_channel("DEC", "Decel\nboost",
                                       0, 100, "% Vs", 25)
        faders.addLayout(ch)

        top_row.addLayout(faders)

        btn_layout = QVBoxLayout()
        btn_layout.addStretch()
        self._apply_kval_btn = QPushButton("Apply")
        self._apply_kval_btn.setToolTip("Send KVAL values to driver")
        self._apply_kval_btn.clicked.connect(self._on_apply_kval)
        btn_layout.addWidget(self._apply_kval_btn)
        btn_layout.addStretch()
        top_row.addLayout(btn_layout)

        main_layout.addLayout(top_row)

        # Tabbed graph area
        graph_tabs = QTabWidget()
        graph_tabs.setFixedHeight(220)
        graph_tabs.setStyleSheet("QTabBar::tab { padding: 2px 10px; }")

        # --- Tab 1: Drive Profile ---
        self._kval_profile_plot = self._create_mini_plot()
        pi = self._kval_profile_plot.getPlotItem()
        pi.showAxis('bottom')
        pi.getAxis('bottom').setStyle(tickFont=QFont("Consolas", 7))
        pi.getAxis('bottom').setPen('#666')
        pi.showAxis('left')
        pi.getAxis('left').setWidth(30)
        pi.getAxis('left').setStyle(tickFont=QFont("Consolas", 7))
        pi.getAxis('left').setPen('#666')
        self._kval_profile_plot.setLabel('left', '% Vs',
                                          **{'font-size': '8pt', 'color': '#888'})
        self._kval_profile_plot.setLabel('bottom', 'time (s)',
                                          **{'font-size': '8pt', 'color': '#888'})
        # Phase labels
        self._kval_profile_labels = []
        for text in ("HOLD", "ACC", "RUN", "DEC", "HOLD"):
            lbl = pg.TextItem(text, color='#aaa', anchor=(0.5, 1))
            self._kval_profile_plot.addItem(lbl)
            self._kval_profile_labels.append(lbl)
        # Phase separators
        self._kval_profile_seps = []
        for _ in range(4):
            sep = pg.InfiniteLine(pos=0, angle=90,
                                  pen=pg.mkPen('#444', width=1, style=Qt.DotLine))
            self._kval_profile_plot.addItem(sep)
            self._kval_profile_seps.append(sep)
        # Data curves
        self._kval_profile_curve = self._kval_profile_plot.plot(
            pen=pg.mkPen(color='#4a9eff', width=2), name="KVAL (% Vs)")
        self._kval_profile_fill = self._kval_profile_plot.plot(
            pen=pg.mkPen(None), brush=pg.mkBrush(74, 158, 255, 40), fillLevel=0)
        self._kval_speed_curve = self._kval_profile_plot.plot(
            pen=pg.mkPen(color='#00cc66', width=1.5, style=Qt.DashLine),
            name="Speed (norm.)")
        self._kval_profile_plot.addItem(
            pg.InfiniteLine(pos=0, angle=0, pen=pg.mkPen('#333', width=1)))
        # Legend (top-right)
        legend = self._kval_profile_plot.addLegend(
            offset=(10, 10), labelTextSize='8pt')
        legend.setParentItem(pi)
        graph_tabs.addTab(self._kval_profile_plot, "Drive Profile")

        # --- Tab 2: Coil Waveform (Phase A & B for CCW) ---
        self._coil_layout = pg.GraphicsLayoutWidget()
        self._coil_layout.setBackground('#1e1e1e')

        # Phase A plot (top)
        self._coil_a_plot = self._coil_layout.addPlot(row=0, col=0)
        self._coil_a_plot.setMouseEnabled(x=False, y=False)
        self._coil_a_plot.hideButtons()
        self._coil_a_plot.setMenuEnabled(False)
        self._coil_a_plot.setLabel('left', 'A', units=None,
                                    **{'font-size': '9pt', 'color': '#ffcc00'})
        self._coil_a_plot.getAxis('left').setWidth(28)
        self._coil_a_plot.getAxis('left').setStyle(
            tickFont=QFont("Consolas", 7), showValues=False)
        self._coil_a_plot.getAxis('left').setPen('#555')
        self._coil_a_plot.getAxis('bottom').setStyle(
            tickFont=QFont("Consolas", 7))
        self._coil_a_plot.getAxis('bottom').setPen('#555')
        self._coil_a_plot.showGrid(x=False, y=True, alpha=0.15)
        self._coil_a_curve = self._coil_a_plot.plot(
            pen=pg.mkPen(color='#ffcc00', width=1.5))
        self._coil_a_fill = self._coil_a_plot.plot(
            pen=pg.mkPen(None), brush=pg.mkBrush(255, 204, 0, 25),
            fillLevel=0)
        # Zero line
        self._coil_a_plot.addItem(
            pg.InfiniteLine(pos=0, angle=0,
                            pen=pg.mkPen('#444', width=1)))

        # Phase B plot (bottom)
        self._coil_b_plot = self._coil_layout.addPlot(row=1, col=0)
        self._coil_b_plot.setMouseEnabled(x=False, y=False)
        self._coil_b_plot.hideButtons()
        self._coil_b_plot.setMenuEnabled(False)
        self._coil_b_plot.setLabel('left', 'B', units=None,
                                    **{'font-size': '9pt', 'color': '#3399ff'})
        self._coil_b_plot.getAxis('left').setWidth(28)
        self._coil_b_plot.getAxis('left').setStyle(
            tickFont=QFont("Consolas", 7), showValues=False)
        self._coil_b_plot.getAxis('left').setPen('#555')
        self._coil_b_plot.getAxis('bottom').setStyle(
            tickFont=QFont("Consolas", 7))
        self._coil_b_plot.getAxis('bottom').setPen('#555')
        self._coil_b_plot.showGrid(x=False, y=True, alpha=0.15)
        self._coil_b_curve = self._coil_b_plot.plot(
            pen=pg.mkPen(color='#3399ff', width=1.5))
        self._coil_b_fill = self._coil_b_plot.plot(
            pen=pg.mkPen(None), brush=pg.mkBrush(51, 153, 255, 25),
            fillLevel=0)
        self._coil_b_plot.addItem(
            pg.InfiniteLine(pos=0, angle=0,
                            pen=pg.mkPen('#444', width=1)))

        # Link X axes so they scroll together
        self._coil_b_plot.setXLink(self._coil_a_plot)

        # Phase labels & separators (added to both plots)
        self._coil_phase_labels_a = []
        self._coil_phase_labels_b = []
        self._coil_separators_a = []
        self._coil_separators_b = []
        for text in ("HOLD", "ACC", "RUN", "DEC"):
            la = pg.TextItem(text, color='#aaa', anchor=(0.5, 1))
            la.setFont(QFont("Segoe UI", 8, QFont.Bold))
            self._coil_a_plot.addItem(la)
            self._coil_phase_labels_a.append(la)
            lb = pg.TextItem(text, color='#aaa', anchor=(0.5, 1))
            lb.setFont(QFont("Segoe UI", 8, QFont.Bold))
            self._coil_b_plot.addItem(lb)
            self._coil_phase_labels_b.append(lb)
        for _ in range(3):
            sa = pg.InfiniteLine(pos=0, angle=90,
                                  pen=pg.mkPen('#555', width=1, style=Qt.DotLine))
            self._coil_a_plot.addItem(sa)
            self._coil_separators_a.append(sa)
            sb = pg.InfiniteLine(pos=0, angle=90,
                                  pen=pg.mkPen('#555', width=1, style=Qt.DotLine))
            self._coil_b_plot.addItem(sb)
            self._coil_separators_b.append(sb)

        graph_tabs.addTab(self._coil_layout, "Coil Waveform")

        main_layout.addWidget(graph_tabs)

        # Connect sliders to graph updates
        for slider in (self._kval_hold_slider, self._kval_run_slider,
                        self._kval_acc_slider, self._kval_dec_slider):
            slider.valueChanged.connect(self._update_kval_profile)
            slider.valueChanged.connect(self._update_coil_waveform)

        self._update_kval_profile()
        self._update_coil_waveform()

        return group

    # ========================================================================
    # Motion Parameters group (vertical faders)
    # ========================================================================

    def _create_motion_params_group(self) -> QGroupBox:
        group = QGroupBox("Motion Parameters")
        main_layout = QVBoxLayout(group)

        # Faders + buttons
        top_row = QHBoxLayout()
        faders = QHBoxLayout()
        faders.setSpacing(16)

        f = self._full_steps_per_rev

        ch, self._acc_slider, self._acc_spin = \
            self._create_fader_channel("ACC", "Acceleration",
                                       15, 59590, " steps/s\u00b2", 10000,
                                       style=FADER_STYLE_GREEN)
        self._acc_rpm_spin = self._create_rpm_spinbox(
            15 * 60.0 / f, 59590 * 60.0 / f, " RPM/s")
        ch.insertWidget(3, self._acc_rpm_spin)
        faders.addLayout(ch)

        ch, self._dec_slider, self._dec_spin = \
            self._create_fader_channel("DEC", "Deceleration",
                                       15, 59590, " steps/s\u00b2", 10000,
                                       style=FADER_STYLE_GREEN)
        self._dec_rpm_spin = self._create_rpm_spinbox(
            15 * 60.0 / f, 59590 * 60.0 / f, " RPM/s")
        ch.insertWidget(3, self._dec_rpm_spin)
        faders.addLayout(ch)

        ch, self._maxspd_slider, self._maxspd_spin = \
            self._create_fader_channel("MAX SPD", "Max speed",
                                       15, 15609, " steps/s", 2500,
                                       style=FADER_STYLE_GREEN)
        self._maxspd_rpm_spin = self._create_rpm_spinbox(
            15 * 60.0 / f, 15609 * 60.0 / f, " RPM")
        ch.insertWidget(3, self._maxspd_rpm_spin)
        faders.addLayout(ch)

        top_row.addLayout(faders)

        btn_layout = QVBoxLayout()
        btn_layout.addStretch()
        self._apply_params_btn = QPushButton("Apply")
        self._apply_params_btn.setToolTip("Send motion parameters to driver")
        self._apply_params_btn.clicked.connect(self._on_apply_params)
        btn_layout.addWidget(self._apply_params_btn)
        self._refresh_btn = QPushButton("Refresh")
        self._refresh_btn.setToolTip("Read current driver parameters")
        self._refresh_btn.clicked.connect(self._on_refresh_clicked)
        btn_layout.addWidget(self._refresh_btn)
        btn_layout.addStretch()
        top_row.addLayout(btn_layout)

        main_layout.addLayout(top_row)

        # Speed profile graph
        self._motion_plot = self._create_mini_plot()
        self._motion_plot.setFixedHeight(110)
        mpi = self._motion_plot.getPlotItem()
        mpi.showAxis('left')
        mpi.getAxis('left').setWidth(45)
        mpi.getAxis('left').setStyle(tickFont=QFont("Consolas", 7))
        mpi.getAxis('left').setPen('#666')
        mpi.showAxis('bottom')
        mpi.getAxis('bottom').setStyle(tickFont=QFont("Consolas", 7))
        mpi.getAxis('bottom').setPen('#666')
        self._motion_plot.setLabel('left', 'steps/s',
                                   **{'font-size': '8pt', 'color': '#888'})
        self._motion_plot.setLabel('bottom', 'time (s)',
                                   **{'font-size': '8pt', 'color': '#888'})
        self._motion_curve = self._motion_plot.plot(
            pen=pg.mkPen(color='#00cc66', width=2))
        self._motion_fill = self._motion_plot.plot(
            pen=pg.mkPen(None), brush=pg.mkBrush(0, 204, 102, 50), fillLevel=0)
        main_layout.addWidget(self._motion_plot)

        # Connect spinboxes (which are synced to sliders) to profile updates
        self._acc_spin.valueChanged.connect(self._update_motion_profile)
        self._dec_spin.valueChanged.connect(self._update_motion_profile)
        self._maxspd_spin.valueChanged.connect(self._update_motion_profile)
        self._maxspd_spin.valueChanged.connect(self.maxspd_changed.emit)

        # Bidirectional sync: steps/s ↔ RPM
        self._acc_spin.valueChanged.connect(self._on_acc_steps_changed)
        self._acc_rpm_spin.valueChanged.connect(self._on_acc_rpm_changed)
        self._dec_spin.valueChanged.connect(self._on_dec_steps_changed)
        self._dec_rpm_spin.valueChanged.connect(self._on_dec_rpm_changed)
        self._maxspd_spin.valueChanged.connect(self._on_maxspd_steps_changed)
        self._maxspd_rpm_spin.valueChanged.connect(self._on_maxspd_rpm_changed)

        # Initial sync
        self._syncing = True
        self._acc_rpm_spin.setValue(
            self._acc_spin.value() * 60.0 / self._full_steps_per_rev)
        self._dec_rpm_spin.setValue(
            self._dec_spin.value() * 60.0 / self._full_steps_per_rev)
        self._maxspd_rpm_spin.setValue(
            self._maxspd_spin.value() * 60.0 / self._full_steps_per_rev)
        self._syncing = False

        self._update_motion_profile()

        return group

    # ========================================================================
    # Mini plot helper
    # ========================================================================

    def _create_mini_plot(self) -> pg.PlotWidget:
        plot = pg.PlotWidget()
        plot.setMouseEnabled(x=False, y=False)
        plot.hideButtons()
        plot.setMenuEnabled(False)
        plot.getPlotItem().hideAxis('bottom')
        plot.getPlotItem().hideAxis('left')
        plot.setBackground('#1e1e1e')
        return plot

    # ========================================================================
    # Graph update methods
    # ========================================================================

    def _update_kval_profile(self, _value=None):
        """Redraw the KVAL drive profile as accurate flat steps.

        Y-axis is fixed 0-100% Vs. Time axis driven by motion parameters.
        """
        hold = self._kval_hold_slider.value()
        run_ = self._kval_run_slider.value()
        acc_kval = self._kval_acc_slider.value()
        dec_kval = self._kval_dec_slider.value()

        try:
            acc_rate = max(self._acc_spin.value(), 1)
            dec_rate = max(self._dec_spin.value(), 1)
            maxspd = max(self._maxspd_spin.value(), 1)
        except AttributeError:
            return

        # Phase durations in seconds
        t_hold1 = 0.3
        t_acc = maxspd / acc_rate
        t_run = max(t_acc, 0.5)
        t_dec = maxspd / dec_rate
        t_hold2 = 0.3

        t0 = 0
        t1 = t_hold1
        t2 = t1 + t_acc
        t3 = t2 + t_run
        t4 = t3 + t_dec
        t5 = t4 + t_hold2

        # KVAL stepped profile
        kval_x = [t0, t1, t1, t2, t2, t3, t3, t4, t4, t5]
        kval_y = [hold, hold, acc_kval, acc_kval, run_, run_,
                  dec_kval, dec_kval, hold, hold]

        # Speed overlay normalized to 0-100 range
        spd_x = [t0, t1, t2, t3, t4, t5]
        spd_y = [0, 0, 100, 100, 0, 0]

        self._kval_profile_curve.setData(kval_x, kval_y)
        self._kval_profile_fill.setData(kval_x, kval_y)
        self._kval_speed_curve.setData(spd_x, spd_y)

        # Phase labels
        labels_pos = [
            (t0 + t1) / 2, (t1 + t2) / 2, (t2 + t3) / 2,
            (t3 + t4) / 2, (t4 + t5) / 2,
        ]
        labels_y = [hold, acc_kval, run_, dec_kval, hold]
        for i, (lx, ly) in enumerate(zip(labels_pos, labels_y)):
            self._kval_profile_labels[i].setPos(lx, min(ly + 8, 108))

        for i, tx in enumerate([t1, t2, t3, t4]):
            self._kval_profile_seps[i].setValue(tx)

        self._kval_profile_plot.setYRange(0, 115)
        self._kval_profile_plot.setXRange(0, t5 * 1.02)

    def _update_coil_waveform(self, _value=None):
        """Redraw Phase A/B rectangular waveforms for CCW full-step drive.

        Both coils produce +/-KVAL square waves. Phase B leads Phase A
        by 90 degrees (one quarter electrical cycle).

        Full-step pattern per electrical cycle (CCW):
          A: [+KVAL, -KVAL, -KVAL, +KVAL]
          B: [+KVAL, +KVAL, -KVAL, -KVAL]

        Step period varies by phase: static HOLD, ramping ACC/DEC,
        constant RUN.
        """
        kval_hold = self._kval_hold_slider.value()   # 0-100 %
        kval_acc = self._kval_acc_slider.value()
        kval_run = self._kval_run_slider.value()
        kval_dec = self._kval_dec_slider.value()

        # Full-step sign pattern per electrical cycle (CCW)
        sign_a = [+1, -1, -1, +1]
        sign_b = [+1, +1, -1, -1]

        a_x, a_y = [], []
        b_x, b_y = [], []
        phase_boundaries = []
        phase_centers = []
        t = 0.0
        step_idx = 0  # running index into full-step pattern

        def add_step(period, kval):
            """Advance one full step and draw a constant segment."""
            nonlocal t, step_idx
            a_val = kval * sign_a[step_idx % 4]
            b_val = kval * sign_b[step_idx % 4]
            a_x.extend([t, t + period])
            a_y.extend([a_val, a_val])
            b_x.extend([t, t + period])
            b_y.extend([b_val, b_val])
            t += period
            step_idx += 1

        # --- HOLD: motor stationary, coils energized at fixed step ---
        hold_start = t
        hold_duration = 3.0
        a_val = kval_hold * sign_a[step_idx % 4]
        b_val = kval_hold * sign_b[step_idx % 4]
        a_x.extend([t, t + hold_duration])
        a_y.extend([a_val, a_val])
        b_x.extend([t, t + hold_duration])
        b_y.extend([b_val, b_val])
        t += hold_duration
        phase_centers.append((hold_start + t) / 2)
        phase_boundaries.append(t)

        # --- ACC: step period decreasing (speeding up) ---
        acc_start = t
        acc_steps = 12
        for i in range(acc_steps):
            frac = i / max(acc_steps - 1, 1)
            period = 0.8 - 0.5 * frac   # 0.8 -> 0.3
            add_step(period, kval_acc)
        phase_centers.append((acc_start + t) / 2)
        phase_boundaries.append(t)

        # --- RUN: constant step period ---
        run_start = t
        run_steps = 16
        for _ in range(run_steps):
            add_step(0.3, kval_run)
        phase_centers.append((run_start + t) / 2)
        phase_boundaries.append(t)

        # --- DEC: step period increasing (slowing down) ---
        dec_start = t
        dec_steps = 12
        for i in range(dec_steps):
            frac = i / max(dec_steps - 1, 1)
            period = 0.3 + 0.5 * frac   # 0.3 -> 0.8
            add_step(period, kval_dec)
        phase_centers.append((dec_start + t) / 2)

        # Update curves
        self._coil_a_curve.setData(a_x, a_y)
        self._coil_a_fill.setData(a_x, a_y)
        self._coil_b_curve.setData(b_x, b_y)
        self._coil_b_fill.setData(b_x, b_y)

        # Y range
        y_max = max(kval_hold, kval_acc, kval_run, kval_dec, 10)
        y_lim = y_max * 1.15
        self._coil_a_plot.setYRange(-y_lim, y_lim)
        self._coil_b_plot.setYRange(-y_lim, y_lim)
        self._coil_a_plot.setXRange(0, t)

        # Phase labels & separators
        for i, cx in enumerate(phase_centers):
            self._coil_phase_labels_a[i].setPos(cx, y_lim * 0.92)
            self._coil_phase_labels_b[i].setPos(cx, y_lim * 0.92)
        for i, bx in enumerate(phase_boundaries):
            self._coil_separators_a[i].setValue(bx)
            self._coil_separators_b[i].setValue(bx)

    def _update_motion_profile(self):
        """Redraw the trapezoidal speed profile."""
        acc = max(self._acc_spin.value(), 1)
        dec = max(self._dec_spin.value(), 1)
        maxspd = self._maxspd_spin.value()

        t_acc = maxspd / acc
        t_cruise = max(t_acc, 0.5)
        t_dec = maxspd / dec

        x = [0, t_acc, t_acc + t_cruise, t_acc + t_cruise + t_dec]
        y = [0, maxspd, maxspd, 0]

        self._motion_curve.setData(x, y)
        self._motion_fill.setData(x, y)
        self._motion_plot.setYRange(0, maxspd * 1.15 if maxspd > 0 else 100)
        self._motion_plot.setXRange(0, x[-1] * 1.05 if x[-1] > 0 else 1)

    # ========================================================================
    # Apply / Refresh handlers
    # ========================================================================

    @Slot()
    def _on_apply_kval(self):
        h = round(self._kval_hold_slider.value() / 100 * 255)
        r = round(self._kval_run_slider.value() / 100 * 255)
        a = round(self._kval_acc_slider.value() / 100 * 255)
        d = round(self._kval_dec_slider.value() / 100 * 255)
        self.kval_apply_requested.emit(
            f"DRV:CFG:KVAL {h:02X} {r:02X} {a:02X} {d:02X}")

    @Slot()
    def _on_apply_params(self):
        acc_raw = max(1, round(self._acc_spin.value() / ACC_DEC_FACTOR))
        dec_raw = max(1, round(self._dec_spin.value() / ACC_DEC_FACTOR))
        maxspd_raw = max(1, round(self._maxspd_spin.value() / MAXSPD_FACTOR))
        self.param_apply_requested.emit([
            f"DRV:CFG:MOTION {acc_raw} {dec_raw} {maxspd_raw}",
            "DRV:CFG:APPLY",
        ])

    @Slot()
    def _on_refresh_clicked(self):
        self.refresh_requested.emit()

    def update_driver_params(self, response: str):
        """Parse and update from MOTOR_DEBUG response."""
        kval_match = re.search(
            r'KVAL:\s*HOLD=([0-9A-Fa-f]+)\s*RUN=([0-9A-Fa-f]+)'
            r'\s*ACC=([0-9A-Fa-f]+)\s*DEC=([0-9A-Fa-f]+)', response)
        if kval_match:
            self._kval_hold_slider.setValue(
                round(int(kval_match.group(1), 16) / 255 * 100))
            self._kval_run_slider.setValue(
                round(int(kval_match.group(2), 16) / 255 * 100))
            self._kval_acc_slider.setValue(
                round(int(kval_match.group(3), 16) / 255 * 100))
            self._kval_dec_slider.setValue(
                round(int(kval_match.group(4), 16) / 255 * 100))

        motion_match = re.search(r'ACC=(\d+)\s+DEC=(\d+)\s+MAXSPD=(\d+)', response)
        if motion_match:
            self._acc_spin.setValue(
                round(int(motion_match.group(1)) * ACC_DEC_FACTOR))
            self._dec_spin.setValue(
                round(int(motion_match.group(2)) * ACC_DEC_FACTOR))
            self._maxspd_spin.setValue(
                round(int(motion_match.group(3)) * MAXSPD_FACTOR))

    @Slot()
    def clear(self):
        for slider in (self._kval_hold_slider, self._kval_run_slider,
                        self._kval_acc_slider, self._kval_dec_slider):
            slider.setValue(0)
        self._acc_spin.setValue(15)
        self._dec_spin.setValue(15)
        self._maxspd_spin.setValue(15)

    # ========================================================================
    # Unit sync: steps/s ↔ RPM (bidirectional)
    # ========================================================================

    def _on_acc_steps_changed(self, val):
        if self._syncing:
            return
        self._syncing = True
        self._acc_rpm_spin.setValue(val * 60.0 / self._full_steps_per_rev)
        self._syncing = False

    def _on_acc_rpm_changed(self, val):
        if self._syncing:
            return
        self._syncing = True
        self._acc_spin.setValue(round(val * self._full_steps_per_rev / 60.0))
        self._syncing = False

    def _on_dec_steps_changed(self, val):
        if self._syncing:
            return
        self._syncing = True
        self._dec_rpm_spin.setValue(val * 60.0 / self._full_steps_per_rev)
        self._syncing = False

    def _on_dec_rpm_changed(self, val):
        if self._syncing:
            return
        self._syncing = True
        self._dec_spin.setValue(round(val * self._full_steps_per_rev / 60.0))
        self._syncing = False

    def _on_maxspd_steps_changed(self, val):
        if self._syncing:
            return
        self._syncing = True
        self._maxspd_rpm_spin.setValue(val * 60.0 / self._full_steps_per_rev)
        self._syncing = False

    def _on_maxspd_rpm_changed(self, val):
        if self._syncing:
            return
        self._syncing = True
        self._maxspd_spin.setValue(
            round(val * self._full_steps_per_rev / 60.0))
        self._syncing = False

    @Slot(dict)
    def update_drv_config(self, data: dict):
        """Update motor configuration (full_steps_per_rev) for RPM conversion."""
        if "full_steps_per_rev" in data:
            self._full_steps_per_rev = data["full_steps_per_rev"]
            f = self._full_steps_per_rev
            # Update RPM spinbox ranges
            self._acc_rpm_spin.setRange(15 * 60.0 / f, 59590 * 60.0 / f)
            self._dec_rpm_spin.setRange(15 * 60.0 / f, 59590 * 60.0 / f)
            self._maxspd_rpm_spin.setRange(15 * 60.0 / f, 15609 * 60.0 / f)
            # Re-sync current values
            self._syncing = True
            self._acc_rpm_spin.setValue(
                self._acc_spin.value() * 60.0 / f)
            self._dec_rpm_spin.setValue(
                self._dec_spin.value() * 60.0 / f)
            self._maxspd_rpm_spin.setValue(
                self._maxspd_spin.value() * 60.0 / f)
            self._syncing = False
