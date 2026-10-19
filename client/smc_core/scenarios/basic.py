"""Single-board scenarios: bring-up, ESTOP, heartbeat loss, step-mode read-back, system ID."""

import time
from typing import Any, Dict

from .. import commands as C
from .. import driver as D
from .. import views as V
from ..codec import parse_idn
from . import ScenarioContext, scenario


@scenario("bringup", "Identity, protocol version, status, driver config, event and watchdog state "
                     "(read-only; the probe the GUI used to run on connect).")
def bringup(ctx: ScenarioContext, params: Dict[str, Any]) -> None:
    idn = ctx.call(C.idn())
    ctx.require("identity", idn.success, idn.error_message or "")
    ident = parse_idn(idn)
    ctx.measure("serial", ident.serial)
    ctx.measure("fw", ident.fw)
    ctx.measure("protocol", ident.proto)
    ctx.check("protocol version", D.protocol_supported(ident.proto),
              f"board {ident.proto}, client {'/'.join(map(str, D.SUPPORTED_PROTOCOLS))}")
    ver = ctx.call(C.version())
    ctx.check("version lines", ver.success and len(ver.lines) >= 3, f"{len(ver.lines)} lines")
    st = ctx.call(C.status())
    ctx.require("status", st.success, st.error_message or "")
    state = st.data.get("state", "?")
    ctx.measure("state", state)
    ctx.check("not latched in FAULT", state != "FAULT", f"state={state}")
    sysd = st.data.get("system", {}) or {}
    ctx.check("no RX overflow", int(sysd.get("rx_overflows", 0)) == 0,
              f"rx_overflows={sysd.get('rx_overflows', 0)}")
    drv = D.DriverAccess(ctx.call, ident.proto)
    view = drv.settings()
    ctx.check("driver config readable", "microsteps_asked" in view, str(view)[:60])
    if "microsteps_asked" in view:
        ctx.measure("microsteps", view["microsteps_asked"])
    if drv.physical:
        caps = drv.caps()
        ctx.check("driver capabilities (DRV:CAPS?)", "max_microsteps_voltage" in caps, str(caps)[:60])
    hb = ctx.call(C.heartbeat_status())
    ctx.check("heartbeat status", hb.success)
    ev = ctx.call(C.events_status())
    if ev.success:
        ctx.measure("events_lost_critical", ev.data.get("lost_critical"))
        ctx.check("no critical events lost", int(ev.data.get("lost_critical", 0) or 0) == 0)


@scenario("estop_latency", "Run the motor, ESTOP through the fast path, confirm latch + Hi-Z, "
                           "optionally clear. Measures write-to-confirmation time.",
          params={"rpm": 30.0, "run_s": 0.5, "clear": True}, moves_motor=True)
def estop_latency(ctx: ScenarioContext, params: Dict[str, Any]) -> None:
    ctx.call(C.fault_clear())
    ctx.require("enable", ctx.call(C.mot_enable()).success)
    ctx.require("run", ctx.call(C.mot_run_rpm(float(params["rpm"]), 1)).success)
    ctx.sleep(float(params["run_s"]))
    r = ctx.estop()
    ctx.measure("confirm_ms", round((r.t_confirmed - r.t_write) * 1000.0, 2))
    ctx.check("latched and Hi-Z", r.confirmed, r.detail)
    refused = ctx.call(C.mot_run_rpm(float(params["rpm"]), 1))
    ctx.check("motion refused while latched", not refused.success, refused.error_code or "accepted!")
    if params.get("clear", True):
        ctx.check("clear", ctx.call(C.fault_clear()).success)


@scenario("heartbeat_loss", "Starve the firmware watchdog on purpose and confirm it stops the "
                            "motor (ESTOP latch), then clear.",
          params={"timeout_ms": 500, "margin_ms": 400, "clear": True}, moves_motor=True)
def heartbeat_loss(ctx: ScenarioContext, params: Dict[str, Any]) -> None:
    timeout = int(params["timeout_ms"])
    restore = ctx.board.config.heartbeat_timeout_ms
    ctx.call(C.fault_clear())
    ctx.require("arm watchdog", ctx.call(C.heartbeat_timeout(timeout)).success)
    ctx.require("enable", ctx.call(C.mot_enable()).success)
    t0 = time.monotonic()
    ctx.hold((timeout + int(params["margin_ms"])) / 1000.0)
    st = ctx.call(C.status())
    state = st.data.get("state", "?") if st.success else "?"
    ctx.measure("held_ms", round((time.monotonic() - t0) * 1000.0))
    ctx.check("watchdog latched the stop", state == "ESTOP", f"state={state}")
    if params.get("clear", True):
        ctx.check("clear", ctx.call(C.fault_clear()).success)
    ctx.call(C.heartbeat_timeout(restore))


