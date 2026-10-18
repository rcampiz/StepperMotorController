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
    QComboBox,
    QCheckBox,
)
from PySide6.QtCore import Qt, Slot, Signal, QSettings

from smc_core import commands as C
from smc_core import units as U
from PySide6.QtGui import QFont
import pyqtgraph as pg


# powerSTEP01 register <-> physical unit conversion factors
ACC_DEC_FACTOR = U.ACC_DEC_FACTOR    # steps/s^2 per raw ACC/DEC register unit
MAXSPD_FACTOR = U.MAXSPD_FACTOR      # steps/s per raw MAXSPD register unit

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

# Fader limits for current mode: the chip's TVAL goes to 0x7F (9.9 A) but the motor's
# nameplate current is the real ceiling, so the faders stop at a user-set rating.
MOTOR_RATED_A_DEFAULT = 2.8          # 23HS22-2804-ME1K, the project's motor
MOTOR_RATED_A_MAX = round(U.tval_to_amps(0x7F), 2)

KVAL_TIPS = {
    "HOLD": "Holding: duty cycle applied while the motor stands still (KVAL_HOLD).\n"
            "Keep it low: holding current is pure heat. 0-100 % of the supply voltage.",
    "RUN": "Running: duty cycle at constant speed (KVAL_RUN). The coil current is\n"
           "roughly this fraction of the supply voltage over the coil resistance.",
    "ACC": "Acceleration boost: duty cycle while speeding up (KVAL_ACC). Usually at\n"
           "or a little above RUN, because torque is needed to accelerate the load.",
    "DEC": "Deceleration boost: duty cycle while slowing down (KVAL_DEC).",
}
TVAL_TIPS = {
    "HOLD": "Holding current: regulated coil current while the motor stands still (TVAL_HOLD).\n"
            "Pure heat with no motion: 30-50 % of the rated current is typical.\n"
            "78 mA per step; the fader stops at the motor rating set on the right.",
    "RUN": "Running current: regulated coil current at constant speed (TVAL_RUN).\n"
           "Up to the motor's nameplate (phase) current; the chip would allow 9.9 A,\n"
           "the fader stops at the motor rating set on the right.",
    "ACC": "Acceleration current: regulated coil current while speeding up (TVAL_ACC).\n"
           "At or a little above RUN; never above the motor rating.",
    "DEC": "Deceleration current: regulated coil current while slowing down (TVAL_DEC).",
}


