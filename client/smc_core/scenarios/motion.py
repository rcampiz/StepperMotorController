"""
Motion behaviour scenarios: does the firmware move what it was asked, in the step mode it
says it is in, with an encoder that follows?

Written after the 2026-10-17 bench session, where a "100 steps" jog at a GUI-believed 1/128
turned two revolutions: the chip was still at 1/32 because `DRV:CFG:APPLY` writes STEP_MODE
while the bridges are on, which the powerSTEP01 ignores. These scenarios measure instead of
trusting readbacks, so that class of bug fails a test rather than a bench session.

All three energise the motor (moves_motor=True) and leave the output disabled at the end.
"""

import time
from typing import Any, Dict, Optional

from .. import commands as C
from .. import views as V
from . import ScenarioContext, scenario


# --- helpers -------------------------------------------------------------------------------

def _chip_geometry(ctx: ScenarioContext) -> Dict[str, int]:
    """Step mode as the chip reports it, full steps and encoder counts per revolution."""
    q = ctx.call(C.step_mode_query())
    ctx.require("read DRV:STEP_MODE?", q.success, q.error_message or "")
    fs = ctx.call(C.full_steps_query())
    full = int(fs.data.get("full_steps_per_rev", 200)) if fs.success else 200
    mode = int(q.data.get("step_mode", 0))
    upr = int(q.data.get("microsteps_per_rev") or full * (1 << mode))
    ppr = ctx.call(C.encoder_ppr_query())
    cpr = int(ppr.data.get("encoder_ppr", 0)) if ppr.success else 0
    return {"step_mode": mode, "full_steps_per_rev": full, "microsteps_per_rev": upr,
            "encoder_cpr": cpr}


def _status(ctx: ScenarioContext) -> Dict[str, Any]:
    r = ctx.call(C.status())
    ctx.require("DIAG:STAT?", r.success, r.error_message or "")
    return r.data


def _moving(st: Dict[str, Any]) -> bool:
    m = st.get("motor", {})
    return bool(m.get("busy")) or int(m.get("speed", 0) or 0) != 0


def _wait_move(ctx: ScenarioContext, timeout_s: float,
               sample: Optional[callable] = None, sample_period_s: float = 0.1) -> bool:
    """Wait for a move to start (≤ 1 s) and finish. Returns False on timeout.

    `sample(status)` is called about every `sample_period_s` while the motor moves.
    """
    t0 = time.monotonic()
    while time.monotonic() - t0 < 1.0:
        if _moving(_status(ctx)):
            break
        ctx.sleep(0.02)
    next_sample = time.monotonic()
    while time.monotonic() - t0 < timeout_s:
        st = _status(ctx)
        if not _moving(st):
            return True
        if sample is not None and time.monotonic() >= next_sample:
            sample(st)
            next_sample = time.monotonic() + sample_period_s
        ctx.sleep(0.02)
    return False


def _move_and_measure(ctx: ScenarioContext, usteps: int, direction: int, speed: int,
                      timeout_s: float, sample: Optional[callable] = None) -> Dict[str, int]:
    before = _status(ctx)
    r = ctx.call(C.mot_move(usteps, direction, speed))
    ctx.require(f"MOT:MOVE {usteps} {direction} accepted", r.success, r.error_message or "")
    ctx.require(f"move {usteps} {direction} finished", _wait_move(ctx, timeout_s, sample),
                f"still moving after {timeout_s:.0f} s")
    ctx.sleep(0.4)                                   # let the encoder snapshot settle
    after = _status(ctx)
    return {"position": int(after["motor"]["position"]) - int(before["motor"]["position"]),
            "encoder": int(after["encoder"]["count"]) - int(before["encoder"]["count"])}


def _move_timeout(usteps: int, speed: int, geom: Dict[str, int]) -> float:
    full_steps = usteps / (1 << geom["step_mode"])
    return 5.0 + 3.0 * full_steps / max(speed, 1)


# --- scenarios -----------------------------------------------------------------------------

@scenario("motion_scale", "Move N microsteps each way at the chip's current step mode and "
                          "check ABS_POS moved exactly N and the encoder moved N/usteps_per_rev "
                          "of a revolution; then the REV form. Measures encoder counts per rev.",
          params={"usteps": 6400, "speed": 100, "rev": 0.25, "encoder_tol": 0.02},
          moves_motor=True)
