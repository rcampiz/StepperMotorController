"""
Encoder measurement-chain scenarios: the frequency response of the velocity the firmware
reports, measured with the firmware's own sine system-identification test, and a search over
filter settings that picks the least lag for an acceptable noise floor.

Why: the drive loop and any closed-loop mode act on the filtered velocity. Every filter in the
chain (EMA, SMA, Pade, Butterworth, notch, Holt) buys smoothness with lag, and lag at the
frequencies the loop works at is what makes a loop sluggish or unstable. A sine sweep gives
gain and phase against frequency directly; no model fitting is needed.
"""

import math
import time
from typing import Any, Dict, List, Optional, Tuple

from .. import commands as C
from .. import views as V
from . import ScenarioContext, scenario


def _fit_sine(t_s: List[float], y: List[float], f_hz: float) -> Tuple[float, float, float]:
    """Least-squares amplitude, phase (rad) and residual std of y at f_hz (mean removed)."""
    n = len(y)
    mean = sum(y) / n
    w = 2.0 * math.pi * f_hz
    c = sum((v - mean) * math.cos(w * t) for v, t in zip(y, t_s))
    s = sum((v - mean) * math.sin(w * t) for v, t in zip(y, t_s))
    amp = 2.0 * math.hypot(c, s) / n
    phase = math.atan2(-s, c)                       # y ~ amp cos(w t + phase)
    resid = [(v - mean) - amp * math.cos(w * t + phase) for v, t in zip(y, t_s)]
    noise = (sum(r * r for r in resid) / n) ** 0.5
    return amp, phase, noise


def _run_sine(ctx: ScenarioContext, base_sps: int, amp_sps: int, f_hz: float, cycles: float,
              timeout_s: float) -> List[Tuple[int, int, int, int]]:
    # 100 samples per test whatever its length: keep tests short so the sample interval stays
    # well above the Nyquist rate of the highest frequency (6 s -> 60 ms -> fine to 3 Hz)
    dur_ms = int(min(6000, max(2500, cycles / f_hz * 1000.0)))
    start = ctx.call(C.sysid_sine(base_sps, amp_sps, int(round(f_hz * 1000)), 1, dur_ms, 500))
    ctx.require(f"sine {f_hz} Hz start", start.success, start.error_message or "")
    deadline = time.monotonic() + dur_ms / 1000.0 + timeout_s
    status = {"phase": -1, "samples": 0}
    while time.monotonic() < deadline:
        ctx.sleep(0.2)
        status = V.sysid_status(ctx.call(C.sysid_status()))
        if status["phase"] == V.SYSID_DONE:
            break
    ctx.require(f"sine {f_hz} Hz done", status["phase"] == V.SYSID_DONE, f"phase={status['phase']}")
    samples: List[Tuple[int, int, int, int]] = []
    total: Optional[int] = None
    while total is None or len(samples) < total:
        chunk = V.sysid_data(ctx.call(C.sysid_data(len(samples), 40)))
        total = chunk.total if chunk.total >= 0 else len(samples)
        if not chunk.samples:
            break
        samples.extend(chunk.samples)
    return samples


def _response(samples: List[Tuple[int, int, int, int]], f_hz: float) -> Dict[str, float]:
    """gain, lag_ms and noise (counts/s) of the reported velocity against the setpoint."""
    # The firmware logs 100 samples over the test including its settle; rows outside the
    # active sine carry a zero setpoint, and pos_error saturates when the encoder count and
    # the motor position have drifted apart, so only the setpoint says which rows are live.
    live = [s for s in samples if s[1] != 0]
    # The sine starts from standstill: the first half second is the acceleration transient
    if live:
        t0 = live[0][0]
        live = [s for s in live if s[0] - t0 >= 500]
    if len(live) < 8:
        return {"gain": 0.0, "lag_ms": 0.0, "noise": 0.0, "samples": len(live)}
    t_s = [s[0] / 1000.0 for s in live]
    sp = [float(s[1]) for s in live]
    ac = [float(s[2]) for s in live]
    a_sp, p_sp, _ = _fit_sine(t_s, sp, f_hz)
    a_ac, p_ac, noise = _fit_sine(t_s, ac, f_hz)
    gain = a_ac / a_sp if a_sp > 0 else 0.0
    lag = p_sp - p_ac                                # positive: the reported velocity lags
    lag = (lag + math.pi) % (2.0 * math.pi) - math.pi
    return {"gain": round(gain, 3), "lag_ms": round(lag / (2.0 * math.pi * f_hz) * 1000.0, 1),
            "noise": round(noise, 1), "samples": len(samples)}


# Candidate filter chains for the search. Each is the list of requests that configures it;
# the firmware keeps whatever a candidate does not mention, so every candidate states all six.
def _chain(ema=None, sma=None, pade=None, biquad=None, notch=False, holt=False) -> List[C.Request]:
    return [
        C.enc_filter_ema(ema is not None, ema),
        C.enc_filter_sma(sma is not None, sma),
        C.enc_filter_pade(pade is not None, pade),
        C.enc_filter_biquad(biquad is not None, biquad),
        C.enc_filter_notch(bool(notch)),
        C.enc_filter_holt(bool(holt)),
    ]


CANDIDATES: Dict[str, List[C.Request]] = {
    "no filters": _chain(),
    "biquad 20 Hz": _chain(biquad=20),
    "biquad 10 Hz": _chain(biquad=10),
    "ema 100": _chain(ema=100),
    "ema 100 + biquad 10 Hz": _chain(ema=100, biquad=10),
}


