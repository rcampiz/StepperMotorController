"""
System Identification Panel.

Orchestrates automated step/ramp tests on the motor, downloads response data,
fits a FOPDT plant model, and recommends PID gains.
"""

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox, QLabel,
    QPushButton, QComboBox, QTableWidget, QTableWidgetItem,
    QProgressBar, QDoubleSpinBox, QHeaderView, QAbstractItemView,
    QTabWidget, QScrollArea, QSplitter, QTextEdit,
)
from PySide6.QtCore import Signal, Slot, QTimer, Qt
from PySide6.QtGui import QFont
from dataclasses import dataclass, field
from typing import Optional
import logging
import math

import pyqtgraph as pg

log = logging.getLogger("sysid_panel")


# ── Data classes ──

@dataclass
class SysIdTestConfig:
    """Configuration for a single sysid test."""
    type: str           # "step", "ramp", "sine", "trapezoid", "rectangle"
    speed: int          # Target speed (steps/s)
    start_speed: int    # Start speed for ramp/sine baseline (0 for step)
    duration_ms: int    # Active recording time
    settle_ms: int      # Pre/post settle time
    forward: bool = True
    frequency_mhz: int = 0  # Sine frequency in millihertz (e.g. 500 = 0.5 Hz)


@dataclass
class SysIdSample:
    """One data point from a sysid test."""
    time_ms: int
    setpoint_tps: int   # encoder ticks/sec
    actual_tps: int     # encoder ticks/sec
    pos_error: int      # encoder ticks


@dataclass
class SysIdTestResult:
    """Results from one completed test."""
    config: SysIdTestConfig
    samples: list       # list[SysIdSample]
    # FOPDT fit results (filled after fitting)
    K: float = 0.0
    tau: float = 0.0
    theta: float = 0.0
    r_squared: float = 0.0
    fit_ok: bool = False
    # Sine frequency-domain results
    sine_gain: float = 0.0
    sine_phase_deg: float = 0.0
    sine_freq_hz: float = 0.0
    sine_fit_ok: bool = False
    # Transfer function data (filled after TF computation)
    tf_data: Optional[dict] = field(default=None, repr=False)
    # Transient metrics (filled after transient analysis)
    transient: Optional[dict] = field(default=None, repr=False)


# ── Test plan presets ──

def _make_preset(name: str, forward: bool = True) -> list:
    """Create a preset test plan. Max speed capped at 900 sps (270 RPM)."""
    if name == "Quick":
        return [
            SysIdTestConfig("step", 100, 0, 5000, 1000, forward),
            SysIdTestConfig("step", 300, 0, 5000, 1000, forward),
            SysIdTestConfig("step", 600, 0, 5000, 1000, forward),
        ]
    elif name == "Standard":
        return [
            SysIdTestConfig("step", 100, 0, 5000, 1000, forward),
            SysIdTestConfig("step", 300, 0, 5000, 1000, forward),
            SysIdTestConfig("step", 600, 0, 5000, 1000, forward),
            SysIdTestConfig("ramp", 300, 0, 5000, 1000, forward),
            SysIdTestConfig("ramp", 600, 0, 5000, 1000, forward),
            SysIdTestConfig("trapezoid", 400, 0, 5000, 1000, forward),
        ]
    elif name == "Thorough":
        return [
            SysIdTestConfig("step", 50, 0, 5000, 1000, forward),
            SysIdTestConfig("step", 100, 0, 5000, 1000, forward),
            SysIdTestConfig("step", 200, 0, 5000, 1000, forward),
            SysIdTestConfig("step", 400, 0, 5000, 1000, forward),
            SysIdTestConfig("step", 700, 0, 5000, 1000, forward),
            SysIdTestConfig("ramp", 300, 0, 5000, 1000, forward),
            SysIdTestConfig("ramp", 900, 0, 5000, 1000, forward),
            SysIdTestConfig("trapezoid", 400, 0, 5000, 1000, forward),
            SysIdTestConfig("rectangle", 500, 0, 5000, 1000, forward),
            SysIdTestConfig("sine", 200, 400, 8000, 1000, forward, 500),  # 0.5 Hz
            SysIdTestConfig("sine", 200, 400, 8000, 1000, forward, 1000), # 1.0 Hz
        ]
    elif name == "Frequency Sweep":
        return [
            SysIdTestConfig("sine", 200, 400, 10000, 1000, forward, 200),   # 0.2 Hz
            SysIdTestConfig("sine", 200, 400, 10000, 1000, forward, 500),   # 0.5 Hz
            SysIdTestConfig("sine", 200, 400, 10000, 1000, forward, 1000),  # 1.0 Hz
            SysIdTestConfig("sine", 200, 400, 10000, 1000, forward, 2000),  # 2.0 Hz
            SysIdTestConfig("sine", 200, 400, 10000, 1000, forward, 4000),  # 4.0 Hz
        ]
    return []


# ── FOPDT model fitting ──

def fit_fopdt(samples: list) -> Optional[tuple]:
    """Fit FOPDT model to step response data.

    Returns (K, tau, theta, r_squared) or None if fitting fails.
    Requires numpy and scipy.
    """
    try:
        import numpy as np
        from scipy.optimize import curve_fit
    except ImportError:
        log.warning("fit_fopdt: numpy/scipy not available")
        return None

    if len(samples) < 20:
        log.warning(f"fit_fopdt: too few samples ({len(samples)})")
        return None

    time_s = np.array([s.time_ms / 1000.0 for s in samples])
    setpoint = np.array([float(s.setpoint_tps) for s in samples])
    actual = np.array([float(s.actual_tps) for s in samples])

    log.info(f"fit_fopdt: {len(samples)} samples, "
             f"time=[{time_s[0]:.2f}..{time_s[-1]:.2f}]s, "
             f"setpoint=[{setpoint.min():.0f}..{setpoint.max():.0f}], "
             f"actual=[{actual.min():.0f}..{actual.max():.0f}]")

    # Find the step: largest absolute change in setpoint
    diffs = np.abs(np.diff(setpoint))
    max_diff = float(np.max(diffs))
    if max_diff < 1:
        log.warning(f"fit_fopdt: no step detected (max_diff={max_diff:.2f})")
        return None
    step_idx = int(np.argmax(diffs)) + 1

    step_magnitude = float(setpoint[step_idx] - setpoint[step_idx - 1])
    log.info(f"fit_fopdt: step at idx={step_idx} (t={time_s[step_idx]:.2f}s), "
             f"magnitude={step_magnitude:.1f} tps")
    if abs(step_magnitude) < 1:
        log.warning("fit_fopdt: step magnitude too small")
        return None

    # Data from step through ACTIVE phase only (exclude POST_SETTLE where
    # setpoint drops to 0 and motor decelerates — that would corrupt y_final)
    sp_after = setpoint[step_idx:]
    active_mask = np.abs(sp_after) > 0.5
    if not np.any(active_mask):
        log.warning("fit_fopdt: no active samples after step")
        return None
    end_active = int(np.where(active_mask)[0][-1]) + 1

    t = time_s[step_idx:step_idx + end_active] - time_s[step_idx]
    y = actual[step_idx:step_idx + end_active]
    log.info(f"fit_fopdt: active region: {len(t)} samples, "
             f"t=[{t[0]:.2f}..{t[-1]:.2f}]s")
    if len(t) < 10:
        log.warning(f"fit_fopdt: too few active samples ({len(t)})")
        return None

    y0 = float(np.mean(actual[max(0, step_idx - 5):step_idx]))
    y_final = float(np.mean(y[-10:]))

    K_init = (y_final - y0) / step_magnitude
    log.info(f"fit_fopdt: y0={y0:.1f}, y_final={y_final:.1f}, "
             f"K_init={K_init:.4f}")
    if abs(K_init) < 0.001:
        K_init = 1.0

    def fopdt_response(t_arr, K, tau, theta):
        return np.where(
            t_arr >= theta,
            y0 + K * step_magnitude * (1.0 - np.exp(-(t_arr - theta) / tau)),
            y0,
        )

    try:
        popt, _ = curve_fit(
            fopdt_response, t, y,
            p0=[max(0.01, min(K_init, 10.0)), 0.3, 0.05],
            bounds=([0.001, 0.005, 0.0], [20.0, 30.0, 5.0]),
            maxfev=10000,
        )
        K, tau, theta = popt

        # R² goodness of fit
        y_pred = fopdt_response(t, K, tau, theta)
        ss_res = float(np.sum((y - y_pred) ** 2))
        ss_tot = float(np.sum((y - np.mean(y)) ** 2))
        r_squared = 1.0 - (ss_res / ss_tot) if ss_tot > 0 else 0.0

        log.info(f"fit_fopdt: SUCCESS K={K:.4f} tau={tau:.4f} "
                 f"theta={theta:.4f} R²={r_squared:.4f}")
        return float(K), float(tau), float(theta), float(r_squared)
    except Exception as e:
        log.warning(f"fit_fopdt: curve_fit failed: {type(e).__name__}: {e}")
        return None