def motion_scale(ctx: ScenarioContext, params: Dict[str, Any]) -> None:
    usteps, speed = int(params["usteps"]), int(params["speed"])
    geom = _chip_geometry(ctx)
    for k, v in geom.items():
        ctx.measure(k, v)
    upr, cpr = geom["microsteps_per_rev"], geom["encoder_cpr"]
    tol = max(2.0, float(params["encoder_tol"]) * cpr)      # counts
    ctx.call(C.fault_clear())
    ctx.require("enable", ctx.call(C.mot_enable()).success)
    try:
        timeout = _move_timeout(usteps, speed, geom)
        for direction in (1, 0):
            d = _move_and_measure(ctx, usteps, direction, speed, timeout)
            sign = 1 if direction else -1
            ctx.check(f"ABS_POS moved {sign * usteps:+d}", d["position"] == sign * usteps,
                      f"moved {d['position']:+d}")
            if cpr > 0:
                expected = sign * usteps * cpr / upr
                ctx.check(f"encoder followed ({expected:+.0f} counts)",
                          abs(d["encoder"] - expected) <= tol,
                          f"got {d['encoder']:+d}, tolerance {tol:.0f}")
                if d["position"]:
                    ctx.measure(f"encoder_counts_per_rev_dir{direction}",
                                round(d["encoder"] * upr / d["position"], 1))
        rev = float(params["rev"])
        if rev > 0:
            before = _status(ctx)
            r = ctx.call(C.mot_move_rev(rev, 1, speed))
            ctx.require("MOT:MOVE x REV accepted", r.success, r.error_message or "")
            ctx.require("REV move finished", _wait_move(ctx, _move_timeout(int(rev * upr), speed, geom)))
            ctx.sleep(0.4)
            moved = int(_status(ctx)["motor"]["position"]) - int(before["motor"]["position"])
            ctx.check(f"REV form uses the chip's {upr} usteps/rev", abs(moved - round(rev * upr)) <= 1,
                      f"{rev} rev moved {moved} usteps")
    finally:
        ctx.call(C.mot_disable())


@scenario("step_mode_chip", "Change the step mode through DRV:CFG:STEPMODE + APPLY with the "
                            "output disabled and then enabled; the chip (DRV:STEP_MODE?) must "
                            "follow in both cases. Restores the original mode in Hi-Z.",
          params={"mode": 7}, moves_motor=True)
def step_mode_chip(ctx: ScenarioContext, params: Dict[str, Any]) -> None:
    mode = int(params["mode"])
    original = _chip_geometry(ctx)["step_mode"]
    ctx.measure("original", original)
    other = mode if mode != original else (mode + 1) % 8
    ctx.measure("mode", other)

    def apply(m: int) -> int:
        ctx.call(C.cfg_step_mode(m))
        a = ctx.call(C.cfg_apply())
        ctx.check(f"APPLY {m} accepted", a.success, a.error_message or "")
        q = ctx.call(C.step_mode_query())
        return int(q.data.get("step_mode", -1)) if q.success else -1

    ctx.call(C.fault_clear())
    try:
        ctx.require("disable (Hi-Z)", ctx.call(C.mot_disable()).success)
        got = apply(other)
        ctx.check(f"chip at {other} after APPLY in Hi-Z", got == other, f"DRV:STEP_MODE?={got}")
        got = apply(original)
        ctx.check(f"chip back at {original} in Hi-Z", got == original, f"DRV:STEP_MODE?={got}")

        ctx.require("enable", ctx.call(C.mot_enable()).success)
        ctx.sleep(0.2)
        got = apply(other)
        cfg = V.motor_config(ctx.call(C.driver_config())).get("step_mode")
        ctx.check(f"config says {other} with the output enabled", cfg == other, f"DRV:CFG?={cfg}")
        ctx.check(f"chip at {other} after APPLY with the output enabled", got == other,
                  f"DRV:STEP_MODE?={got} (config {cfg})")
    finally:
        ctx.call(C.mot_disable())
        ctx.call(C.cfg_step_mode(original))
        ctx.call(C.cfg_apply())
        q = ctx.call(C.step_mode_query())
        ctx.check("original restored", q.success and int(q.data.get("step_mode", -1)) == original,
                  f"DRV:STEP_MODE?={q.data.get('step_mode') if q.success else q.error_message}")


@scenario("telemetry_load", "With telemetry pushed every period_ms, run a move and require the "
                            "encoder snapshot to keep advancing between samples (a starved "
                            "Encoder task freezes it). Restores the board's telemetry period.",
          params={"period_ms": 50, "usteps": 6400, "speed": 100}, moves_motor=True)
def telemetry_load(ctx: ScenarioContext, params: Dict[str, Any]) -> None:
    period = int(params["period_ms"])
    usteps, speed = int(params["usteps"]), int(params["speed"])
    geom = _chip_geometry(ctx)
    restore = ctx.board.config.telemetry_period_ms
    counts = []
    ctx.call(C.fault_clear())
    ctx.require("enable", ctx.call(C.mot_enable()).success)
    try:
        ctx.require(f"SYST:TLM:RATE {period}", ctx.call(C.telemetry_rate(period)).success)
        ctx.sleep(0.3)
        d = _move_and_measure(ctx, usteps, 1, speed, _move_timeout(usteps, speed, geom),
                              sample=lambda st: counts.append(int(st["encoder"]["count"])))
        ctx.measure("encoder_samples", counts)
        ctx.measure("encoder_delta", d["encoder"])
        stale = sum(1 for a, b in zip(counts, counts[1:]) if a == b)
        ctx.measure("stale_samples", stale)
        ctx.check("ABS_POS moved", d["position"] == usteps, f"moved {d['position']}")
        ctx.check("encoder snapshot advanced in every sample during the move",
                  len(counts) >= 2 and stale == 0,
                  f"{stale} of {max(len(counts) - 1, 0)} consecutive samples unchanged: {counts}")
    finally:
        ctx.call(C.telemetry_rate(restore))
        ctx.call(C.mot_disable())
