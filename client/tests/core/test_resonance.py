"""The resonance profile's analysis: harmonic naming, peak finding and the f_n(I) fit."""
import math

from smc_core.scenarios import resonance as R


def _profile(model_f_ref=132.0, i_ref=1.0, exponent=0.5, currents=(0.6, 0.8, 1.0),
             rpms=range(30, 151, 5), width_rpm=4.0):
    """Synthetic rows: a 2 count floor plus a peak where 2 x f_e meets f_n(I)."""
    rows = []
    for i in currents:
        fn = model_f_ref * (i / i_ref) ** exponent
        res_rpm = fn / 2.0 * 4.0 / 200 * 60.0
        for rpm in rpms:
            ripple = 2.0 + 5.0 * math.exp(-((rpm - res_rpm) / width_rpm) ** 2)
            peak = 2.0 * R.electrical_hz(rpm) if abs(rpm - res_rpm) < 8 else 441.0
            rows.append({"run_a": i, "rpm": rpm, "ripple": round(ripple, 2), "peak_hz": peak, "ok": True})
    return rows


def test_harmonics_are_named_from_the_electrical_frequency():
    assert R.electrical_hz(60) == 50.0                      # 200 steps: 50 cycles per turn
    assert R.harmonic_of(99.0, 60) == 2                     # bench: 99 Hz at 60 RPM
    assert R.harmonic_of(132.0, 40) == 4
    assert R.harmonic_of(108.0, 130) == 1
    assert R.harmonic_of(441.0, 130) == 4                   # the full-step rate (433 Hz)
    assert R.harmonic_of(300.0, 130) is None                # between 2 (217 Hz) and 4


def test_peaks_are_found_and_the_fit_recovers_the_model():
    rows = _profile()
    res = R.find_resonances(rows)
    assert sorted(r["run_a"] for r in res) == [0.6, 0.8, 1.0]
    assert all(r["harmonic"] == 2 for r in res)
    m = R.fit_model(res, 1.0)
    assert abs(m["f_ref_hz"] - 132.0) < 3.0
    assert abs(m["exponent"] - 0.5) < 0.05
    assert abs(R.resonant_rpm(m, 1.0, 2) - 79.2) < 2.0      # 132 Hz / 2 / 50 x 60


def test_a_jump_no_shaft_can_make_marks_the_capture_corrupt():
    smooth = [3.0 * math.sin(i / 3.0) for i in range(1024)]
    assert not R.capture_glitch(smooth)
    assert R.capture_glitch(smooth[:500] + [x + 5672.0 for x in smooth[500:]])


def test_a_flat_profile_has_no_resonance_and_no_model():
    rows = [{"run_a": i, "rpm": rpm, "ripple": 2.0, "peak_hz": 441.0, "ok": True}
            for i in (0.6, 1.0) for rpm in range(30, 151, 5)]
    assert R.find_resonances(rows) == []
    assert R.fit_model([], 1.0) is None


def test_a_ramp_is_cut_into_windows_and_its_ringing_and_lag_measured():
    rate, cpfs = 2000.0, 20.0
    low, high, acc = 3333.0, 8000.0, 30000.0           # counts/s, counts/s^2
    cmd, v, x, phase = [], low, 0.0, 0
    for i in range(4096):                               # cruise, up, hold, down, cruise
        t = i / rate
        if 0.3 <= t and phase == 0: phase = 1
        if phase == 1:
            v = min(high, v + acc / rate)
            if v >= high: phase, t_hold = 2, t
        elif phase == 2 and t - t_hold > 0.25: phase = 3
        elif phase == 3:
            v = max(low, v - acc / rate)
        x += v / rate
        cmd.append(x)
    ring = [6.0 * math.sin(2 * math.pi * 130 * i / rate) if 800 < i < 1300 else 0.0 for i in range(4096)]
    lag = [-4.0 if 600 < i < 920 else 0.0 for i in range(4096)]   # the ramp's extra lag
    enc = [c + r + l for c, r, l in zip(cmd, ring, lag)]
    a = R.analyse_ramp(enc, cmd, rate, low, high, cpfs)
    assert a["ramp_samples"][0] > 50 and a["ramp_samples"][1] > 50
    assert 4.0 < a["peak"] < 8.0                        # the 6-count ringing
    assert 0.1 < a["lag_fs"] < 0.3                      # 4 counts = 0.2 full step
    assert abs(a["lost_steps"]) < 0.1

