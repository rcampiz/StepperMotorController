"""
Resonance profile: where the mechanical resonance sits against the run current.

A stepper and its load act as a mass on a magnetic spring: the run current sets the spring's
stiffness, so the natural frequency f_n moves with it (bench 2026-10-21: ~132 Hz at 1.01 A,
116 Hz at 0.78 A, ~104 Hz at 0.62 A, close to f_n ~ sqrt(I)). The steps excite it at harmonics
of the electrical frequency f_e = rpm / 60 x full_steps / 4; a resonance shows where k x f_e
crosses f_n. This scenario maps the ripple over (run current, speed) with the 2 kHz position
capture (the raw encoder against the chip's step count, no velocity filter), finds the peaks,
names their harmonic and fits f_n = f_ref x (I / I_ref) ^ exponent: the model the firmware's
resonance avoider needs. The analysis functions are pure (host-tested).
"""

import math
from typing import Any, Dict, List, Optional, Sequence, Tuple

from .. import commands as C
from .. import views as V
from . import ScenarioContext, scenario
from .motion import _chip_geometry, _dft_peak_hz

HARMONIC_TOL = 0.08          # a peak belongs to harmonic k when within 8 % of k x f_e


def electrical_hz(rpm: float, full_steps_per_rev: int = 200) -> float:
    """The motor's electrical frequency: one cycle per 4 full steps."""
    return rpm / 60.0 * full_steps_per_rev / 4.0


def harmonic_of(peak_hz: float, rpm: float, full_steps_per_rev: int = 200,
                harmonics: Sequence[int] = (1, 2, 4)) -> Optional[int]:
    """Which harmonic of the electrical frequency a wobble is (None: none of `harmonics`)."""
    fe = electrical_hz(rpm, full_steps_per_rev)
    if fe <= 0.0:
        return None
    best = min(harmonics, key=lambda k: abs(peak_hz - k * fe))
    return best if abs(peak_hz - best * fe) <= HARMONIC_TOL * best * fe else None