def fit_sine_response(samples: list, freq_hz: float) -> Optional[tuple]:
    """Extract gain and phase from sinusoidal test data.

    Fits A*sin(2*pi*f*t + phi) + C to both setpoint and actual signals,
    then computes gain = A_actual/A_setpoint, phase = phi_actual - phi_setpoint.

    Returns (gain, phase_deg, r_squared) or None if fitting fails.
    """
    try:
        import numpy as np
        from scipy.optimize import curve_fit
    except ImportError:
        log.warning("fit_sine_response: numpy/scipy not available")
        return None

    if len(samples) < 20 or freq_hz <= 0:
        log.warning(f"fit_sine_response: too few samples ({len(samples)}) "
                    f"or bad freq ({freq_hz})")
        return None

    time_s = np.array([s.time_ms / 1000.0 for s in samples])
    setpoint = np.array([float(s.setpoint_tps) for s in samples])
    actual = np.array([float(s.actual_tps) for s in samples])

    # Skip initial transient: first period or 20% of data, whichever is larger
    period_s = 1.0 / freq_hz
    t_total = time_s[-1] - time_s[0]
    skip_t = max(t_total * 0.2, period_s)
    mask = (time_s - time_s[0]) >= skip_t
    if np.sum(mask) < 20:
        mask = np.ones(len(time_s), dtype=bool)

    t = time_s[mask]
    sp = setpoint[mask]
    act = actual[mask]

    w = 2 * np.pi * freq_hz

    def sine_model(t_arr, A, phi, C):
        return A * np.sin(w * t_arr + phi) + C

    # Fit setpoint sine
    sp_amp_init = (np.max(sp) - np.min(sp)) / 2
    sp_off_init = float(np.mean(sp))
    try:
        sp_popt, _ = curve_fit(sine_model, t, sp,
                               p0=[sp_amp_init, 0.0, sp_off_init],
                               maxfev=5000)
        A_sp = abs(sp_popt[0])
        phi_sp = sp_popt[1]
    except Exception as e:
        log.warning(f"fit_sine_response: setpoint fit failed: {e}")
        return None

    if A_sp < 1.0:
        log.warning(f"fit_sine_response: setpoint amplitude too small ({A_sp:.2f})")
        return None

    # Fit actual response sine
    act_amp_init = (np.max(act) - np.min(act)) / 2
    act_off_init = float(np.mean(act))
    try:
        act_popt, _ = curve_fit(sine_model, t, act,
                                p0=[act_amp_init, phi_sp, act_off_init],
                                maxfev=5000)
        A_act = abs(act_popt[0])
        phi_act = act_popt[1]
    except Exception as e:
        log.warning(f"fit_sine_response: actual fit failed: {e}")
        return None

    # Gain and phase
    gain = A_act / A_sp
    phase_deg = float(np.degrees(phi_act - phi_sp))
    # Normalize phase to [-180, 180]
    while phase_deg > 180:
        phase_deg -= 360
    while phase_deg < -180:
        phase_deg += 360

    # R² for actual fit quality
    y_pred = sine_model(t, *act_popt)
    ss_res = float(np.sum((act - y_pred) ** 2))
    ss_tot = float(np.sum((act - np.mean(act)) ** 2))
    r_squared = 1.0 - (ss_res / ss_tot) if ss_tot > 0 else 0.0

    log.info(f"fit_sine_response: freq={freq_hz:.3f}Hz gain={gain:.4f} "
             f"phase={phase_deg:.1f}\u00b0 R\u00b2={r_squared:.4f}")
    return gain, phase_deg, r_squared


def recommend_pid(K: float, tau: float, theta: float,
                  method: str = "gentle") -> tuple:
    """Compute PI gains from FOPDT model parameters.

    The fpr/ppr conversion in applyPIDCorrection cancels with the plant's
    inverse ratio, so the PID sees effective gain = K (typically ~1.0).
    Kd is always 0 — encoder velocity at 50ms is too noisy for derivative.

    Returns (Kp, Ki, Kd).
    """
    if K <= 0 or tau <= 0:
        return 0.0, 0.0, 0.0
    if theta < 0:
        theta = 0.0

    if method == "gentle":
        # Correct 20% of error per time-constant — smooth, stable
        Kp = 0.2 / K
        Ti = 2.0 * tau  # Slow integral — full correction in ~2 tau
        Ki = Kp / Ti if Ti > 0 else 0.0
    elif method == "simc":
        # Moderate — reliable workhorse
        Kp = 0.4 / K
        Ti = tau
        Ki = Kp / Ti if Ti > 0 else 0.0
    elif method == "lambda":
        # Conservative — very smooth, slow convergence
        Kp = 0.15 / K
        Ti = 2.5 * tau
        Ki = Kp / Ti if Ti > 0 else 0.0
    elif method == "imc":
        # Responsive — faster correction, still safe
        Kp = 0.6 / K
        Ti = max(0.7 * tau, 0.1)
        Ki = Kp / Ti if Ti > 0 else 0.0
    else:
        return 0.0, 0.0, 0.0

    Kd = 0.0
    return Kp, Ki, Kd


# ── Transfer function computation ──