class DriverPanel(QWidget):
    """Panel for driver KVAL and motion parameter configuration."""

    refresh_requested = Signal()
    kval_apply_requested = Signal(object)     # Request
    drive_apply_requested = Signal(list)      # [DRV:CFG:DRIVE, DRV:CFG:TVAL, DRV:CFG:APPLY]
    param_apply_requested = Signal(list)      # [Request]
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

    def _create_current_channel(self, name, description, tooltip):
        """Vertical fader in TVAL units (78 mA each) with a synced spinbox in amps.

        Returns (layout, slider, spinbox); the slider's value is the raw TVAL (0-127).
        """
        ch = QVBoxLayout()
        ch.setSpacing(4)

        name_lbl = QLabel(name + "  \u24d8")
        name_lbl.setAlignment(Qt.AlignCenter)
        name_lbl.setFont(QFont("Segoe UI", 9, QFont.Bold))
        name_lbl.setToolTip(tooltip)
        ch.addWidget(name_lbl)

        slider = QSlider(Qt.Vertical)
        slider.setRange(0, 0x7F)
        slider.setValue(0)
        slider.setTickPosition(QSlider.TicksBothSides)
        slider.setTickInterval(13)                      # ~1 A
        slider.setMinimumHeight(120)
        slider.setFixedWidth(60)
        slider.setStyleSheet(FADER_STYLE_GREEN)
        slider.setToolTip(tooltip)
        ch.addWidget(slider, alignment=Qt.AlignHCenter)

        spin = QDoubleSpinBox()
        spin.setRange(0.0, MOTOR_RATED_A_MAX)
        spin.setDecimals(2)
        spin.setSingleStep(U.TVAL_AMPS_PER_UNIT)
        spin.setSuffix(" A")
        spin.setAlignment(Qt.AlignCenter)
        spin.setFont(QFont("Consolas", 9))
        spin.setMinimumWidth(70)
        spin.setToolTip(tooltip)
        ch.addWidget(spin)

        desc_lbl = QLabel(description)
        desc_lbl.setAlignment(Qt.AlignCenter)
        desc_lbl.setStyleSheet("font-size: 9px; color: #888;")
        ch.addWidget(desc_lbl)

        # Sync both ways through the raw unit, so the spinbox always shows a reachable value
        slider.valueChanged.connect(lambda raw: spin.setValue(U.tval_to_amps(raw)))
        spin.valueChanged.connect(lambda amps: slider.setValue(U.tval_from_amps(amps)))
        return ch, slider, spin

    def _create_scaled_fader(self, name, description, min_raw, max_raw, scale, default_raw,
                             tooltip):
        """Vertical fader over a raw register range with a spinbox in physical units
        (value = raw x scale). Returns (layout, slider, spinbox); the slider holds the raw value.
        """
        ch = QVBoxLayout()
        ch.setSpacing(4)
        name_lbl = QLabel(name + "  \u24d8")
        name_lbl.setAlignment(Qt.AlignCenter)
        name_lbl.setFont(QFont("Segoe UI", 9, QFont.Bold))
        name_lbl.setToolTip(tooltip)
        ch.addWidget(name_lbl)
        slider = QSlider(Qt.Vertical)
        slider.setRange(min_raw, max_raw)
        slider.setValue(default_raw)
        slider.setTickPosition(QSlider.TicksBothSides)
        slider.setTickInterval(max(1, (max_raw - min_raw) // 8))
        slider.setMinimumHeight(100)
        slider.setFixedWidth(60)
        slider.setStyleSheet(FADER_STYLE_GREEN)
        slider.setToolTip(tooltip)
        ch.addWidget(slider, alignment=Qt.AlignHCenter)
        spin = QDoubleSpinBox()
        spin.setRange(min_raw * scale, max_raw * scale)
        spin.setDecimals(1 if scale < 1 else 0)
        spin.setSingleStep(scale)
        spin.setValue(default_raw * scale)
        spin.setSuffix(" \u00b5s")
        spin.setAlignment(Qt.AlignCenter)
        spin.setFont(QFont("Consolas", 9))
        spin.setMinimumWidth(70)
        spin.setToolTip(tooltip)
        ch.addWidget(spin)
        desc_lbl = QLabel(description)
        desc_lbl.setAlignment(Qt.AlignCenter)
        desc_lbl.setStyleSheet("font-size: 9px; color: #888;")
        ch.addWidget(desc_lbl)
        slider.valueChanged.connect(lambda raw: spin.setValue(raw * scale))
        spin.valueChanged.connect(lambda v: slider.setValue(int(round(v / scale))))
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
        for slider in self._tval_sliders:
            slider.valueChanged.connect(self._update_kval_profile)
        self._on_drive_mode_changed(self._drive_mode_combo.currentIndex())

    # ========================================================================
    # KVAL fader group
    # ========================================================================

    def _create_kval_group(self) -> QGroupBox:
        group = QGroupBox("Coil Drive")
        self._drive_group = group
        main_layout = QVBoxLayout(group)

        # Faders + apply button
        top_row = QHBoxLayout()
        faders = QHBoxLayout()
        faders.setSpacing(16)

        ch, self._kval_hold_slider, self._kval_hold_spin = \
            self._create_fader_channel("HOLD", "Holding\ncurrent",
                                       0, 100, "% Vs", 25)
        faders.addLayout(ch)
        for w in (self._kval_hold_slider, self._kval_hold_spin):
            w.setToolTip(KVAL_TIPS["HOLD"])

        ch, self._kval_run_slider, self._kval_run_spin = \
            self._create_fader_channel("RUN", "Running\ncurrent",
                                       0, 100, "% Vs", 25)
        faders.addLayout(ch)
        for w in (self._kval_run_slider, self._kval_run_spin):
            w.setToolTip(KVAL_TIPS["RUN"])

        ch, self._kval_acc_slider, self._kval_acc_spin = \
            self._create_fader_channel("ACC", "Accel\nboost",
                                       0, 100, "% Vs", 25)
        faders.addLayout(ch)
        for w in (self._kval_acc_slider, self._kval_acc_spin):
            w.setToolTip(KVAL_TIPS["ACC"])

        ch, self._kval_dec_slider, self._kval_dec_spin = \
            self._create_fader_channel("DEC", "Decel\nboost",
                                       0, 100, "% Vs", 25)
        faders.addLayout(ch)
        for w in (self._kval_dec_slider, self._kval_dec_spin):
            w.setToolTip(KVAL_TIPS["DEC"])

        self._kval_row = QWidget()
        self._kval_row.setLayout(faders)
        top_row.addWidget(self._kval_row)

        btn_layout = QVBoxLayout()
        btn_layout.addStretch()
        self._apply_kval_btn = QPushButton("Apply KVAL")
        self._apply_kval_btn.setToolTip("Send the KVAL duty cycles to the driver (used in voltage mode)")
        self._apply_kval_btn.clicked.connect(self._on_apply_kval)
        btn_layout.addWidget(self._apply_kval_btn)
        btn_layout.addStretch()
        top_row.addLayout(btn_layout)

        main_layout.addLayout(top_row)

        # Drive mode (powerSTEP01 CM_VM) + current-mode references. Current mode regulates the
        # coil current directly (smoother torque at speed); the KVAL faders above apply in
        # voltage mode, the TVAL faders here in current mode. Takes effect on Apply (motor idle).
        mode_row = QHBoxLayout()
        mode_lbl = QLabel("Drive mode:")
        mode_lbl.setFont(QFont("Segoe UI", 9, QFont.Bold))
        mode_row.addWidget(mode_lbl)
        self._drive_mode_combo = QComboBox()
        self._drive_mode_combo.addItems(["Voltage (KVAL, % of supply)", "Current (TVAL, amps)"])
        self._drive_mode_combo.setCurrentIndex(1)       # current mode is the firmware default
        self._drive_mode_combo.setToolTip(
            "Voltage mode drives the coils with a duty cycle (KVAL); current mode regulates the coil "
            "current to the TVAL references (powerSTEP01 CM_VM bit, written in Hi-Z on Apply).\n"
            "The faders of the inactive mode are greyed out; the graph follows the active mode.")
        self._drive_mode_combo.currentIndexChanged.connect(self._on_drive_mode_changed)
        mode_row.addWidget(self._drive_mode_combo)
        mode_row.addSpacing(24)
        self._adc_label = QLabel("ADCIN raw: --")
        self._adc_label.setFont(QFont("Consolas", 9))
        self._adc_label.setToolTip(
            "powerSTEP01 ADC_OUT: a 5-bit reading of the ADCIN pin. On the IHM03A1 it is not a "
            "supply divider (2 counts at 12 V and at 24 V on the bench), so it is shown raw. "
            "CONFIG is the chip's live register (PRED_EN is bit 15). Updated by Refresh.")
        mode_row.addWidget(self._adc_label)
        mode_row.addStretch()
        main_layout.addLayout(mode_row)

        tval_row = QHBoxLayout()
        tfaders = QHBoxLayout()
        tfaders.setSpacing(16)
        self._tval_sliders = []
        self._tval_spins = []
        for name, desc in (("HOLD", "Holding\ncurrent"), ("RUN", "Running\ncurrent"),
                           ("ACC", "Accel\ncurrent"), ("DEC", "Decel\ncurrent")):
            ch, slider, spin = self._create_current_channel(name, desc, TVAL_TIPS[name])
            tfaders.addLayout(ch)
            self._tval_sliders.append(slider)
            self._tval_spins.append(spin)
        self._tval_row = QWidget()
        self._tval_row.setLayout(tfaders)
        tval_row.addWidget(self._tval_row)
        tbtn = QVBoxLayout()
        tbtn.addStretch()
        self._apply_drive_btn = QPushButton("Apply mode + TVAL")
        self._apply_drive_btn.setToolTip(
            "DRV:CFG:DRIVE, DRV:CFG:TVAL, DRV:CFG:APPLY (needs the motor idle)")
        self._apply_drive_btn.clicked.connect(self._on_apply_drive)
        tbtn.addWidget(self._apply_drive_btn)
        rating_row = QHBoxLayout()
        rating_lbl = QLabel("Motor rated current:")
        rating_lbl.setToolTip("The motor's nameplate phase current. Caps the TVAL faders; remembered "
                              "on this PC, never sent to the board.")
        rating_row.addWidget(rating_lbl)
        self._rated_spin = QDoubleSpinBox()
        self._rated_spin.setRange(0.1, MOTOR_RATED_A_MAX)
        self._rated_spin.setDecimals(2)
        self._rated_spin.setSingleStep(0.1)
        self._rated_spin.setSuffix(" A")
        self._rated_spin.setFont(QFont("Consolas", 9))
        self._rated_spin.setToolTip(rating_lbl.toolTip())
        try:
            rated = float(QSettings().value("driver/motor_rated_a", MOTOR_RATED_A_DEFAULT))
        except (TypeError, ValueError):
            rated = MOTOR_RATED_A_DEFAULT
        self._rated_spin.setValue(min(max(rated, 0.1), MOTOR_RATED_A_MAX))
        self._rated_spin.valueChanged.connect(self._on_rated_current_changed)
        rating_row.addWidget(self._rated_spin)
        rating_row.addStretch()
        tbtn.addLayout(rating_row)
        tnote = QLabel("TVAL regulates the coil current in 78 mA steps (0.1 \u03a9 sense);\n"
                       "firmware defaults (23HS22-2804): 1.0 A hold, 2.0 A run, 2.3 A acc / dec")
        tnote.setStyleSheet("font-size: 8px; color: #888;")
        tbtn.addWidget(tnote)
        tbtn.addStretch()
        tval_row.addLayout(tbtn)
        main_layout.addLayout(tval_row)

        # Current-mode regulation: how the chip chops the current (PRED_EN/TSW in CONFIG,
        # Hi-Z on Apply; T_FAST, TON_MIN, TOFF_MIN with the motor stopped). Faders in
        # microseconds over the raw register ranges; chip defaults unless changed. The
        # resonance_sweep scenario (Tests tab) tells whether a setting helped.
        self._cm_row = QWidget()
        cm_v = QVBoxLayout(self._cm_row)
        cm_v.setContentsMargins(0, 0, 0, 0)
        head = QHBoxLayout()
        cm_lbl = QLabel("Current regulation:")
        cm_lbl.setFont(QFont("Segoe UI", 9, QFont.Bold))
        head.addWidget(cm_lbl)
        self._pred_cb = QCheckBox("Predictive current control")
        self._pred_cb.setChecked(True)                    # firmware default
        self._pred_cb.setToolTip("PRED_EN: the chip predicts the next PWM period from the last one "
                                 "so the current lands on the reference instead of overshooting it. "
                                 "Firmware default on (bench 2026-10-18: less ripple at every speed).")
        head.addWidget(self._pred_cb)
        head.addStretch()
        cm_v.addLayout(head)

        cm = QHBoxLayout()
        faders2 = QHBoxLayout()
        faders2.setSpacing(16)
        ch, self._tsw_slider, self._tsw_spin = self._create_scaled_fader(
            "TSW", "Switching\nperiod", 1, 32, 4.0, 48,
            "TSW: target switching period, 4 to 128 \u00b5s in 4 \u00b5s steps (48 \u00b5s = 21 kHz "
            "default). Shorter = higher chopper frequency, finer current control, more switching heat.")
        faders2.addLayout(ch)
        ch, self._fast_step_slider, self._fast_step_spin = self._create_scaled_fader(
            "FAST STEP", "Fast decay\nstep", 1, 16, 2.0, 4,
            "T_FAST[7:4] FAST_STEP: longest fast-decay step when the current must fall quickly "
            "(default 4 \u00b5s).")
        faders2.addLayout(ch)
        ch, self._toff_fast_slider, self._toff_fast_spin = self._create_scaled_fader(
            "FAST DECAY", "Fast decay\ntime", 1, 16, 2.0, 20,
            "T_FAST[3:0] TOFF_FAST: fast-decay time at the start of each off period (default "
            "20 \u00b5s). More fast decay follows a falling sine better, at the cost of ripple.")
        faders2.addLayout(ch)
        ch, self._ton_min_slider, self._ton_min_spin = self._create_scaled_fader(
            "MIN ON", "Shortest\non time", 1, 128, 0.5, 42,
            "TON_MIN: shortest on time of a PWM period, 0.5 to 64 \u00b5s (default 21 \u00b5s).")
        faders2.addLayout(ch)
        ch, self._toff_min_slider, self._toff_min_spin = self._create_scaled_fader(
            "MIN OFF", "Shortest\noff time", 1, 128, 0.5, 42,
            "TOFF_MIN: shortest off time of a PWM period, 0.5 to 64 \u00b5s (default 21 \u00b5s).")
        faders2.addLayout(ch)
        cm.addLayout(faders2)
        cbtn = QVBoxLayout()
        cbtn.addStretch()
        self._apply_cm_btn = QPushButton("Apply regulation")
        self._apply_cm_btn.setToolTip("DRV:CFG:PRED, DRV:CFG:DECAY, DRV:CFG:APPLY (needs the motor idle)")
        self._apply_cm_btn.clicked.connect(self._on_apply_cm)
        cbtn.addWidget(self._apply_cm_btn)
        self._cm_defaults_btn = QPushButton("Chip defaults")
        self._cm_defaults_btn.setToolTip("Set the faders to the powerSTEP01 defaults (48, 4, 20, 21, 21 \u00b5s); "
                                         "Apply to send them.")
        self._cm_defaults_btn.clicked.connect(self._on_cm_defaults)
        cbtn.addWidget(self._cm_defaults_btn)
        cnote = QLabel("Chopper timing in \u00b5s over the raw register ranges;\n"
                       "run the resonance_sweep scenario to compare settings by numbers")
        cnote.setStyleSheet("font-size: 8px; color: #888;")
        cbtn.addWidget(cnote)
        cbtn.addStretch()
        cm.addLayout(cbtn)
        cm_v.addLayout(cm)
        main_layout.addWidget(self._cm_row)
        self._on_rated_current_changed(self._rated_spin.value())

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

    def _is_current_mode(self) -> bool:
        return self._drive_mode_combo.currentIndex() == 1

    @Slot(int)
    def _on_drive_mode_changed(self, index: int):
        """Grey out the faders of the inactive mode and redraw the profile in its unit."""
        current = index == 1
        self._tval_row.setEnabled(current)
        self._cm_row.setEnabled(current)
        self._kval_row.setEnabled(not current)
        self._apply_kval_btn.setEnabled(not current)   # never send the idle faders' zeros
        self._drive_group.setTitle("Coil Drive \u2014 current mode: regulated coil current (TVAL, amps)"
                                   if current else
                                   "Coil Drive \u2014 voltage mode: duty cycle (KVAL, % of supply voltage)")
        self._update_kval_profile()

    @Slot(float)
    def _on_rated_current_changed(self, amps: float):
        """Cap the TVAL faders at the motor's rated current and remember it on this PC."""
        cap = U.tval_from_amps(amps)
        for slider, spin in zip(self._tval_sliders, self._tval_spins):
            slider.setMaximum(max(cap, 1))
            spin.setMaximum(U.tval_to_amps(max(cap, 1)))
        try:
            QSettings().setValue("driver/motor_rated_a", float(amps))
        except Exception:
            pass
        self._update_kval_profile()

    def _update_kval_profile(self, _value=None):
        """Redraw the drive profile as flat steps: KVAL in % Vs (voltage mode) or TVAL in
        amps (current mode). Time axis driven by the motion parameters."""
        try:
            current = self._is_current_mode()
        except AttributeError:
            return
        if current:
            hold, run_, acc_kval, dec_kval = (U.tval_to_amps(sl.value()) for sl in self._tval_sliders)
            full = max(self._rated_spin.value(), 0.1)
            unit = "A"
        else:
            hold = self._kval_hold_slider.value()
            run_ = self._kval_run_slider.value()
            acc_kval = self._kval_acc_slider.value()
            dec_kval = self._kval_dec_slider.value()
            full = 100.0
            unit = "% Vs"

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

        # Speed overlay normalized to the full scale of the active unit
        spd_x = [t0, t1, t2, t3, t4, t5]
        spd_y = [0, 0, full, full, 0, 0]

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
            self._kval_profile_labels[i].setPos(lx, min(ly + 0.08 * full, 1.08 * full))

        for i, tx in enumerate([t1, t2, t3, t4]):
            self._kval_profile_seps[i].setValue(tx)

        self._kval_profile_plot.setLabel('left', unit, **{'font-size': '8pt', 'color': '#888'})
        self._kval_profile_curve.opts['name'] = "TVAL (A)" if current else "KVAL (% Vs)"
        self._kval_profile_plot.setYRange(0, 1.15 * full)
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
        self.kval_apply_requested.emit(C.cfg_kval(
            U.kval_from_percent(self._kval_hold_slider.value()),
            U.kval_from_percent(self._kval_run_slider.value()),
            U.kval_from_percent(self._kval_acc_slider.value()),
            U.kval_from_percent(self._kval_dec_slider.value())))

    @Slot()
    def _on_apply_drive(self):
        self.drive_apply_requested.emit([
            C.cfg_drive_mode(self._is_current_mode()),
            C.cfg_tval(*(sl.value() for sl in self._tval_sliders)),
            C.cfg_apply(),
        ])

    @Slot()
    def _on_cm_defaults(self):
        self._pred_cb.setChecked(True)
        self._tsw_spin.setValue(48)
        self._fast_step_spin.setValue(4)
        self._toff_fast_spin.setValue(20)
        self._ton_min_spin.setValue(21.0)
        self._toff_min_spin.setValue(21.0)

    @Slot()
    def _on_apply_cm(self):
        self.drive_apply_requested.emit([
            C.cfg_pred(self._pred_cb.isChecked(), self._tsw_spin.value()),
            C.cfg_decay(U.t_fast_join(self._fast_step_spin.value(), self._toff_fast_spin.value()),
                        U.tmin_from_us(self._ton_min_spin.value()),
                        U.tmin_from_us(self._toff_min_spin.value())),
            C.cfg_apply(),
        ])

    def update_motor_config(self, cfg: dict):
        """DRV:CFG? view: drive mode, KVAL duties, TVAL references and the current-mode
        regulation (the saved config, not the chip)."""
        if "drive_mode" in cfg:
            self._drive_mode_combo.setCurrentIndex(1 if cfg["drive_mode"] == "current" else 0)
        if "kval" in cfg:
            for slider, raw in zip((self._kval_hold_slider, self._kval_run_slider,
                                    self._kval_acc_slider, self._kval_dec_slider), cfg["kval"]):
                slider.setValue(U.kval_to_percent(raw))
        if "cm" in cfg:
            c = cfg["cm"]
            self._pred_cb.setChecked(bool(c["pred"]))
            self._tsw_spin.setValue(int(c["tsw_us"]))
            fs, tf = U.t_fast_split(c["t_fast"])
            self._fast_step_spin.setValue(fs)
            self._toff_fast_spin.setValue(tf)
            self._ton_min_spin.setValue(U.tmin_to_us(c["ton_min"]))
            self._toff_min_spin.setValue(U.tmin_to_us(c["toff_min"]))
        if "tval" in cfg:
            for slider, raw in zip(self._tval_sliders, cfg["tval"]):
                if raw > slider.maximum():                 # the board holds more than the rating
                    self._rated_spin.setValue(U.tval_to_amps(raw))
                slider.setValue(int(raw))

    @Slot()
    def _on_apply_params(self):
        self.param_apply_requested.emit([
            C.cfg_motion(U.acc_raw_from_sps2(self._acc_spin.value()),
                         U.acc_raw_from_sps2(self._dec_spin.value()),
                         U.maxspd_raw_from_sps(self._maxspd_spin.value())),
            C.cfg_apply(),
        ])

    @Slot()
    def _on_refresh_clicked(self):
        self.refresh_requested.emit()

    def update_driver_params(self, dbg: dict):
        """Update from the DBG:MOTOR view (smc_core.views.motor_debug). In current mode the
        KVAL registers hold the TVAL references, so they must not overwrite the KVAL faders."""
        if "kval" in dbg and not self._is_current_mode():
            h, r, a, d = dbg["kval"]
            self._kval_hold_slider.setValue(U.kval_to_percent(h))
            self._kval_run_slider.setValue(U.kval_to_percent(r))
            self._kval_acc_slider.setValue(U.kval_to_percent(a))
            self._kval_dec_slider.setValue(U.kval_to_percent(d))
        if "adc_out" in dbg:
            extra = ""
            if "config_reg" in dbg:
                extra = "   CONFIG %04X (PRED %s)" % (dbg["config_reg"],
                                                      "on" if dbg["config_reg"] & 0x8000 else "off")
            self._adc_label.setText("ADCIN raw: %d%s" % (dbg["adc_out"], extra))
        if "acc_raw" in dbg:
            self._acc_spin.setValue(U.acc_sps2_from_raw(dbg["acc_raw"]))
            self._dec_spin.setValue(U.acc_sps2_from_raw(dbg["dec_raw"]))
            self._maxspd_spin.setValue(U.maxspd_sps_from_raw(dbg["maxspd_raw"]))

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