def find_resonances(rows: List[Dict[str, Any]], full_steps_per_rev: int = 200,
                    factor: float = 1.5) -> List[Dict[str, Any]]:
    """The resonant points of a profile: per current, the local maxima of the ripple that stand
    `factor` above that current's median. The speed is refined by a parabola through the peak
    and its neighbours, the natural frequency is the harmonic x f_e at that speed."""
    out: List[Dict[str, Any]] = []
    for cur in sorted({r["run_a"] for r in rows}):
        line = sorted((r for r in rows if r["run_a"] == cur and r.get("ok", True)),
                      key=lambda r: r["rpm"])
        if len(line) < 3:
            continue
        rip = [r["ripple"] for r in line]
        med = sorted(rip)[len(rip) // 2]
        for i, r in enumerate(line):
            left = rip[i - 1] if i > 0 else -1.0
            right = rip[i + 1] if i + 1 < len(rip) else -1.0
            if not (rip[i] >= left and rip[i] >= right and rip[i] >= factor * med):
                continue
            rpm = float(r["rpm"])
            if 0 < i < len(line) - 1:                     # parabolic refinement of the speed
                denom = left - 2.0 * rip[i] + right
                step = line[i + 1]["rpm"] - r["rpm"]
                if denom < 0.0:
                    rpm += 0.5 * (left - right) / denom * step
            k = harmonic_of(r["peak_hz"], r["rpm"], full_steps_per_rev)
            if k is None:
                continue
            out.append({"run_a": cur, "rpm": round(rpm, 1), "harmonic": k,
                        "fn_hz": round(k * electrical_hz(rpm, full_steps_per_rev), 1),
                        "ripple": rip[i], "median": med})
    return out


def fit_model(resonances: List[Dict[str, Any]], i_ref: float) -> Optional[Dict[str, Any]]:
    """f_n = f_ref x (I / i_ref) ^ exponent, least squares in log-log over the resonances.
    One resonance per current at least two currents apart; None when there is not enough."""
    pts = [(r["run_a"], r["fn_hz"]) for r in resonances if r["run_a"] > 0 and r["fn_hz"] > 0]
    if len({p[0] for p in pts}) < 2:
        return None
    xs = [math.log(p[0] / i_ref) for p in pts]
    ys = [math.log(p[1]) for p in pts]
    n = len(pts)
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    if sxx <= 0.0:
        return None
    b = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
    a = my - b * mx
    f_ref = math.exp(a)
    resid = [p[1] - f_ref * (p[0] / i_ref) ** b for p in pts]
    return {"f_ref_hz": round(f_ref, 1), "i_ref_a": round(i_ref, 3), "exponent": round(b, 3),
            "rms_resid_hz": round((sum(e * e for e in resid) / n) ** 0.5, 1), "points": n}


def resonant_rpm(model: Dict[str, Any], current_a: float, harmonic: int,
                 full_steps_per_rev: int = 200) -> float:
    """The speed at which harmonic `harmonic` meets the natural frequency at `current_a`."""
    fn = model["f_ref_hz"] * (current_a / model["i_ref_a"]) ** model["exponent"]
    return fn / harmonic * 4.0 / full_steps_per_rev * 60.0


def _capture_tail(ctx: ScenarioContext, samples: int) -> Tuple[List[Tuple[int, int]], Dict[str, Any]]:
    f = ctx.call(C.cap_freeze())
    ctx.require("freeze", f.success, f.error_message or "")
    info = dict(f.data)
    n = int(info.get("samples", 0))
    off = max(0, n - samples)
    rows: List[Tuple[int, int]] = []
    while off < n:
        d = ctx.call(C.cap_data(off, min(256, n - off)))
        ctx.require(f"read {off}", d.success, d.error_message or "")
        chunk = V.capture_rows(d)
        if not chunk:
            break
        rows.extend(chunk)
        off += len(chunk)
    ctx.call(C.cap_resume())
    return rows, info


GLITCH_COUNTS = 50.0         # no shaft moves the error this far between two samples (0.5 ms)


def capture_glitch(err: List[float]) -> bool:
    """A capture with a sample-to-sample error jump no shaft can make: corrupt readout
    (bench 2026-10-21: a 5672-count jump inside 0.5 s at 75 RPM, impossible even stalled)."""
    return any(abs(b - a) > GLITCH_COUNTS for a, b in zip(err, err[1:]))


def _ripple(rows: List[Tuple[int, int]], info: Dict[str, Any], cpr: int, full: int,
            rate: float) -> Dict[str, Any]:
    enc = V.unwrap16([r[0] for r in rows])
    syn = V.unwrap16([r[1] for r in rows])
    cpp = cpr / float(full * int(info.get("pulses_per_full_step", 8)))
    err = [(e - enc[0]) - (s - syn[0]) * cpp for e, s in zip(enc, syn)]
    m = sum(err) / len(err)
    err = [x - m for x in err]
    std = (sum(x * x for x in err) / len(err)) ** 0.5
    pk_hz, pk_amp = _dft_peak_hz(err, float(info.get("rate_hz", rate)))
    travel_enc = enc[-1] - enc[0]
    travel_cmd = (syn[-1] - syn[0]) * cpp
    return {"ripple": round(std, 2), "p2p": round(max(err) - min(err), 1),
            "peak_hz": round(pk_hz, 1), "peak_amp": round(pk_amp, 2),
            "ok": abs(travel_enc - travel_cmd) <= 0.02 * abs(travel_cmd) + 4,
            "glitch": capture_glitch(err)}


def _measure(ctx: ScenarioContext, samples: int, cpr: int, full: int, rate: float,
             fill_s: float) -> Optional[Dict[str, Any]]:
    """One capture's ripple; a corrupt capture is taken again once (`retried` says so)."""
    r = None
    for attempt in range(2):
        cap_rows, info = _capture_tail(ctx, samples)
        if len(cap_rows) < 64:
            return None
        r = _ripple(cap_rows, info, cpr, full, rate)
        r["retried"] = attempt > 0
        if not r["glitch"]:
            return r
        ctx.sleep(fill_s)                  # the rings refill after the resume
    return r


@scenario("resonance_profile",
          "Map the position ripple over run current and speed with the 2 kHz capture (the raw "
          "encoder against the chip's step count), find the resonant peaks, name their harmonic "
          "of the electrical frequency and fit the natural frequency against current "
          "(f_n = f_ref x (I / I_ref) ^ exponent). The motor keeps running across the speeds of "
          "one current; each current is applied stopped (DRV:CFG:APPLY). Accel, decel and hold "
          "currents are not changed; everything is restored. Current mode, forward only.",
          params={"current_fractions": [0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.15], "rpm_from": 30,
                  "rpm_to": 150, "rpm_step": 5, "settle_s": 0.35, "samples": 1024,
                  "rate_hz": 2000, "first_settle_s": 1.0, "peak_factor": 1.5,
                  "apply": False, "band_pct": 8, "boost_pct": 40, "cut_pct": 30},
          moves_motor=True)
def resonance_profile(ctx: ScenarioContext, params: Dict[str, Any]) -> None:
    geom = _chip_geometry(ctx)
    full, cpr = geom["full_steps_per_rev"], geom["encoder_cpr"] or 4000
    rate, samples = int(params["rate_hz"]), int(params["samples"])
    cur = ctx.call(C.cfg_query("CURRENT"))
    ctx.require("read the currents", cur.success and "run_a" in (cur.data or {}),
                cur.error_message or "no run_a (current mode only)")
    c0 = dict(cur.data)
    cap = max(c0["run_a"], c0["acc_a"], c0["dec_a"])        # never above what the ramps use
    currents = sorted({round(min(cap, c0["run_a"] * float(f)), 3) for f in params["current_fractions"]})
    rpms = list(range(int(params["rpm_from"]), int(params["rpm_to"]) + 1, int(params["rpm_step"])))
    rate0 = int((ctx.call(C.enc_filter_query()).data or {}).get("sample_rate_hz", 1000))
    ctx.measure("original_currents", c0)
    ctx.measure("currents_a", currents)
    rows: List[Dict[str, Any]] = []
    try:
        ctx.require("sample rate", ctx.call(C.enc_sample_rate(rate)).success)
        for ia in currents:
            ctx.call(C.mot_stop())
            ctx.sleep(0.4)
            ctx.require(f"set {ia} A", ctx.call(C.cfg_current(c0["hold_a"], ia, c0["acc_a"], c0["dec_a"])).success)
            ctx.require(f"apply {ia} A", ctx.call(C.cfg_apply()).success)
            ia_used = float((ctx.call(C.cfg_query("CURRENT")).data or {}).get("run_a", ia))
            ctx.call(C.fault_clear())
            ctx.require("enable", ctx.call(C.mot_enable()).success)
            first = True
            for rpm in rpms:
                ctx.require(f"run {rpm}", ctx.call(C.mot_run_rpm(rpm, 1)).success)
                ctx.sleep((float(params["first_settle_s"]) if first else float(params["settle_s"]))
                          + samples / float(rate) + 0.05)
                first = False
                r = _measure(ctx, samples, cpr, full, rate, samples / float(rate) + 0.05)
                if r is None:
                    continue
                r.update({"run_a": round(ia_used, 3), "rpm": rpm,
                          "harmonic": harmonic_of(r["peak_hz"], rpm, full)})
                rows.append(r)
                if not r["ok"]:
                    ctx.check(f"no lost steps at {ia_used:.2f} A {rpm} RPM", False)
                    break                                  # this current is too weak here
    finally:
        ctx.call(C.cap_resume())
        ctx.call(C.mot_stop())
        ctx.sleep(0.4)
        ctx.call(C.cfg_current(c0["hold_a"], c0["run_a"], c0["acc_a"], c0["dec_a"]))
        ctx.call(C.cfg_apply())
        ctx.call(C.enc_sample_rate(rate0))
        ctx.call(C.mot_disable())
    ctx.measure("rows", rows)
    res = find_resonances(rows, full, float(params["peak_factor"]))
    ctx.measure("resonances", res)
    model = fit_model(res, c0["run_a"])
    ctx.measure("model", model)
    if model is not None:
        ctx.measure("predicted_rpm_at_run_current",
                    {k: round(resonant_rpm(model, c0["run_a"], k, full), 1) for k in (1, 2, 4)})
        ctx.measure("f1a_hz", round(f1a_hz(model), 1))
        if params.get("apply"):
            r = ctx.call(C.res_config(True, f1a_hz(model), model["exponent"], int(params["band_pct"]),
                                      int(params["boost_pct"]), int(params["cut_pct"])))
            ctx.require("avoider configured", r.success, r.error_message or "")
            ctx.require("avoider saved", ctx.call(C.res_save()).success)
            ctx.measure("applied", True)
    ctx.check("profile measured", len(rows) >= len(rpms))


def f1a_hz(model: Dict[str, Any]) -> float:
    """The model's natural frequency at 1 A, the firmware's reference (CTRL:RES:CFG)."""
    return model["f_ref_hz"] * (1.0 / model["i_ref_a"]) ** model["exponent"]


@scenario("resonance_check",
          "Ripple with the resonance avoider off and on at the same speeds: the motor runs through "
          "rpm_list twice (2 kHz position capture at each), the avoider switched off then on "
          "(CTRL:RES:CFG, not saved; its saved state is restored). Reports the ripple, the "
          "avoider's state and the run current in force per speed. Needs a stored model.",
          params={"rpm_list": [40, 60, 70, 75, 80, 85, 90, 100, 120], "settle_s": 0.5,
                  "samples": 1024, "rate_hz": 2000},
          moves_motor=True)
def resonance_check(ctx: ScenarioContext, params: Dict[str, Any]) -> None:
    geom = _chip_geometry(ctx)
    full, cpr = geom["full_steps_per_rev"], geom["encoder_cpr"] or 4000
    rate, samples = int(params["rate_hz"]), int(params["samples"])
    st = ctx.call(C.res_query())
    ctx.require("read the avoider", st.success, st.error_message or "")
    cfg0 = dict(st.data)
    ctx.require("a stored model", float(cfg0.get("f1a_hz", 0)) > 0, "run resonance_profile with apply")
    rate0 = int((ctx.call(C.enc_filter_query()).data or {}).get("sample_rate_hz", 1000))
    out: Dict[str, List[Dict[str, Any]]] = {"off": [], "on": []}
    try:
        ctx.require("sample rate", ctx.call(C.enc_sample_rate(rate)).success)
        ctx.call(C.fault_clear())
        ctx.require("enable", ctx.call(C.mot_enable()).success)
        for label, on in (("off", False), ("on", True)):
            ctx.require(f"avoider {label}", ctx.call(C.res_config(on)).success)
            first = True
            for rpm in params["rpm_list"]:
                ctx.require(f"run {rpm}", ctx.call(C.mot_run_rpm(float(rpm), 1)).success)
                ctx.sleep((1.0 if first else float(params["settle_s"])) + samples / float(rate) + 0.05)
                first = False
                s = ctx.call(C.res_query()).data or {}
                r = _measure(ctx, samples, cpr, full, rate, samples / float(rate) + 0.05)
                if r is None:
                    continue
                r.update({"rpm": rpm, "state": s.get("state"), "run_a": s.get("run_a")})
                out[label].append(r)
    finally:
        ctx.call(C.cap_resume())
        ctx.call(C.mot_stop())
        ctx.call(C.res_config(bool(cfg0.get("enabled"))))
        ctx.call(C.enc_sample_rate(rate0))
        ctx.call(C.mot_disable())
    ctx.measure("off", out["off"])
    ctx.measure("on", out["on"])
    worst_off = max((r["ripple"] for r in out["off"]), default=0.0)
    worst_on = max((r["ripple"] for r in out["on"]), default=0.0)
    ctx.measure("worst_ripple_off", worst_off)
    ctx.measure("worst_ripple_on", worst_on)
    ctx.check("no lost steps", all(r["ok"] or r["glitch"] for r in out["off"] + out["on"]))
    ctx.check("no corrupt captures left", not any(r["glitch"] for r in out["off"] + out["on"]))
    ctx.check("both passes measured", len(out["off"]) == len(out["on"]) == len(params["rpm_list"]))


# ---------------------------------------------------------------------------------------------
# Ramps through the resonance: how the slope and the ramp current excite it
# ---------------------------------------------------------------------------------------------
# A ramp sweeps the excitation (k x the electrical frequency) through f_n: the rotor, a damped
# mass on a magnetic spring, rings up while the sweep is near f_n. Slow sweeps linger and build
# it up; fast ones pass before it grows but load the rotor harder (the lag during the ramp is
# torque spent accelerating). The powerSTEP01 takes a new acceleration only while stopped
# (bench 2026-10-21), its ramp currents (TVAL_ACC / TVAL_DEC) at any time.

OSC_WINDOW = 40              # samples (20 ms at 2 kHz): the slow part removed to leave the ringing


def _moving_mean(x: List[float], n: int) -> List[float]:
    out, acc, q = [], 0.0, []
    half = n // 2
    pad = [x[0]] * half + list(x) + [x[-1]] * (n - half)
    for i, v in enumerate(pad):
        acc += v
        q.append(v)
        if len(q) > n:
            acc -= q.pop(0)
        if i >= n:
            out.append(acc / n)
    return out[:len(x)]


def analyse_ramp(enc: List[float], cmd: List[float], rate_hz: float, low_cps: float,
                 high_cps: float, counts_per_full_step: float) -> Dict[str, Any]:
    """One up-and-down ramp captured at rate_hz: encoder and commanded counts, sample by sample.
    Windows from the commanded speed; in each, the ringing (error minus its 20 ms mean: peak
    and rms, counts) and the lag the ramp adds to the cruise lag (full steps)."""
    n = len(enc)
    err = [e - c for e, c in zip(enc, cmd)]
    slow = _moving_mean(err, OSC_WINDOW)
    osc = [e - s_ for e, s_ in zip(err, slow)]
    sp = [0.0] * n                                    # commanded speed, counts/s, over 5 ms
    k = max(1, int(rate_hz * 0.005))
    for i in range(k, n):
        sp[i] = (cmd[i] - cmd[i - k]) * rate_hz / k
    for i in range(k):
        sp[i] = sp[k] if n > k else 0.0
    lo, hi = low_cps * 1.05, high_cps * 0.95
    rising = [i for i in range(1, n) if lo < sp[i] < hi and sp[i] >= sp[i - 1]]
    falling = [i for i in range(1, n) if lo < sp[i] < hi and sp[i] < sp[i - 1]]
    cruise_low = [i for i in range(n) if abs(sp[i] - low_cps) <= 0.03 * low_cps]
    base = sum(slow[i] for i in cruise_low[:200]) / max(1, len(cruise_low[:200])) if cruise_low else slow[0]

    def window(idx: List[int], extra: int = 0) -> Dict[str, float]:
        if not idx:
            return {"peak": 0.0, "rms": 0.0, "lag_fs": 0.0}
        a, b = idx[0], min(n, idx[-1] + extra)
        seg = osc[a:b]
        lag = max((abs(slow[i] - base) for i in range(a, b)), default=0.0)
        return {"peak": round(max(abs(v) for v in seg), 2),
                "rms": round((sum(v * v for v in seg) / len(seg)) ** 0.5, 2),
                "lag_fs": round(lag / counts_per_full_step, 3)}

    ring = int(rate_hz * 0.15)                        # 150 ms of ringing after each ramp
    up, down = window(rising, ring), window(falling, ring)
    end_err = sum(slow[-200:]) / min(200, n)
    return {"up": up, "down": down,
            "peak": max(up["peak"], down["peak"]), "lag_fs": max(up["lag_fs"], down["lag_fs"]),
            "lost_steps": round((end_err - base) / counts_per_full_step, 2),
            "ramp_samples": [len(rising), len(falling)]}


@scenario("ramp_profile",
          "Ramps through the resonance: for each acceleration (set stopped: the chip takes a new "
          "one only then) and ramp current (TVAL_ACC = TVAL_DEC), cruise at rpm_low, ramp to "
          "rpm_high, hold, ramp back, all inside one 2 kHz capture; measures the ringing during "
          "each ramp and after it, the lag the ramp adds (fraction of a full step: the torque "
          "margin used) and lost steps. The resonance avoider is off meanwhile; accel, decel, "
          "currents, sample rate and the avoider are restored.",
          params={"accels_fsps2": [300, 600, 1200, 2500, 5000, 10000],
                  "ramp_currents_a": [0.86, 1.01, 1.17, 1.33, 1.48, 1.64],
                  "rpm_low": 50, "rpm_high": 120, "hold_s": 0.25, "rate_hz": 2000,
                  "max_current_a": 1.7},
          moves_motor=True)
def ramp_profile(ctx: ScenarioContext, params: Dict[str, Any]) -> None:
    geom = _chip_geometry(ctx)
    full, cpr = geom["full_steps_per_rev"], geom["encoder_cpr"] or 4000
    cpfs = cpr / float(full)
    rate = int(params["rate_hz"])
    cur = ctx.call(C.cfg_query("CURRENT"))
    ctx.require("read the currents", cur.success and "run_a" in (cur.data or {}), cur.error_message or "")
    c0 = dict(cur.data)
    phys = ctx.call(C.driver_physical()).data or {}
    acc0, dec0 = float(phys.get("accel_fsps2", 5000)), float(phys.get("decel_fsps2", 5000))
    res0 = ctx.call(C.res_query())
    res_on = bool((res0.data or {}).get("enabled")) if res0.success else False
    rate0 = int((ctx.call(C.enc_filter_query()).data or {}).get("sample_rate_hz", 1000))
    low_cps = float(params["rpm_low"]) / 60.0 * cpr
    high_cps = float(params["rpm_high"]) / 60.0 * cpr
    rows: List[Dict[str, Any]] = []
    try:
        if res_on:
            ctx.call(C.res_config(False))
        ctx.require("sample rate", ctx.call(C.enc_sample_rate(rate)).success)
        for ia in params["ramp_currents_a"]:
            ia = min(float(ia), float(params["max_current_a"]))
            for acc in params["accels_fsps2"]:
                ctx.call(C.mot_stop())
                ctx.sleep(0.6)
                ctx.call(C.cfg_accel(float(acc)))
                ctx.call(C.cfg_decel(float(acc)))
                ctx.call(C.cfg_current(c0["hold_a"], c0["run_a"], ia, ia))
                ctx.require(f"apply {acc} / {ia}", ctx.call(C.cfg_apply()).success)
                ctx.call(C.fault_clear())
                ctx.require("enable", ctx.call(C.mot_enable()).success)
                ramp_s = (high_cps - low_cps) / cpfs / float(acc)
                ctx.require("cruise", ctx.call(C.mot_run_rpm(float(params["rpm_low"]), 1)).success)
                ctx.sleep(max(1.0, float(params["rpm_low"]) / 60.0 * full / float(acc) + 0.6))
                ctx.call(C.mot_run_rpm(float(params["rpm_high"]), 1))
                ctx.sleep(ramp_s + float(params["hold_s"]))
                ctx.call(C.mot_run_rpm(float(params["rpm_low"]), 1))
                ctx.sleep(ramp_s + 0.2)
                cap_rows, info = _capture_tail(ctx, 4096)
                if len(cap_rows) < 256:
                    continue
                enc = V.unwrap16([r[0] for r in cap_rows]); syn = V.unwrap16([r[1] for r in cap_rows])
                cpp = cpr / float(full * int(info.get("pulses_per_full_step", 8)))
                e = [x - enc[0] for x in enc]
                c = [(x - syn[0]) * cpp for x in syn]
                a = analyse_ramp(e, c, float(info.get("rate_hz", rate)), low_cps, high_cps, cpfs)
                a.update({"accel_fsps2": float(acc), "ramp_a": ia, "glitch": capture_glitch(
                    [x - y for x, y in zip(e, c)])})
                rows.append(a)
    finally:
        ctx.call(C.cap_resume())
        ctx.call(C.mot_stop())
        ctx.sleep(0.6)
        ctx.call(C.cfg_accel(acc0))
        ctx.call(C.cfg_decel(dec0))
        ctx.call(C.cfg_current(c0["hold_a"], c0["run_a"], c0["acc_a"], c0["dec_a"]))
        ctx.call(C.cfg_apply())
        ctx.call(C.enc_sample_rate(rate0))
        if res_on:
            ctx.call(C.res_config(True))
        ctx.call(C.mot_disable())
    ctx.measure("rows", rows)
    ctx.check("grid measured", len(rows) == len(params["accels_fsps2"]) * len(params["ramp_currents_a"]))
    ctx.check("no lost steps", all(abs(r["lost_steps"]) < 0.5 for r in rows if not r["glitch"]))


@scenario("resonance_recal",
          "Quick recalibration of the resonance avoider's model at the run current in force (after "
          "a load, or a current scale): sweeps +-span_pct around the predicted main resonance "
          "(harmonic 2), finds the peak with the 2 kHz capture and, with apply, updates the "
          "model's constant (f_n at 1 A) keeping its exponent (CTRL:RES:CFG + SAVE). The avoider is "
          "off meanwhile and restored. Needs a model (resonance_profile with apply). ~30 s.",
          params={"span_pct": 25.0, "step_pct": 2.5, "settle_s": 0.35, "samples": 1024,
                  "rate_hz": 2000, "apply": False, "peak_factor": 1.4},
          moves_motor=True)
def resonance_recal(ctx: ScenarioContext, params: Dict[str, Any]) -> None:
    geom = _chip_geometry(ctx)
    full, cpr = geom["full_steps_per_rev"], geom["encoder_cpr"] or 4000
    rate, samples = int(params["rate_hz"]), int(params["samples"])
    st = ctx.call(C.res_query())
    ctx.require("read the avoider", st.success, st.error_message or "")
    s0 = dict(st.data)
    f1a_old, expo = float(s0.get("f1a_hz") or 0), float(s0.get("exponent") or 0)
    run_a = float(s0.get("run_cfg_a") or 0)
    ctx.require("a model to recalibrate", f1a_old > 0 and expo > 0 and run_a > 0,
                "run resonance_profile with apply first")
    centre = float(s0.get("rpm_k2") or 0)
    span, step = float(params["span_pct"]) / 100.0, float(params["step_pct"]) / 100.0
    n = int(round(2 * span / step))
    rpms = [round(centre * (1.0 - span + i * step), 1) for i in range(n + 1)]
    rate0 = int((ctx.call(C.enc_filter_query()).data or {}).get("sample_rate_hz", 1000))
    rows: List[Dict[str, Any]] = []
    try:
        ctx.call(C.res_config(False))
        ctx.require("sample rate", ctx.call(C.enc_sample_rate(rate)).success)
        ctx.call(C.fault_clear())
        ctx.require("enable", ctx.call(C.mot_enable()).success)
        first = True
        for rpm in rpms:
            ctx.require(f"run {rpm}", ctx.call(C.mot_run_rpm(rpm, 1)).success)
            ctx.sleep((1.0 if first else float(params["settle_s"])) + samples / float(rate) + 0.05)
            first = False
            r = _measure(ctx, samples, cpr, full, rate, samples / float(rate) + 0.05)
            if r is not None:
                r.update({"run_a": run_a, "rpm": rpm, "harmonic": harmonic_of(r["peak_hz"], rpm, full)})
                rows.append(r)
    finally:
        ctx.call(C.cap_resume())
        ctx.call(C.mot_stop())
        ctx.call(C.enc_sample_rate(rate0))
        ctx.call(C.res_config(bool(s0.get("enabled"))))
        ctx.call(C.mot_disable())
    ctx.measure("rows", rows)
    peaks = [r for r in find_resonances(rows, full, float(params["peak_factor"])) if r["harmonic"] == 2]
    ctx.measure("f1a_old", f1a_old)
    if not peaks:
        ctx.measure("why", "no clear harmonic-2 peak in the sweep (the resonance may be gone or far off)")
        ctx.check("a resonance found", False)
        return
    best = max(peaks, key=lambda r: r["ripple"])
    fn = best["fn_hz"]
    f1a_new = fn / run_a ** expo
    ctx.measure("peak_rpm", best["rpm"])
    ctx.measure("fn_hz", fn)
    ctx.measure("f1a_new", round(f1a_new, 1))
    ctx.measure("shift_pct", round((f1a_new / f1a_old - 1.0) * 100.0, 1))
    ctx.check("a resonance found", True)
    if params.get("apply"):
        r = ctx.call(C.res_config(bool(s0.get("enabled")), round(f1a_new, 1), expo))
        ctx.require("model updated", r.success, r.error_message or "")
        ctx.require("saved", ctx.call(C.res_save()).success)
        ctx.measure("applied", True)