def compute_transfer_functions(K: float, tau: float, theta: float,
                                dt: float = 0.05) -> Optional[dict]:
    """Compute s-domain and z-domain TFs with Padé [1/1] delay approximation.

    FOPDT: G(s) = K * e^(-θs) / (τs + 1)
    Padé [1/1]: e^(-θs) ≈ (1 - θs/2) / (1 + θs/2)
    Combined: G(s) = K*(1 - θs/2) / (τθs²/2 + (τ + θ/2)s + 1)

    Returns dict with s_num, s_den, s_poles, s_zeros, z_num, z_den,
    z_poles, z_zeros, dc_gain, or None on failure.
    """
    try:
        import numpy as np
        from scipy.signal import cont2discrete
    except ImportError:
        log.warning("compute_transfer_functions: numpy/scipy not available")
        return None

    if K <= 0 or tau <= 0:
        return None

    # Handle zero/tiny delay gracefully
    theta_eff = max(theta, 0.001)

    # s-domain coefficients (descending powers of s)
    s_num = np.array([-K * theta_eff / 2.0, K])
    s_den = np.array([tau * theta_eff / 2.0, tau + theta_eff / 2.0, 1.0])

    s_poles = np.roots(s_den)
    s_zeros = np.roots(s_num)

    # z-domain via ZOH discretization
    try:
        z_sys = cont2discrete((s_num, s_den), dt, method='zoh')
        z_num = z_sys[0].flatten()
        z_den = z_sys[1].flatten()
    except Exception as e:
        log.warning(f"cont2discrete failed: {e}")
        # Fallback: just provide s-domain
        z_num = np.array([1.0])
        z_den = np.array([1.0])

    z_poles = np.roots(z_den)
    z_zeros = np.roots(z_num) if len(z_num) > 1 else np.array([])

    return {
        's_num': s_num, 's_den': s_den,
        's_poles': s_poles, 's_zeros': s_zeros,
        'z_num': z_num, 'z_den': z_den,
        'z_poles': z_poles, 'z_zeros': z_zeros,
        'dc_gain': K,
        'theta': theta_eff,
        'tau': tau,
    }


def compute_transient_metrics(samples: list) -> Optional[dict]:
    """Compute transient response metrics from step response data.

    Returns dict with rise_time, settling_time, overshoot_pct, peak_time,
    delay_time, y_final, y0, or None if analysis fails.
    """
    try:
        import numpy as np
    except ImportError:
        return None

    if len(samples) < 20:
        return None

    time_s = np.array([s.time_ms / 1000.0 for s in samples])
    actual = np.array([float(s.actual_tps) for s in samples])
    setpoint = np.array([float(s.setpoint_tps) for s in samples])

    # Find the step: largest absolute change in setpoint
    diffs = np.abs(np.diff(setpoint))
    if float(np.max(diffs)) < 1:
        return None
    step_idx = int(np.argmax(diffs)) + 1

    # Active region only (exclude POST_SETTLE)
    sp_after = setpoint[step_idx:]
    active_mask = np.abs(sp_after) > 0.5
    if not np.any(active_mask):
        return None
    end_active = int(np.where(active_mask)[0][-1]) + 1

    t = time_s[step_idx:step_idx + end_active] - time_s[step_idx]
    y = actual[step_idx:step_idx + end_active]

    if len(t) < 10:
        return None

    y0 = float(np.mean(actual[max(0, step_idx - 5):step_idx]))
    y_final = float(np.mean(y[-10:]))

    if abs(y_final - y0) < 0.01:
        return None

    y_norm = (y - y0) / (y_final - y0)

    # Rise time: 10% → 90%
    rise_time = None
    t10, t90 = None, None
    for i, yn in enumerate(y_norm):
        if t10 is None and yn >= 0.1:
            t10 = float(t[i])
        if t90 is None and yn >= 0.9:
            t90 = float(t[i])
            break
    if t10 is not None and t90 is not None:
        rise_time = t90 - t10

    # Settling time: last time outside ±2% band
    settling_time = None
    for i in range(len(y_norm) - 1, -1, -1):
        if abs(y_norm[i] - 1.0) > 0.02:
            if i + 1 < len(t):
                settling_time = float(t[i + 1])
            break

    # Overshoot %
    peak_val = float(np.max(y_norm))
    overshoot_pct = max(0.0, (peak_val - 1.0) * 100.0)

    # Peak time
    peak_idx = int(np.argmax(y_norm))
    peak_time = float(t[peak_idx])

    # Delay time: time to 50% of final value
    delay_time = None
    for i, yn in enumerate(y_norm):
        if yn >= 0.5:
            delay_time = float(t[i])
            break

    return {
        'rise_time': rise_time,
        'settling_time': settling_time,
        'overshoot_pct': overshoot_pct,
        'peak_time': peak_time,
        'delay_time': delay_time,
        'y_final': y_final,
        'y0': y0,
    }


def format_transfer_function(tf_data: dict, domain: str = 's') -> str:
    """Format transfer function as readable string.

    domain: 's' for s-domain, 'z' for z-domain
    """
    try:
        import numpy as np
    except ImportError:
        return "numpy not available"

    if domain == 's':
        num, den = tf_data['s_num'], tf_data['s_den']
        var = 's'
    else:
        num, den = tf_data['z_num'], tf_data['z_den']
        var = 'z'

    def poly_str(coeffs, var_name):
        terms = []
        order = len(coeffs) - 1
        for i, c in enumerate(coeffs):
            power = order - i
            if abs(c) < 1e-10:
                continue
            if power == 0:
                terms.append(f"{c:.4g}")
            elif power == 1:
                terms.append(f"{c:.4g}{var_name}")
            else:
                terms.append(f"{c:.4g}{var_name}^{power}")
        return " + ".join(terms) if terms else "0"

    num_str = poly_str(num, var)
    den_str = poly_str(den, var)
    return f"G({var}) = ({num_str}) / ({den_str})"


def compute_bode_data(tf_data: dict, f_min: float = 0.01,
                       f_max: float = 10.0, n_points: int = 200) -> Optional[dict]:
    """Compute Bode magnitude and phase data from transfer function.

    Returns dict with freq_hz, mag_db, phase_deg arrays.
    """
    try:
        import numpy as np
        from scipy.signal import freqs
    except ImportError:
        return None

    s_num = tf_data['s_num']
    s_den = tf_data['s_den']

    # Angular frequencies
    w = np.logspace(np.log10(2 * np.pi * f_min),
                    np.log10(2 * np.pi * f_max), n_points)

    w_out, h = freqs(s_num, s_den, worN=w)

    freq_hz = w_out / (2 * np.pi)
    mag_db = 20 * np.log10(np.abs(h) + 1e-20)
    phase_deg = np.degrees(np.unwrap(np.angle(h)))

    return {
        'freq_hz': freq_hz,
        'mag_db': mag_db,
        'phase_deg': phase_deg,
    }


# ── Panel widget ──

