"""
Driver configuration panel.

Coil drive faders (current in amps, duty in % of the supply) and motion parameter controls
with live preview graphs. The ranges and steps are the driver's (DRV:CAPS?, set_caps); the
values come from the board in physical units (update_physical: DRV:CFG:PHYS?, or a protocol 2
board's DRV:CFG? translated by smc_core.driver), and Apply sends the physical commands to a
protocol 3 board and the chip-code aliases to a protocol 2 one (smc_core.driver picks).
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
    QButtonGroup,
)
from PySide6.QtCore import Qt, Slot, Signal, QSettings

from smc_core import commands as C
from smc_core import driver as D
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
        background: #00f0ff;
        border-radius: 6px;
    }
    QSlider::sub-page:vertical {
        background: #2d2d2d;
        border-radius: 6px;
    }
"""

FADER_STYLE_GREEN = FADER_STYLE.replace('#00f0ff', '#00cc66')

# Fader limits for current mode: the chip's TVAL goes to 0x7F (9.9 A) but the motor's
# nameplate current is the real ceiling, so the faders stop at a user-set rating.
MOTOR_RATED_A_DEFAULT = 2.8          # 23HS22-2804-ME1K, the project's motor
MOTOR_RATED_A_MAX = round(U.tval_to_amps(0x7F), 2)