def _restore_requests(cfg: Dict[str, Any]) -> List[C.Request]:
    """Requests that put the chain back to a CTRL:ENC:FILT? snapshot."""
    g = cfg.get
    return [
        C.enc_filter_window(int(g("meas_window_ms", 40))),
        C.enc_filter_ema(bool(g("ema_enabled", False)), int(g("ema_alpha", 100))),
        C.enc_filter_sma(bool(g("sma_enabled", False)), int(g("sma_window", 8))),
        C.enc_filter_pade(bool(g("pade_enabled", False)), int(g("pade_gain", 50)), int(g("pade_max_corr", 50))),
        C.enc_filter_biquad(bool(g("biquad_enabled", False)), int(g("biquad_cutoff_hz", 10))),
        C.enc_filter_notch(bool(g("notch_enabled", False)), int(g("notch_center_hz", 25)), int(g("notch_q10", 50))),
        C.enc_filter_holt(bool(g("holt_enabled", False)), int(g("holt_alpha", 51)), int(g("holt_beta", 13))),
    ]


@scenario("filter_bode", "Frequency response of the reported velocity: the firmware's sine "
                         "system-identification test at each frequency in freqs_hz around "
                         "baseline_rpm, fitted for gain, lag and noise. mode 'configured' measures "
                         "the current filter chain; 'search' measures the built-in candidate chains "
                         "too, recommends the least lag at target_hz whose noise stays within "
                         "noise_factor of the quietest, and applies it when apply is true "
                         "(CTRL:ENC:FILT:SAVE keeps it). Restores the chain otherwise.",
          params={"baseline_rpm": 90, "amplitude_rpm": 20, "freqs_hz": [0.3, 0.5, 1.0, 1.5, 2.0, 3.0],
                  "cycles": 3, "target_hz": 2.0, "mode": "configured", "noise_factor": 1.5,
                  "apply": False, "timeout_s": 10},
          moves_motor=True)
def filter_bode(ctx: ScenarioContext, params: Dict[str, Any]) -> None:
    full = int(ctx.call(C.full_steps_query()).data.get("full_steps_per_rev", 200) or 200)
    base_sps = int(round(float(params["baseline_rpm"]) / 60.0 * full))
    amp_sps = int(round(float(params["amplitude_rpm"]) / 60.0 * full))
    freqs = [float(f) for f in params["freqs_hz"]]
    target = float(params["target_hz"])
    cycles, timeout = float(params["cycles"]), float(params["timeout_s"])
    original = ctx.call(C.enc_filter_query())
    ctx.require("read filter chain", original.success, original.error_message or "")
    orig_cfg = dict(original.data or {})
    ctx.measure("original_chain", orig_cfg)
    ctx.call(C.fault_clear())
    ctx.require("enable", ctx.call(C.mot_enable()).success)

    def sweep(label: str) -> Dict[str, Any]:
        rows = []
        for f in freqs:
            rows.append(_response(_run_sine(ctx, base_sps, amp_sps, f, cycles, timeout), f))
        lag_t = _interp(freqs, [r["lag_ms"] for r in rows], target)
        gain_t = _interp(freqs, [r["gain"] for r in rows], target)
        bw = next((f for f, r in zip(freqs, rows) if r["gain"] < 0.707), None)
        out = {"freqs": freqs, "gain": [r["gain"] for r in rows], "lag_ms": [r["lag_ms"] for r in rows],
               "noise": [r["noise"] for r in rows], "lag_at_target_ms": round(lag_t, 1),
               "gain_at_target": round(gain_t, 3), "bandwidth_hz": bw,
               "noise_mean": round(sum(r["noise"] for r in rows) / len(rows), 1)}
        ctx.measure(f"chain_{label}", out)
        return out

    results: Dict[str, Dict[str, Any]] = {}
    applied: Optional[str] = None
    try:
        results["configured"] = sweep("configured")
        if params["mode"] == "search":
            for name, reqs in CANDIDATES.items():
                for r in reqs:
                    ctx.require(f"{name}: {r.name}", ctx.call(r).success)
                results[name] = sweep(name)
            quietest = min(r["noise_mean"] for r in results.values())
            ok = {n: r for n, r in results.items() if r["noise_mean"] <= float(params["noise_factor"]) * max(quietest, 0.5)}
            best = min(ok, key=lambda n: ok[n]["lag_at_target_ms"])
            ctx.measure("recommended", best)
            ctx.measure("recommended_lag_ms", ok[best]["lag_at_target_ms"])
            if params.get("apply") and best != "configured":
                for r in CANDIDATES[best]:
                    ctx.call(r)
                applied = best
        ctx.measure("bode", results)
        ctx.measure("target_hz", target)
        ctx.measure("applied", applied)
    finally:
        ctx.call(C.mot_stop())
        ctx.call(C.mot_disable())
        if applied is None:
            for r in _restore_requests(orig_cfg):
                ctx.call(r)


def _interp(xs: List[float], ys: List[float], x: float) -> float:
    if x <= xs[0]:
        return ys[0]
    for x0, x1, y0, y1 in zip(xs, xs[1:], ys, ys[1:]):
        if x0 <= x <= x1:
            return y0 + (y1 - y0) * (x - x0) / (x1 - x0) if x1 > x0 else y0
    return ys[-1]