class SysIdPanel(QWidget):
    """System Identification panel for automated trim tuning."""

    command_requested = Signal(str)
    tagged_command_requested = Signal(str, str)  # (command, tag_name)
    gains_applied = Signal()  # emitted after Apply to Trim, so motion panel can refresh

    def __init__(self, parent=None):
        super().__init__(parent)
        self._tests: list[SysIdTestConfig] = []
        self._results: list[SysIdTestResult] = []
        self._current_idx = 0
        self._running = False
        self._download_offset = 0
        self._download_samples: list[SysIdSample] = []
        self._download_total = 0

        # Polling timer for test status
        self._poll_timer = QTimer(self)
        self._poll_timer.setInterval(500)
        self._poll_timer.timeout.connect(self._poll_status)

        self._setup_ui()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSpacing(6)

        # ── Test Plan Configuration ──
        plan_group = QGroupBox("Test Plan")
        plan_layout = QVBoxLayout(plan_group)

        top_row = QHBoxLayout()
        top_row.addWidget(QLabel("Preset:"))
        self._preset_combo = QComboBox()
        self._preset_combo.addItems(["Quick (3 steps)", "Standard (6 tests)",
                                      "Thorough (11 tests)", "Frequency Sweep (5 sine)"])
        self._preset_combo.currentIndexChanged.connect(self._on_preset_changed)
        top_row.addWidget(self._preset_combo)

        top_row.addWidget(QLabel("Direction:"))
        self._dir_combo = QComboBox()
        self._dir_combo.addItems(["CW", "CCW"])
        self._dir_combo.currentIndexChanged.connect(self._on_preset_changed)
        top_row.addWidget(self._dir_combo)
        top_row.addStretch()
        plan_layout.addLayout(top_row)

        # Test table
        self._test_table = QTableWidget(0, 5)
        self._test_table.setHorizontalHeaderLabels(
            ["#", "Type", "Speed (sps)", "Duration", "Status"])
        self._test_table.horizontalHeader().setSectionResizeMode(
            QHeaderView.Stretch)
        self._test_table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self._test_table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self._test_table.setMaximumHeight(180)
        plan_layout.addWidget(self._test_table)

        # Control buttons
        btn_row = QHBoxLayout()
        self._run_btn = QPushButton("Run All Tests")
        self._run_btn.clicked.connect(self._start_tests)
        btn_row.addWidget(self._run_btn)

        self._abort_btn = QPushButton("Abort")
        self._abort_btn.setEnabled(False)
        self._abort_btn.clicked.connect(self._abort_tests)
        btn_row.addWidget(self._abort_btn)

        self._progress = QProgressBar()
        self._progress.setTextVisible(True)
        self._progress.setFormat("Idle")
        btn_row.addWidget(self._progress)
        plan_layout.addLayout(btn_row)

        layout.addWidget(plan_group)

        # ── Response Plot ──
        plot_group = QGroupBox("Step Response")
        plot_layout = QVBoxLayout(plot_group)

        self._plot = pg.PlotWidget()
        self._plot.setLabel("left", "Speed (RPM)")
        self._plot.setLabel("bottom", "Time", units="s")
        self._plot.addLegend(offset=(10, 10))
        self._plot.setMinimumHeight(200)
        plot_layout.addWidget(self._plot)

        layout.addWidget(plot_group)

        # ── Per-Test Results ──
        results_group = QGroupBox("Per-Test Results")
        results_layout = QVBoxLayout(results_group)

        self._results_table = QTableWidget(0, 8)
        self._results_table.setHorizontalHeaderLabels([
            "#", "Type", "Speed", "K", "\u03c4 (s)", "\u03b8 (s)",
            "R\u00b2", "Transient"])
        self._results_table.horizontalHeader().setSectionResizeMode(
            QHeaderView.Stretch)
        self._results_table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self._results_table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self._results_table.setMaximumHeight(160)
        self._results_table.currentCellChanged.connect(
            lambda row, _col, _prev_row, _prev_col: self._on_result_selected(row))
        results_layout.addWidget(self._results_table)

        layout.addWidget(results_group)

        # ── Analysis (Tabs: Transfer Function | Pole-Zero s | Pole-Zero z | Bode) ──
        analysis_group = QGroupBox("Analysis")
        analysis_layout = QVBoxLayout(analysis_group)

        self._analysis_tabs = QTabWidget()

        # Transfer Function tab (shows equation + details for selected test)
        tf_tab = QWidget()
        tf_tab_layout = QVBoxLayout(tf_tab)
        tf_tab_layout.setContentsMargins(8, 8, 8, 8)
        self._tf_display = QTextEdit()
        self._tf_display.setReadOnly(True)
        self._tf_display.setFont(QFont("Consolas", 10))
        self._tf_display.setStyleSheet(
            "QTextEdit { background: #1a1a2e; color: #e0e0e0; "
            "border: 1px solid #333; }")
        self._tf_display.setPlaceholderText(
            "Select a completed test in the results table above "
            "to view its transfer function")
        tf_tab_layout.addWidget(self._tf_display)
        self._analysis_tabs.addTab(tf_tab, "Transfer Function")

        # Pole-Zero s-domain (with legend)
        pz_s_widget = QWidget()
        pz_s_layout = QVBoxLayout(pz_s_widget)
        pz_s_layout.setContentsMargins(0, 0, 0, 0)
        pz_s_layout.setSpacing(2)
        pz_s_legend = QLabel(
            '<span style="color:#e0e0e0; font-family:monospace;">'
            '<b style="color:#ff6666;">X</b> = Poles &nbsp;&nbsp; '
            '<b style="color:#66aaff;">O</b> = Zeros &nbsp;&nbsp; '
            '<span style="color:#888;">(RHP zeros = non-minimum phase from delay)</span>'
            '</span>')
        pz_s_legend.setAlignment(Qt.AlignCenter)
        pz_s_layout.addWidget(pz_s_legend)
        self._pz_s_plot = pg.PlotWidget()
        self._pz_s_plot.setLabel("left", "Imaginary")
        self._pz_s_plot.setLabel("bottom", "Real")
        self._pz_s_plot.setTitle("Pole-Zero (s-domain)")
        self._pz_s_plot.showGrid(x=True, y=True)
        self._pz_s_plot.setAspectLocked(True)
        self._pz_s_plot.setMinimumHeight(230)
        pz_s_layout.addWidget(self._pz_s_plot)
        self._analysis_tabs.addTab(pz_s_widget, "Pole-Zero (s)")

        # Pole-Zero z-domain (with legend)
        pz_z_widget = QWidget()
        pz_z_layout = QVBoxLayout(pz_z_widget)
        pz_z_layout.setContentsMargins(0, 0, 0, 0)
        pz_z_layout.setSpacing(2)
        pz_z_legend = QLabel(
            '<span style="color:#e0e0e0; font-family:monospace;">'
            '<b style="color:#ff6666;">X</b> = Poles &nbsp;&nbsp; '
            '<b style="color:#66aaff;">O</b> = Zeros &nbsp;&nbsp; '
            '<span style="color:#888;">(inside unit circle = stable)</span>'
            '</span>')
        pz_z_legend.setAlignment(Qt.AlignCenter)
        pz_z_layout.addWidget(pz_z_legend)
        self._pz_z_plot = pg.PlotWidget()
        self._pz_z_plot.setLabel("left", "Imaginary")
        self._pz_z_plot.setLabel("bottom", "Real")
        self._pz_z_plot.setTitle("Pole-Zero (z-domain)")
        self._pz_z_plot.showGrid(x=True, y=True)
        self._pz_z_plot.setAspectLocked(True)
        self._pz_z_plot.setMinimumHeight(230)
        pz_z_layout.addWidget(self._pz_z_plot)
        self._analysis_tabs.addTab(pz_z_widget, "Pole-Zero (z)")

        # Bode plot (magnitude + phase stacked)
        bode_widget = QWidget()
        bode_layout = QVBoxLayout(bode_widget)
        bode_layout.setContentsMargins(0, 0, 0, 0)
        bode_layout.setSpacing(2)

        self._bode_mag_plot = pg.PlotWidget()
        self._bode_mag_plot.setLabel("left", "Magnitude", units="dB")
        self._bode_mag_plot.setLabel("bottom", "")
        self._bode_mag_plot.showGrid(x=True, y=True)
        self._bode_mag_plot.setLogMode(x=True, y=False)
        self._bode_mag_plot.setMinimumHeight(120)
        bode_layout.addWidget(self._bode_mag_plot)

        self._bode_phase_plot = pg.PlotWidget()
        self._bode_phase_plot.setLabel("left", "Phase", units="\u00b0")
        self._bode_phase_plot.setLabel("bottom", "Frequency", units="Hz")
        self._bode_phase_plot.showGrid(x=True, y=True)
        self._bode_phase_plot.setLogMode(x=True, y=False)
        self._bode_phase_plot.setMinimumHeight(120)
        bode_layout.addWidget(self._bode_phase_plot)

        # Link X axes for Bode plots
        self._bode_phase_plot.setXLink(self._bode_mag_plot)

        self._analysis_tabs.addTab(bode_widget, "Bode Plot")

        analysis_layout.addWidget(self._analysis_tabs)
        layout.addWidget(analysis_group)

        # ── Model Fit + Trim Recommendation ──
        model_group = QGroupBox("Model Fit & Trim Recommendation")
        model_layout = QVBoxLayout(model_group)

        # FOPDT parameters
        fit_row = QHBoxLayout()
        self._fit_label = QLabel("Run tests to identify plant model")
        self._fit_label.setStyleSheet("color: gray;")
        fit_row.addWidget(self._fit_label)
        model_layout.addLayout(fit_row)

        # Linearity warning
        self._linearity_label = QLabel("")
        self._linearity_label.setStyleSheet("color: orange;")
        self._linearity_label.setVisible(False)
        model_layout.addWidget(self._linearity_label)

        # Tuning method
        tune_row = QHBoxLayout()
        tune_row.addWidget(QLabel("Tuning Method:"))
        self._method_combo = QComboBox()
        self._method_combo.addItems([
            "Gentle PI (Recommended)",
            "SIMC (Moderate)",
            "Lambda (Conservative)",
            "IMC (Aggressive)",
        ])
        self._method_combo.currentIndexChanged.connect(self._update_recommendation)
        tune_row.addWidget(self._method_combo)
        tune_row.addStretch()
        model_layout.addLayout(tune_row)

        # Recommended gains
        gain_row = QHBoxLayout()
        gain_row.addWidget(QLabel("Recommended:"))
        self._rec_kp = QLabel("Kp = --")
        self._rec_ki = QLabel("Ki = --")
        self._rec_kd = QLabel("Kd = --")
        for lbl in (self._rec_kp, self._rec_ki, self._rec_kd):
            lbl.setStyleSheet("font-weight: bold; font-family: monospace;")
            gain_row.addWidget(lbl)
        gain_row.addStretch()
        model_layout.addLayout(gain_row)

        # Apply buttons
        apply_row = QHBoxLayout()
        self._apply_btn = QPushButton("Apply to Trim")
        self._apply_btn.setEnabled(False)
        self._apply_btn.clicked.connect(self._apply_gains)
        apply_row.addWidget(self._apply_btn)

        self._apply_save_btn = QPushButton("Apply && Save to Flash")
        self._apply_save_btn.setEnabled(False)
        self._apply_save_btn.clicked.connect(self._apply_and_save)
        apply_row.addWidget(self._apply_save_btn)
        apply_row.addStretch()
        model_layout.addLayout(apply_row)

        layout.addWidget(model_group)
        layout.addStretch()

        # Load initial preset
        self._on_preset_changed()

    # ── Preset / table management ──

    def _on_preset_changed(self):
        preset_names = ["Quick", "Standard", "Thorough", "Frequency Sweep"]
        idx = self._preset_combo.currentIndex()
        forward = (self._dir_combo.currentIndex() == 0)
        self._tests = _make_preset(preset_names[idx], forward)
        self._refresh_table()

    def _refresh_table(self):
        self._test_table.setRowCount(len(self._tests))
        for i, t in enumerate(self._tests):
            self._test_table.setItem(i, 0, QTableWidgetItem(str(i + 1)))
            self._test_table.setItem(i, 1, QTableWidgetItem(t.type.title()))
            if t.type == "ramp":
                speed_str = f"{t.start_speed}\u2192{t.speed}"
            elif t.type == "sine":
                freq_hz = t.frequency_mhz / 1000.0
                speed_str = f"{t.start_speed}\u00b1{t.speed} @{freq_hz:.1f}Hz"
            else:
                speed_str = str(t.speed)
            self._test_table.setItem(i, 2, QTableWidgetItem(speed_str))
            dur_str = f"{t.duration_ms / 1000:.1f}s"
            self._test_table.setItem(i, 3, QTableWidgetItem(dur_str))
            self._test_table.setItem(i, 4, QTableWidgetItem("Pending"))

    # ── Test execution ──

    def _start_tests(self):
        if not self._tests or self._running:
            return
        self._running = True
        self._current_idx = 0
        self._results = []
        self._plot.clear()
        self._results_table.setRowCount(0)
        self._tf_display.clear()
        self._pz_s_plot.clear()
        self._pz_z_plot.clear()
        self._bode_mag_plot.clear()
        self._bode_phase_plot.clear()

        self._run_btn.setEnabled(False)
        self._abort_btn.setEnabled(True)
        self._preset_combo.setEnabled(False)
        self._dir_combo.setEnabled(False)
        self._apply_btn.setEnabled(False)
        self._apply_save_btn.setEnabled(False)

        self._progress.setMaximum(len(self._tests))
        self._progress.setValue(0)
        self._progress.setFormat(f"Test 1/{len(self._tests)}")

        self._refresh_table()
        self._send_current_test()

    def _send_current_test(self):
        if self._current_idx >= len(self._tests):
            self._on_all_complete()
            return

        test = self._tests[self._current_idx]
        self._update_table_status(self._current_idx, "Running...")

        dir_val = 1 if test.forward else 0
        if test.type == "step":
            cmd = (f"CTRL:SYSID:STEP {test.speed} {dir_val} "
                   f"{test.duration_ms} {test.settle_ms}")
        elif test.type == "ramp":
            cmd = (f"CTRL:SYSID:RAMP {test.start_speed} {test.speed} "
                   f"{dir_val} {test.duration_ms} {test.settle_ms}")
        elif test.type == "sine":
            cmd = (f"CTRL:SYSID:SINE {test.start_speed} {test.speed} "
                   f"{test.frequency_mhz} {dir_val} "
                   f"{test.duration_ms} {test.settle_ms}")
        elif test.type == "trapezoid":
            cmd = (f"CTRL:SYSID:TRAPEZOID {test.speed} {dir_val} "
                   f"{test.duration_ms} {test.settle_ms}")
        elif test.type == "rectangle":
            cmd = (f"CTRL:SYSID:RECT {test.speed} {dir_val} "
                   f"{test.duration_ms} {test.settle_ms}")
        else:
            log.error(f"Unknown test type: {test.type}")
            self._current_idx += 1
            self._send_current_test()
            return

        self.tagged_command_requested.emit(cmd, "SYSID_START")

        # Start polling for completion
        self._poll_timer.start()

    def _abort_tests(self):
        self._poll_timer.stop()
        self.tagged_command_requested.emit("CTRL:SYSID:ABORT", "SYSID_ABORT")
        self._running = False
        self._update_ui_idle()
        if self._current_idx < len(self._tests):
            self._update_table_status(self._current_idx, "Aborted")
        self._progress.setFormat("Aborted")

    def _poll_status(self):
        self.tagged_command_requested.emit(
            "CTRL:SYSID:STATUS?", "SYSID_STATUS")

    def _update_table_status(self, idx: int, status: str):
        if idx < self._test_table.rowCount():
            self._test_table.setItem(idx, 4, QTableWidgetItem(status))

    # ── Response handlers (called from main_window) ──

    def on_sysid_start_response(self, success: bool, data):
        if not success:
            log.error(f"SYSID start failed: {data}")
            self._abort_tests()

    def on_sysid_status_response(self, success: bool, data):
        if not success or not self._running:
            return

        # Parse phase from response
        phase = self._parse_phase(data)
        if phase == 4:  # DONE
            self._poll_timer.stop()
            self._update_table_status(self._current_idx, "Downloading...")
            self._start_download()

    def on_sysid_data_response(self, success: bool, raw_data):
        if not success or not self._running:
            return
        self._process_data_chunk(raw_data)

    def _parse_phase(self, data) -> int:
        """Extract phase from status response."""
        if isinstance(data, dict) and "phase" in data:
            return int(data["phase"])
        if isinstance(data, str):
            for part in data.split():
                if part.startswith("phase="):
                    return int(part.split("=")[1])
        return -1

    # ── Data download ──

    def _start_download(self):
        self._download_offset = 0
        self._download_samples = []
        self._download_total = 0
        self._request_data_chunk()

    def _request_data_chunk(self):
        chunk_size = 40
        self.tagged_command_requested.emit(
            f"CTRL:SYSID:DATA? {self._download_offset} {chunk_size}",
            "SYSID_DATA")

    def _process_data_chunk(self, raw_data):
        """Parse CSV data from SYSID:DATA? response."""
        if isinstance(raw_data, str):
            lines = raw_data.strip().split("\n")
        elif isinstance(raw_data, list):
            lines = raw_data
        elif hasattr(raw_data, "raw") and isinstance(raw_data.raw, str):
            lines = raw_data.raw.strip().split("\n")
        else:
            lines = str(raw_data).strip().split("\n")

        # Parse header: "SYSID_DATA <offset> <count> <total>"
        header_parsed = False
        for line in lines:
            line = line.strip()
            if not line or line == "END":
                continue
            if line.startswith("SYSID_DATA"):
                parts = line.split()
                if len(parts) >= 4:
                    self._download_total = int(parts[3])
                header_parsed = True
                continue
            if line.startswith("OK "):
                # Skip OK prefix in ASCII mode
                line = line[3:].strip()
                if line.startswith("SYSID_DATA"):
                    parts = line.split()
                    if len(parts) >= 4:
                        self._download_total = int(parts[3])
                    header_parsed = True
                    continue

            # Parse CSV: time_ms,setpoint_tps,actual_tps,pos_error
            parts = line.split(",")
            if len(parts) >= 4:
                try:
                    sample = SysIdSample(
                        time_ms=int(parts[0]),
                        setpoint_tps=int(parts[1]),
                        actual_tps=int(parts[2]),
                        pos_error=int(parts[3]),
                    )
                    self._download_samples.append(sample)
                except ValueError:
                    continue

        self._download_offset = len(self._download_samples)

        # Check if we have all samples
        if (self._download_total > 0 and
                self._download_offset >= self._download_total):
            self._on_test_data_complete()
        elif self._download_total > 0:
            # Request next chunk
            self._request_data_chunk()
        elif header_parsed:
            # Got header but total=0 — empty test
            self._on_test_data_complete()

    def _on_test_data_complete(self):
        """One test's data is fully downloaded."""
        test = self._tests[self._current_idx]
        result = SysIdTestResult(
            config=test,
            samples=list(self._download_samples),
        )

        # Fit FOPDT for step/rectangle tests (clean step input)
        # Trapezoid excluded: gradual ramp confuses step finder, produces garbage
        fittable = test.type in ("step", "rectangle")
        if fittable and len(self._download_samples) >= 20:
            fit = fit_fopdt(self._download_samples)
            if fit is not None:
                result.K, result.tau, result.theta, result.r_squared = fit
                result.fit_ok = True
                log.info(f"Test {self._current_idx + 1}: K={fit[0]:.3f} "
                         f"tau={fit[1]:.3f} theta={fit[2]:.3f} R\u00b2={fit[3]:.3f}")

                # Compute transfer functions
                result.tf_data = compute_transfer_functions(
                    result.K, result.tau, result.theta)

                # Compute transient metrics for step/rectangle tests
                result.transient = compute_transient_metrics(
                    self._download_samples)

        # Frequency-domain identification for sine tests
        if test.type == "sine" and len(self._download_samples) >= 20:
            freq_hz = test.frequency_mhz / 1000.0
            sine_fit = fit_sine_response(self._download_samples, freq_hz)
            if sine_fit is not None:
                result.sine_gain, result.sine_phase_deg, result.r_squared = sine_fit
                result.sine_freq_hz = freq_hz
                result.sine_fit_ok = True
                log.info(f"Test {self._current_idx + 1}: Sine "
                         f"f={freq_hz:.2f}Hz gain={result.sine_gain:.4f} "
                         f"phase={result.sine_phase_deg:.1f}\u00b0 "
                         f"R\u00b2={result.r_squared:.3f}")

        self._results.append(result)

        # Update results table
        self._update_results_table()

        status = "Done"
        if result.fit_ok:
            status = f"Done (R\u00b2={result.r_squared:.3f})"
        elif result.sine_fit_ok:
            status = f"Done (gain={result.sine_gain:.3f})"
        elif test.type == "sine":
            status = f"Done ({len(result.samples)} samples)"
        self._update_table_status(self._current_idx, status)

        # Plot this test's data
        self._plot_test(result, self._current_idx)

        # Move to next test
        self._current_idx += 1
        self._progress.setValue(self._current_idx)

        if self._current_idx < len(self._tests):
            self._progress.setFormat(
                f"Test {self._current_idx + 1}/{len(self._tests)}")
            self._send_current_test()
        else:
            self._on_all_complete()

    def _on_all_complete(self):
        """All tests finished."""
        self._running = False
        self._poll_timer.stop()
        self._update_ui_idle()
        self._progress.setFormat("Complete")
        self._progress.setValue(self._progress.maximum())

        # Fit aggregate model and show recommendations
        self._compute_aggregate_model()

        # Populate analysis plots from all fitted results
        self._plot_all_pole_zero()
        self._plot_all_bode()

    def _update_ui_idle(self):
        self._run_btn.setEnabled(True)
        self._abort_btn.setEnabled(False)
        self._preset_combo.setEnabled(True)
        self._dir_combo.setEnabled(True)

    # ── Plotting ──

    def _plot_test(self, result: SysIdTestResult, test_idx: int):
        """Add a test's response curve to the plot."""
        if not result.samples:
            return

        colors = [
            (66, 133, 244),   # blue
            (234, 67, 53),    # red
            (251, 188, 4),    # yellow
            (52, 168, 83),    # green
            (171, 71, 188),   # purple
            (255, 112, 67),   # orange
            (0, 172, 193),    # teal
            (124, 179, 66),   # lime
        ]
        color = colors[test_idx % len(colors)]

        time_s = [s.time_ms / 1000.0 for s in result.samples]
        # Convert encoder ticks/s to RPM: RPM = tps * 60 / encoder_PPR
        tps_to_rpm = 60.0 / 4000.0
        actual = [float(s.actual_tps) * tps_to_rpm for s in result.samples]
        setpoint = [float(s.setpoint_tps) * tps_to_rpm for s in result.samples]

        label = f"#{test_idx + 1} {result.config.type} {result.config.speed} sps"

        # Setpoint (dashed)
        self._plot.plot(time_s, setpoint, pen=pg.mkPen(color, width=1,
                        style=Qt.DashLine))
        # Actual (solid)
        self._plot.plot(time_s, actual, pen=pg.mkPen(color, width=2),
                        name=label)

    # ── Results table + selection ──

    def _update_results_table(self):
        """Refresh the per-test results table from self._results."""
        self._results_table.setRowCount(len(self._results))
        for i, r in enumerate(self._results):
            self._results_table.setItem(i, 0, QTableWidgetItem(str(i + 1)))
            self._results_table.setItem(i, 1, QTableWidgetItem(
                r.config.type.title()))
            self._results_table.setItem(i, 2, QTableWidgetItem(
                str(r.config.speed)))
            if r.fit_ok:
                self._results_table.setItem(i, 3, QTableWidgetItem(
                    f"{r.K:.4f}"))
                self._results_table.setItem(i, 4, QTableWidgetItem(
                    f"{r.tau:.3f}"))
                self._results_table.setItem(i, 5, QTableWidgetItem(
                    f"{r.theta:.3f}"))
                self._results_table.setItem(i, 6, QTableWidgetItem(
                    f"{r.r_squared:.3f}"))
                # Transient summary
                if r.transient:
                    tr = r.transient
                    parts = []
                    if tr.get('rise_time') is not None:
                        parts.append(f"t_r={tr['rise_time']:.2f}s")
                    if tr.get('settling_time') is not None:
                        parts.append(f"t_s={tr['settling_time']:.2f}s")
                    if tr.get('overshoot_pct') is not None:
                        parts.append(f"OS={tr['overshoot_pct']:.1f}%")
                    self._results_table.setItem(i, 7, QTableWidgetItem(
                        ", ".join(parts)))
                else:
                    self._results_table.setItem(i, 7, QTableWidgetItem("--"))
            elif r.sine_fit_ok:
                # Sine: show gain, phase, freq instead of K/tau/theta
                self._results_table.setItem(i, 3, QTableWidgetItem(
                    f"{r.sine_gain:.4f}"))
                self._results_table.setItem(i, 4, QTableWidgetItem(
                    f"{r.sine_phase_deg:.1f}\u00b0"))
                self._results_table.setItem(i, 5, QTableWidgetItem(
                    f"{r.sine_freq_hz:.2f}Hz"))
                self._results_table.setItem(i, 6, QTableWidgetItem(
                    f"{r.r_squared:.3f}"))
                gain_db = 20 * math.log10(r.sine_gain) if r.sine_gain > 0 else -99
                self._results_table.setItem(i, 7, QTableWidgetItem(
                    f"{gain_db:.1f}dB, {r.sine_phase_deg:.1f}\u00b0"))
            else:
                for col in range(3, 8):
                    self._results_table.setItem(i, col, QTableWidgetItem(
                        "--" if col < 7 else r.config.type))

    def _on_result_selected(self, row: int):
        """Show transfer function details for the selected test."""
        if row < 0 or row >= len(self._results):
            self._tf_display.clear()
            return

        # Switch to Transfer Function tab so the user sees the details
        self._analysis_tabs.setCurrentIndex(0)

        r = self._results[row]
        lines = [f"Test #{row + 1}: {r.config.type.title()} "
                 f"{r.config.speed} sps"]

        if r.fit_ok:
            lines.append(f"FOPDT: K={r.K:.4f}  \u03c4={r.tau:.3f}s  "
                         f"\u03b8={r.theta:.3f}s  R\u00b2={r.r_squared:.3f}")

            if r.tf_data:
                tf = r.tf_data
                lines.append("")
                lines.append("s-domain (Pad\u00e9 [1/1] delay approx):")
                lines.append(f"  {format_transfer_function(tf, 's')}")
                poles_s = ", ".join(f"{p:.3f}" for p in tf['s_poles'])
                zeros_s = ", ".join(f"{z:.3f}" for z in tf['s_zeros'])
                lines.append(f"  Poles: {poles_s}")
                lines.append(f"  Zeros: {zeros_s}")
                # Flag RHP zeros
                rhp_zeros = [z for z in tf['s_zeros'] if z.real > 0]
                if rhp_zeros:
                    lines.append(f"  \u26a0 RHP zero(s) — "
                                 f"non-minimum phase (delay artifact)")

                lines.append("")
                lines.append(f"z-domain (ZOH, T=50ms):")
                lines.append(f"  {format_transfer_function(tf, 'z')}")
                poles_z = ", ".join(f"{p:.4f}" for p in tf['z_poles'])
                zeros_z = ", ".join(f"{z:.4f}" for z in tf['z_zeros'])
                lines.append(f"  Poles: {poles_z}")
                lines.append(f"  Zeros: {zeros_z}")
                # Stability check
                unstable_z = [p for p in tf['z_poles'] if abs(p) > 1.0]
                if unstable_z:
                    lines.append(f"  \u26a0 Unstable — pole(s) outside unit circle!")
                lines.append(f"  DC gain: {tf['dc_gain']:.4f}")

            if r.transient:
                tr = r.transient
                lines.append("")
                lines.append("Transient characterization:")
                if tr.get('rise_time') is not None:
                    lines.append(f"  Rise time (10\u219290%): "
                                 f"{tr['rise_time']:.3f}s")
                if tr.get('delay_time') is not None:
                    lines.append(f"  Delay time (50%):    "
                                 f"{tr['delay_time']:.3f}s")
                if tr.get('peak_time') is not None:
                    lines.append(f"  Peak time:           "
                                 f"{tr['peak_time']:.3f}s")
                if tr.get('settling_time') is not None:
                    lines.append(f"  Settling time (\u00b12%): "
                                 f"{tr['settling_time']:.3f}s")
                if tr.get('overshoot_pct') is not None:
                    lines.append(f"  Overshoot:           "
                                 f"{tr['overshoot_pct']:.1f}%")
                lines.append(f"  Steady-state value:  "
                             f"{tr.get('y_final', 0):.1f} tps")
        elif r.sine_fit_ok:
            gain_db = 20 * math.log10(r.sine_gain) if r.sine_gain > 0 else -99
            lines.append(f"Frequency-domain identification "
                         f"@ {r.sine_freq_hz:.2f} Hz")
            lines.append("")
            lines.append(f"Gain:      {r.sine_gain:.4f}  ({gain_db:.2f} dB)")
            lines.append(f"Phase:     {r.sine_phase_deg:.1f}\u00b0")
            lines.append(f"R\u00b2:       {r.r_squared:.4f}  (sine fit quality)")
            lines.append("")
            lines.append("This is one point on the empirical Bode plot.")
            lines.append("Run a Frequency Sweep preset to build full Bode data.")
        else:
            if r.config.type == "sine":
                lines.append(f"Sine test: {len(r.samples)} samples collected")
                lines.append("Sine fitting failed (check signal amplitude)")
            elif r.config.type == "trapezoid":
                lines.append(f"Trapezoid test: {len(r.samples)} samples")
                lines.append("(FOPDT not applicable \u2014 gradual ramp has no clean step)")
            else:
                lines.append("FOPDT fitting failed for this test")

        self._tf_display.setPlainText("\n".join(lines))

    # ── Analysis plots ──

    _PZ_COLORS = [
        (66, 133, 244),   # blue
        (234, 67, 53),    # red
        (251, 188, 4),    # yellow
        (52, 168, 83),    # green
        (171, 71, 188),   # purple
        (255, 112, 67),   # orange
        (0, 172, 193),    # teal
        (124, 179, 66),   # lime
    ]

    def _plot_all_pole_zero(self):
        """Plot poles and zeros for all fitted tests on both s and z planes."""
        self._pz_s_plot.clear()
        self._pz_z_plot.clear()

        # Draw axes on s-domain plot
        self._pz_s_plot.addLine(x=0, pen=pg.mkPen('w', width=1, style=Qt.DashLine))
        self._pz_s_plot.addLine(y=0, pen=pg.mkPen('w', width=1, style=Qt.DashLine))

        # Draw unit circle on z-domain plot
        try:
            import numpy as np
            theta = np.linspace(0, 2 * np.pi, 100)
            uc_x = np.cos(theta)
            uc_y = np.sin(theta)
            self._pz_z_plot.plot(uc_x, uc_y,
                                 pen=pg.mkPen('w', width=1, style=Qt.DashLine))
        except ImportError:
            pass
        self._pz_z_plot.addLine(x=0, pen=pg.mkPen((80, 80, 80), width=1))
        self._pz_z_plot.addLine(y=0, pen=pg.mkPen((80, 80, 80), width=1))

        fitted = [(i, r) for i, r in enumerate(self._results)
                  if r.tf_data is not None]
        if not fitted:
            return

        for idx, (test_num, r) in enumerate(fitted):
            color = self._PZ_COLORS[idx % len(self._PZ_COLORS)]
            tf = r.tf_data

            # s-domain poles (×) and zeros (○)
            for pole in tf['s_poles']:
                self._pz_s_plot.plot(
                    [pole.real], [pole.imag],
                    symbol='x', symbolSize=14, symbolBrush=color,
                    symbolPen=pg.mkPen(color, width=2), pen=None)
            for zero in tf['s_zeros']:
                self._pz_s_plot.plot(
                    [zero.real], [zero.imag],
                    symbol='o', symbolSize=12, symbolBrush=None,
                    symbolPen=pg.mkPen(color, width=2), pen=None)

            # z-domain poles (×) and zeros (○)
            for pole in tf['z_poles']:
                self._pz_z_plot.plot(
                    [pole.real], [pole.imag],
                    symbol='x', symbolSize=14, symbolBrush=color,
                    symbolPen=pg.mkPen(color, width=2), pen=None)
            for zero in tf['z_zeros']:
                self._pz_z_plot.plot(
                    [zero.real], [zero.imag],
                    symbol='o', symbolSize=12, symbolBrush=None,
                    symbolPen=pg.mkPen(color, width=2), pen=None)

    def _plot_all_bode(self):
        """Plot Bode magnitude and phase for all fitted tests."""
        self._bode_mag_plot.clear()
        self._bode_phase_plot.clear()

        # Add reference lines
        self._bode_mag_plot.addLine(y=0,
                                     pen=pg.mkPen('w', width=1, style=Qt.DashLine))
        self._bode_phase_plot.addLine(y=-180,
                                       pen=pg.mkPen('r', width=1, style=Qt.DashLine))

        # FOPDT-derived Bode curves
        fitted = [(i, r) for i, r in enumerate(self._results)
                  if r.tf_data is not None]
        for idx, (test_num, r) in enumerate(fitted):
            color = self._PZ_COLORS[idx % len(self._PZ_COLORS)]

            bode = compute_bode_data(r.tf_data)
            if bode is None:
                continue

            freq = bode['freq_hz']
            mag = bode['mag_db']
            phase = bode['phase_deg']

            label = (f"#{test_num + 1} {r.config.type} "
                     f"{r.config.speed} sps")

            self._bode_mag_plot.plot(
                freq, mag,
                pen=pg.mkPen(color, width=2),
                name=label)
            self._bode_phase_plot.plot(
                freq, phase,
                pen=pg.mkPen(color, width=2))

        # Empirical sine data points (markers on Bode)
        sine_results = [(i, r) for i, r in enumerate(self._results)
                        if r.sine_fit_ok]
        if sine_results:
            sine_freqs = []
            sine_mags = []
            sine_phases = []
            for _, r in sine_results:
                sine_freqs.append(r.sine_freq_hz)
                gain_db = (20 * math.log10(r.sine_gain)
                           if r.sine_gain > 0 else -99)
                sine_mags.append(gain_db)
                sine_phases.append(r.sine_phase_deg)

            # Plot as large markers (empirical Bode points)
            marker_color = (255, 255, 0)  # yellow for visibility
            self._bode_mag_plot.plot(
                sine_freqs, sine_mags, pen=None,
                symbol='d', symbolSize=12,
                symbolBrush=marker_color,
                symbolPen=pg.mkPen(marker_color, width=2),
                name="Sine (empirical)")
            self._bode_phase_plot.plot(
                sine_freqs, sine_phases, pen=None,
                symbol='d', symbolSize=12,
                symbolBrush=marker_color,
                symbolPen=pg.mkPen(marker_color, width=2))

    # ── Model fitting ──

    def _compute_aggregate_model(self):
        """Average FOPDT parameters from all step test fits."""
        # Check scipy availability first
        try:
            import scipy  # noqa: F401
        except ImportError:
            self._fit_label.setText(
                "scipy not installed — run: pip install scipy")
            self._fit_label.setStyleSheet("color: red;")
            return

        step_fits = [r for r in self._results if r.fit_ok]

        if not step_fits:
            self._fit_label.setText("No valid step response fits obtained")
            self._fit_label.setStyleSheet("color: red;")
            return

        # Weighted average by R²
        total_weight = sum(r.r_squared for r in step_fits)
        if total_weight <= 0:
            total_weight = 1.0

        K_avg = sum(r.K * r.r_squared for r in step_fits) / total_weight
        tau_avg = sum(r.tau * r.r_squared for r in step_fits) / total_weight
        theta_avg = sum(r.theta * r.r_squared for r in step_fits) / total_weight
        r2_avg = sum(r.r_squared for r in step_fits) / len(step_fits)

        self._model_K = K_avg
        self._model_tau = tau_avg
        self._model_theta = theta_avg

        self._fit_label.setText(
            f"FOPDT: K = {K_avg:.4f}   \u03c4 = {tau_avg:.3f}s   "
            f"\u03b8 = {theta_avg:.3f}s   "
            f"(avg R\u00b2 = {r2_avg:.3f}, {len(step_fits)} tests)")
        self._fit_label.setStyleSheet("color: green; font-weight: bold;")

        # Linearity check
        if len(step_fits) >= 2:
            K_vals = [r.K for r in step_fits]
            K_min, K_max = min(K_vals), max(K_vals)
            if K_max > 0 and (K_max - K_min) / K_max > 0.20:
                self._linearity_label.setText(
                    f"Warning: Plant appears nonlinear \u2014 "
                    f"gain varies {K_min:.3f} to {K_max:.3f} across "
                    f"operating points. PID may need different tuning at "
                    f"different speeds.")
                self._linearity_label.setVisible(True)
            else:
                self._linearity_label.setVisible(False)
        else:
            self._linearity_label.setVisible(False)

        self._apply_btn.setEnabled(True)
        self._apply_save_btn.setEnabled(True)
        self._update_recommendation()

    def _update_recommendation(self):
        """Recalculate PID gains for the selected tuning method."""
        if not hasattr(self, "_model_K"):
            return

        methods = ["gentle", "simc", "lambda", "imc"]
        method = methods[self._method_combo.currentIndex()]

        Kp, Ki, Kd = recommend_pid(
            self._model_K, self._model_tau, self._model_theta, method)

        self._rec_Kp = Kp
        self._rec_Ki = Ki
        self._rec_Kd = Kd

        self._rec_kp.setText(f"Kp = {Kp:.4f}")
        self._rec_ki.setText(f"Ki = {Ki:.4f}")
        self._rec_kd.setText(f"Kd = {Kd:.4f}")

    # ── Apply gains ──

    def _apply_gains(self):
        if not hasattr(self, "_rec_Kp"):
            return
        Kp, Ki = self._rec_Kp, self._rec_Ki
        self.command_requested.emit(
            f"CTRL:TRIM:GAINS {Kp:.4f} {Ki:.4f}")
        # Trigger a refresh of the motion panel's trim controls after firmware processes
        QTimer.singleShot(200, self.gains_applied.emit)

    def _apply_and_save(self):
        self._apply_gains()
        # Small delay then save
        QTimer.singleShot(300, lambda: self.command_requested.emit(
            "CTRL:TRIM:SAVE"))