def _fmt_ma(amps: float) -> str:
    return "%.0f mA" % (amps * 1000.0) if amps < 1.0 else "%.3g A" % amps


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
        self._step_mode = 7                 # from DRV:USTEP? / DRV:STEP_MODE?; 1/128 default
        self._syncing = False
        # What the driver can do (DRV:CAPS?): the powerSTEP01's until the board says otherwise.
        # The current faders count in the driver's current steps (amps = raw x step).
        self._caps = dict(D.POWERSTEP01_CAPS)
        self._physical = False              # True: the board takes the physical commands
        self._current_step = float(self._caps["current_step_a"])
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

    # Current faders: the slider counts the driver's current steps (DRV:CAPS? current_step_a)
    def _raw_to_amps(self, raw: int) -> float:
        return round(int(raw) * self._current_step, 4)

    def _amps_to_raw(self, amps: float) -> int:
        return max(0, int(round(float(amps) / self._current_step))) if self._current_step > 0 else 0

    def _current_raw_max(self) -> int:
        return max(1, int(float(self._caps.get("max_current_a", 9.9)) / self._current_step + 1e-6))

    def _create_current_channel(self, name, description, tooltip):
        """Vertical fader in the driver's current steps with a synced spinbox in amps.

        Returns (layout, slider, spinbox); the slider's value counts current steps.
        """
        ch = QVBoxLayout()
        ch.setSpacing(4)

        name_lbl = QLabel(name + "  \u24d8")
        name_lbl.setAlignment(Qt.AlignCenter)
        name_lbl.setFont(QFont("Segoe UI", 9, QFont.Bold))
        name_lbl.setToolTip(tooltip)
        ch.addWidget(name_lbl)

        slider = QSlider(Qt.Vertical)
        slider.setRange(0, self._current_raw_max())
        slider.setValue(0)
        slider.setTickPosition(QSlider.TicksBothSides)
        slider.setTickInterval(max(1, int(round(1.0 / self._current_step))))    # ~1 A
        slider.setMinimumHeight(120)
        slider.setFixedWidth(60)
        slider.setStyleSheet(FADER_STYLE_GREEN)
        slider.setToolTip(tooltip)
        ch.addWidget(slider, alignment=Qt.AlignHCenter)

        spin = QDoubleSpinBox()
        spin.setRange(0.0, MOTOR_RATED_A_MAX)
        spin.setDecimals(3)                 # the driver steps 78 mA: 0.312 A, not 0.31
        spin.setSingleStep(self._current_step)
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
        slider.valueChanged.connect(lambda raw: spin.setValue(self._raw_to_amps(raw)))
        spin.valueChanged.connect(lambda amps: slider.setValue(self._amps_to_raw(amps)))
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
            slider.valueChanged.connect(self._update_coil_waveform)
        self._on_drive_mode_changed(1 if self._is_current_mode() else 0)

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
        self._apply_kval_btn = QPushButton("Apply duty")
        self._apply_kval_btn.setToolTip("Send the drive duties (% of the supply) to the driver (used in voltage mode)")
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
        self._mode_voltage_btn = QPushButton("Voltage (duty)")
        self._mode_current_btn = QPushButton("Current (amps)")
        for b in (self._mode_voltage_btn, self._mode_current_btn):
            b.setCheckable(True)
            b.setMinimumWidth(130)
            b.setStyleSheet("QPushButton:checked { background: #2d6cdf; color: white; font-weight: bold; }")
        self._mode_current_btn.setChecked(True)          # current mode is the firmware default
        self._mode_group = QButtonGroup(self)
        self._mode_group.setExclusive(True)
        self._mode_group.addButton(self._mode_voltage_btn, 0)
        self._mode_group.addButton(self._mode_current_btn, 1)
        self._mode_current_btn.toggled.connect(lambda on: self._on_drive_mode_changed(1 if on else 0))
        tip = ("Voltage mode drives the coils with a duty cycle (KVAL faders); current mode regulates "
               "the coil current to the TVAL references. Pick one, then Apply (the chip takes the "
               "mode in Hi-Z). Both fader sets stay editable; the badge shows what the chip is in.")
        self._mode_voltage_btn.setToolTip(tip)
        self._mode_current_btn.setToolTip(tip)
        mode_row.addWidget(self._mode_voltage_btn)
        mode_row.addWidget(self._mode_current_btn)
        self._chip_mode_label = QLabel("chip: --")
        self._chip_mode_label.setFont(QFont("Consolas", 9, QFont.Bold))
        self._chip_mode_label.setStyleSheet("color: #888; padding: 2px 8px; border: 1px solid #444; border-radius: 4px;")
        self._chip_mode_label.setToolTip("Drive mode read back from the driver (DRV:USTEP?; protocol 2: DRV:STEP_MODE?)")
        mode_row.addWidget(self._chip_mode_label)
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
        self._apply_drive_btn = QPushButton("Apply mode + currents")
        self._apply_drive_btn.setToolTip(
            "Drive mode, coil currents (amps) and idle release, then APPLY (needs the motor idle)")
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
        idle_row = QHBoxLayout()
        idle_lbl = QLabel("Release coils after idle:")
        idle_lbl.setToolTip("Hi-Z after the motor has sat stopped this long (0 = hold for ever). "
                            "The next motion command re-energises the coils by itself. Saves "
                            "battery on a robot that waits; no holding torque while released.")
        idle_row.addWidget(idle_lbl)
        self._idle_spin = QDoubleSpinBox()
        self._idle_spin.setRange(0.0, 60.0)
        self._idle_spin.setDecimals(1)
        self._idle_spin.setSingleStep(0.5)
        self._idle_spin.setSuffix(" s")
        self._idle_spin.setSpecialValueText("never")
        self._idle_spin.setFont(QFont("Consolas", 9))
        self._idle_spin.setToolTip(idle_lbl.toolTip())
        idle_row.addWidget(self._idle_spin)
        idle_row.addStretch()
        tbtn.addLayout(idle_row)
        tnote = QLabel("Coil current in 78 mA steps up to 9.91 A (the driver's, DRV:CAPS?)")
        tnote.setStyleSheet("font-size: 8px; color: #888;")
        self._tnote = tnote
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
            pen=pg.mkPen(color='#00f0ff', width=2), name="KVAL (% Vs)")
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
        return self._mode_current_btn.isChecked()

    def set_rated_current(self, amps: float) -> None:
        self._rated_spin.setValue(min(max(float(amps), 0.1), MOTOR_RATED_A_MAX))

    def rated_current(self) -> float:
        return float(self._rated_spin.value())

    def set_drive_mode(self, current: bool) -> None:
        (self._mode_current_btn if current else self._mode_voltage_btn).setChecked(True)

    @Slot(int)
    def _on_drive_mode_changed(self, index: int):
        """Both fader sets stay live; the title and the profile follow the selected mode."""
        current = index == 1
        self._drive_group.setTitle("Coil Drive \u2014 current mode selected: regulated coil current (amps)"
                                   if current else
                                   "Coil Drive \u2014 voltage mode selected: duty cycle (% of supply voltage)")
        self._apply_kval_btn.setText("Apply duty" + ("" if not current else "  (voltage mode)"))
        self._apply_drive_btn.setText("Apply mode + currents" if current else "Apply mode (voltage)")
        self._update_kval_profile()
        self._update_coil_waveform()

    @Slot(float)
    def _on_rated_current_changed(self, amps: float):
        """Cap the TVAL faders at the motor's rated current and remember it on this PC."""
        cap = min(self._amps_to_raw(amps), self._current_raw_max())
        for slider, spin in zip(self._tval_sliders, self._tval_spins):
            slider.setMaximum(max(cap, 1))
            spin.setMaximum(self._raw_to_amps(max(cap, 1)))
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
            hold, run_, acc_kval, dec_kval = (self._raw_to_amps(sl.value()) for sl in self._tval_sliders)
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
        self._kval_profile_curve.opts['name'] = "current (A)" if current else "duty (% Vs)"
        self._kval_profile_plot.setYRange(0, 1.15 * full)
        self._kval_profile_plot.setXRange(0, t5 * 1.02)

    def _update_coil_waveform(self, _value=None):
        """Draw the two phase currents over one electrical cycle, which is four full steps.

        A = I cos(theta), B = I sin(theta), quantised to the selected microstep count per full
        step (the sine table the powerSTEP01 steps through). I is the running reference: the
        TVAL run current in amps in current mode, the KVAL run duty in % of supply in voltage
        mode. Full-step boundaries are the dotted lines; a lower microstep count shows as a
        coarser staircase, which is the torque ripple you hear.
        """
        import math
        try:
            current = self._is_current_mode()
        except AttributeError:
            return
        usteps = 1 << int(getattr(self, "_step_mode", 7))
        if current:
            amp = self._raw_to_amps(self._tval_sliders[1].value())
            unit = "A"
        else:
            amp = float(self._kval_run_slider.value())
            unit = "% Vs"
        n = 4 * usteps                                  # points per electrical cycle
        a_x, a_y, b_x, b_y = [], [], [], []
        for i in range(n + 1):
            theta = 2.0 * math.pi * i / n
            x = 4.0 * i / n                             # full steps
            ya, yb = amp * math.cos(theta), amp * math.sin(theta)
            if i > 0 and usteps < 64:                   # staircase: hold the previous value up to x
                a_x.append(x); a_y.append(a_y[-1])
                b_x.append(x); b_y.append(b_y[-1])
            a_x.append(x); a_y.append(ya)
            b_x.append(x); b_y.append(yb)
        self._coil_a_curve.setData(a_x, a_y)
        self._coil_a_fill.setData(a_x, a_y)
        self._coil_b_curve.setData(b_x, b_y)
        self._coil_b_fill.setData(b_x, b_y)
        y_lim = max(amp, 0.1) * 1.2
        self._coil_a_plot.setYRange(-y_lim, y_lim)
        self._coil_b_plot.setYRange(-y_lim, y_lim)
        self._coil_a_plot.setXRange(0, 4)
        self._coil_a_plot.setLabel('left', 'A (%s)' % unit, **{'font-size': '8pt', 'color': '#ffcc00'})
        self._coil_b_plot.setLabel('left', 'B (%s)' % unit, **{'font-size': '8pt', 'color': '#00ccff'})
        self._coil_b_plot.setLabel('bottom', 'full steps (one electrical cycle, 1/%d microstep)' % usteps,
                                   **{'font-size': '8pt', 'color': '#888'})
        for i, (la, lb) in enumerate(zip(self._coil_phase_labels_a, self._coil_phase_labels_b)):
            la.setText("%d\u00b0" % (90 * i)); lb.setText("%d\u00b0" % (90 * i))
            la.setPos(i + 0.5, y_lim * 0.78); lb.setPos(i + 0.5, y_lim * 0.78)
        for i, (sa, sb) in enumerate(zip(self._coil_separators_a, self._coil_separators_b)):
            sa.setValue(i + 1); sb.setValue(i + 1)

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

    def _duties(self):
        return tuple(sl.value() / 100.0 for sl in (self._kval_hold_slider, self._kval_run_slider,
                                                    self._kval_acc_slider, self._kval_dec_slider))

    @Slot()
    def _on_apply_kval(self):
        """The voltage-mode duties: DRV:CFG:DUTY (protocol 3) or DRV:CFG:KVAL (protocol 2)."""
        if self._physical:
            self.kval_apply_requested.emit(C.cfg_duty(*self._duties()))
        else:
            self.kval_apply_requested.emit(C.cfg_kval(
                U.kval_from_percent(self._kval_hold_slider.value()),
                U.kval_from_percent(self._kval_run_slider.value()),
                U.kval_from_percent(self._kval_acc_slider.value()),
                U.kval_from_percent(self._kval_dec_slider.value())))

    @Slot()
    def _on_apply_drive(self):
        changes = {"drive_mode": "current" if self._is_current_mode() else "voltage",
                   "idle_release_ms": int(round(self._idle_spin.value() * 1000))}
        if self._caps.get("current_mode", True) and self._caps.get("current_settable", True):
            changes["current_a"] = tuple(self._raw_to_amps(sl.value()) for sl in self._tval_sliders)
        self.drive_apply_requested.emit(D.settings_requests(changes, self._physical) + [C.cfg_apply()])

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

    def set_caps(self, caps: dict, physical: bool) -> None:
        """What the driver can do (DRV:CAPS?, or smc_core.driver.POWERSTEP01_CAPS for a protocol 2
        board) and whether it takes the physical commands: fader ranges and steps follow it, the
        controls of a drive mode the driver lacks are hidden."""
        self._caps = dict(caps)
        self._physical = bool(physical)
        step = float(caps.get("current_step_a") or self._current_step or 0.078)
        amps = [self._raw_to_amps(sl.value()) for sl in self._tval_sliders]
        self._current_step = step
        top = self._current_raw_max()
        for slider, spin, a in zip(self._tval_sliders, self._tval_spins, amps):
            slider.blockSignals(True)
            slider.setRange(0, top)
            slider.setTickInterval(max(1, int(round(1.0 / step))))
            slider.setValue(self._amps_to_raw(a))
            slider.blockSignals(False)
            spin.setSingleStep(step)
        self._rated_spin.setMaximum(round(float(caps.get("max_current_a", MOTOR_RATED_A_MAX)), 2))
        self._on_rated_current_changed(self._rated_spin.value())
        voltage, current = bool(caps.get("voltage_mode", True)), bool(caps.get("current_mode", True))
        for w in (self._kval_row, self._apply_kval_btn, self._mode_voltage_btn):
            w.setVisible(voltage)
        self._mode_current_btn.setVisible(current)
        # a current set in hardware (a VREF pot): the mode exists, there is nothing to set
        self._tval_row.setVisible(current and bool(caps.get("current_settable", True)))
        if not current and voltage:
            self.set_drive_mode(False)
        elif current and not voltage:
            self.set_drive_mode(True)
        lo = int(float(caps.get("accel_min_fsps2", 15)) + 0.999)
        hi = int(float(caps.get("accel_max_fsps2", 59590)))
        for sl, sp in ((self._acc_slider, self._acc_spin), (self._dec_slider, self._dec_spin)):
            sl.setRange(lo, hi)
            sp.setRange(lo, hi)
        lo = int(float(caps.get("speed_limit_step_fsps", 15)) + 0.999)
        hi = int(float(caps.get("max_speed_limit_fsps", 15609)))
        self._maxspd_slider.setRange(lo, hi)
        self._maxspd_spin.setRange(lo, hi)
        self._tnote.setText(
            "Coil current in %s steps up to %.2f A (the driver's, DRV:CAPS?)%s" % (
                _fmt_ma(step), float(caps.get("max_current_a", 0)),
                "" if self._physical else ";\nprotocol 2 board: sent as the powerSTEP01's TVAL codes"))
        self._update_kval_profile()
        self._update_coil_waveform()

    def update_physical(self, phys: dict) -> None:
        """The driver settings in physical units (smc_core.views.driver_physical, or a protocol 2
        board's DRV:CFG? through smc_core.driver.settings_from_legacy): drive mode, currents,
        duties, idle release, ramps and speed ceiling."""
        if "drive_mode" in phys:
            self.set_drive_mode(phys["drive_mode"] == "current")
        if "hold_duty" in phys:
            for slider, key in zip((self._kval_hold_slider, self._kval_run_slider,
                                    self._kval_acc_slider, self._kval_dec_slider),
                                   ("hold_duty", "run_duty", "acc_duty", "dec_duty")):
                slider.setValue(int(round(float(phys[key]) * 100.0)))
        if "hold_a" in phys:
            for slider, key in zip(self._tval_sliders, ("hold_a", "run_a", "acc_a", "dec_a")):
                raw = self._amps_to_raw(phys[key])
                if raw > slider.maximum():                 # the board holds more than the rating
                    self._rated_spin.setValue(self._raw_to_amps(raw))
                slider.setValue(raw)
        if "idle_release_ms" in phys:
            self._idle_spin.setValue(int(phys["idle_release_ms"]) / 1000.0)
        for spin, key in ((self._acc_spin, "accel_fsps2"), (self._dec_spin, "decel_fsps2"),
                          (self._maxspd_spin, "max_speed_fsps")):
            if key in phys:
                spin.setValue(int(round(float(phys[key]))))

    def update_motor_config(self, cfg: dict):
        """DRV:CFG? view: the current-mode regulation (PRED/TSW/decay timing: powerSTEP01 tuning
        with no physical command) and the idle release. Drive mode, currents and duties come
        through update_physical."""
        if "idle_release_ms" in cfg:
            self._idle_spin.setValue(cfg["idle_release_ms"] / 1000.0)
        if "cm" in cfg:
            c = cfg["cm"]
            self._pred_cb.setChecked(bool(c["pred"]))
            self._tsw_spin.setValue(int(c["tsw_us"]))
            fs, tf = U.t_fast_split(c["t_fast"])
            self._fast_step_spin.setValue(fs)
            self._toff_fast_spin.setValue(tf)
            self._ton_min_spin.setValue(U.tmin_to_us(c["ton_min"]))
            self._toff_min_spin.setValue(U.tmin_to_us(c["toff_min"]))

    @Slot()
    def _on_apply_params(self):
        """Ramps and speed ceiling: DRV:CFG:ACCEL/DECEL/MAXSPD (protocol 3) or DRV:CFG:MOTION."""
        changes = {"accel_fsps2": self._acc_spin.value(), "decel_fsps2": self._dec_spin.value(),
                   "max_speed_fsps": self._maxspd_spin.value()}
        self.param_apply_requested.emit(D.settings_requests(changes, self._physical) + [C.cfg_apply()])

    @Slot()
    def _on_refresh_clicked(self):
        self.refresh_requested.emit()

    def update_driver_params(self, dbg: dict):
        """Update from the DBG:MOTOR view (smc_core.views.motor_debug): the ADC/CONFIG note, and on a
        protocol 2 board the register values (a protocol 3 board's physical settings arrive
        through update_physical). In current mode the KVAL registers hold the TVAL references,
        so they must not overwrite the duty faders."""
        if self._physical:
            dbg = {k: v for k, v in dbg.items() if k not in ("kval", "acc_raw", "dec_raw", "maxspd_raw")}
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
        """Update motor configuration (full_steps_per_rev, step mode, the chip's drive mode)."""
        if "drive_mode" in data:
            current = data["drive_mode"] == "current"
            self._chip_mode_label.setText("chip: %s" % ("CURRENT" if current else "VOLTAGE"))
            self._chip_mode_label.setStyleSheet(
                "color: %s; padding: 2px 8px; border: 1px solid #444; border-radius: 4px;"
                % ("#00cc66" if current else "#00f0ff"))
        if "step_mode" in data:
            self._step_mode = int(data["step_mode"]) & 7
            self._update_coil_waveform()
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