@scenario("step_mode_readback", "Set each step mode (log2 of the microsteps) through DRV:CFG:USTEP "
                                "(protocol 2: DRV:CFG:STEPMODE) + APPLY, read back the resolution in "
                                "force (DRV:USTEP?) and the one asked for, restore the original. A mode "
                                "finer than the driver's finest (DRV:CAPS?) must be refused.",
          params={"modes": [0, 3, 7]})
def step_mode_readback(ctx: ScenarioContext, params: Dict[str, Any]) -> None:
    drv = D.DriverAccess.for_ctx(ctx)
    before = drv.resolution()
    ctx.require("read current", "microsteps" in before, "no resolution readback")
    drive = str(before.get("drive_mode", "voltage"))
    caps = drv.caps()
    asked = drv.configured_microsteps()
    original = int(asked if asked is not None else before["microsteps"])
    ctx.measure("original", original)
    finest = D.finest_microsteps(caps)
    try:
        for mode in params["modes"]:
            n = 1 << int(mode)
            if n > finest:
                # beyond what the driver can run in any mode: refused, never capped silently
                r = drv.set_microsteps(n)
                ctx.check(f"mode {mode} refused (the driver's finest is 1/{finest})", not r.success,
                          r.error_code or "accepted!")
                continue
            drv.set_microsteps(n)
            ctx.call(C.cfg_apply())
            got = drv.resolution().get("microsteps", -1)
            cfg = drv.configured_microsteps()
            # the driver runs the resolution in force (capped per drive mode by its caps;
            # powerSTEP01 current mode: 1/16 at most); the config keeps what was asked
            want = D.microsteps_in_force(caps, n, drive)
            ctx.check(f"mode {mode}", got == want and cfg == n,
                      f"in force {got} (expected {want}), asked {cfg} (expected {n})")
    finally:
        drv.set_microsteps(D.requestable_microsteps(caps, original))
        ctx.call(C.cfg_apply())


_SYSID = {
    "step": lambda p, d: C.sysid_step(p["speed"], d, p["duration_ms"], p["settle_ms"]),
    "ramp": lambda p, d: C.sysid_ramp(p["start_speed"], p["speed"], d, p["duration_ms"],
                                      p["settle_ms"]),
    "sine": lambda p, d: C.sysid_sine(p["start_speed"], p["speed"], p["frequency_mhz"], d,
                                      p["duration_ms"], p["settle_ms"]),
    "trapezoid": lambda p, d: C.sysid_trapezoid(p["speed"], d, p["duration_ms"], p["settle_ms"]),
    "rectangle": lambda p, d: C.sysid_rect(p["speed"], d, p["duration_ms"], p["settle_ms"]),
}


@scenario("sysid", "Run one system-identification test on the firmware, wait for DONE, "
                   "download every sample. The fit stays with the frontend (scipy).",
          params={"type": "step", "speed": 800, "start_speed": 0, "frequency_mhz": 500,
                  "forward": True, "duration_ms": 5000, "settle_ms": 1000, "poll_s": 0.2,
                  "chunk": 40, "timeout_s": 60}, moves_motor=True)
def sysid(ctx: ScenarioContext, params: Dict[str, Any]) -> None:
    kind = params["type"]
    ctx.require("test type", kind in _SYSID, kind)
    start = ctx.call(_SYSID[kind](params, 1 if params.get("forward", True) else 0))
    ctx.require("start", start.success, start.error_message or "")
    deadline = time.monotonic() + float(params["timeout_s"])
    status = {"phase": -1, "samples": 0}
    while time.monotonic() < deadline:
        ctx.sleep(float(params["poll_s"]))
        status = V.sysid_status(ctx.call(C.sysid_status()))
        if status["phase"] == V.SYSID_DONE:
            break
    ctx.require("done", status["phase"] == V.SYSID_DONE, f"phase={status['phase']}")
    samples = []
    total = None
    while total is None or len(samples) < total:
        chunk = V.sysid_data(ctx.call(C.sysid_data(len(samples), int(params["chunk"]))))
        total = chunk.total if chunk.total >= 0 else len(samples)
        if not chunk.samples:
            break
        samples.extend([list(s) for s in chunk.samples])   # JSON-native: identical after a round trip
    ctx.check("all samples", total is not None and len(samples) == total,
              f"{len(samples)}/{total}")
    ctx.measure("total", total)
    ctx.measure("samples", samples)
