"""
The motor driver's settings in physical units, on either protocol (driver boundary step 4).

Protocol 3 boards configure the driver in physical units (DRV:CFG:CURRENT, DRV:CFG:USTEP, ...)
and say what the driver can do (DRV:CAPS?). Protocol 2 boards only take the powerSTEP01's chip
codes (DRV:CFG:TVAL, DRV:CFG:STEPMODE, ...). Frontends, scenarios and motor profiles speak
physical units only and let this module pick the commands:

    drv = DriverAccess(board.request, board.identity.proto)   # or DriverAccess.for_ctx(ctx)
    caps = drv.caps()                 # DRV:CAPS?, or the powerSTEP01's for a protocol 2 board
    s = drv.settings()                # DRV:CFG:PHYS?, or DRV:CFG? translated
    for req in drv.requests({"current_a": (0.31, 1.33, 1.56, 1.56), "microsteps": 16}):
        board.request(req)
    board.request(C.cfg_apply())

Settings keys (the DRV:CFG:PHYS? names): drive_mode ('current'|'voltage'), microsteps_asked,
microsteps (in force), hold_a/run_a/acc_a/dec_a, hold_duty/run_duty/acc_duty/dec_duty,
accel_fsps2, decel_fsps2, max_speed_fsps, overcurrent_a, overcurrent_v, stall_threshold,
alarms [fault names], fault_action, idle_release_ms, source. A change set for requests() uses
drive_mode, current_a (4 amps), duty (4 x 0..1), microsteps, accel_fsps2, decel_fsps2,
max_speed_fsps, overcurrent_a, stall_threshold, alarms, fault_action, idle_release_ms.

The protocol 2 translation below is the only chip knowledge left in the client, and it goes
with the protocol 2 aliases.
"""

from typing import Any, Callable, Dict, List, Optional, Sequence

from . import commands as C
from . import units as U
from . import views as V
from .codec import Response

PHYSICAL_PROTOCOL = 3
SUPPORTED_PROTOCOLS = (2, 3)            # what this client speaks; newest first in features
FAULT_NAMES = C.FAULT_NAMES             # bit i of the telemetry `motor.faults`

# A protocol 2 board is a powerSTEP01 on an X-NUCLEO-IHM03A1: its capabilities, as a
# protocol 3 board would report them (DRV:CAPS?), so the frontends have one code path.
POWERSTEP01_CAPS: Dict[str, Any] = {
    "voltage_mode": True, "current_mode": True,
    "max_microsteps_voltage": 128, "max_microsteps_current": 16,
    "stall_detect": True, "standby_pin": True, "hardware_positioning": True,
    "sync_pulses_per_full_step": 8,
    "fault_bits": list(FAULT_NAMES), "faults_reported": list(FAULT_NAMES),
    "faults_alarmable": list(FAULT_NAMES),
    "max_current_a": 9.906, "current_step_a": 0.078, "duty_step": 0.00390625,
    "overcurrent_sense": "voltage", "overcurrent_ohms": 0.0012,
    "overcurrent_min_a": 26.0417, "overcurrent_max_a": 833.333, "overcurrent_step_a": 26.0417,
    "stall_step": 1.0 / 31.0,
    "max_speed_fsps": 15625, "max_speed_limit_fsps": 15609.7, "speed_limit_step_fsps": 15.2588,
    "accel_min_fsps2": 14.552, "accel_max_fsps2": 59590.4, "accel_step_fsps2": 14.552,
    "min_position_usteps": -2097152, "max_position_usteps": 2097151,
    "protocol_2": True,                 # not from a board: the client's protocol 2 assumption
}

_OCD_V_PER_CODE = 0.03125               # protocol 2: OCD_TH is (code + 1) x 31.25 mV of VDS
_STALL_CODE_MAX = 31
_FAULT_FLAGS = ("OCD", "UVLO", "TH_SD", "TH_WARN")   # legacy view keys of the first four bits


def supports_physical(proto: Optional[int]) -> bool:
    """True when the board speaks protocol 3 (or newer) and so takes the physical commands."""
    return proto is not None and int(proto) >= PHYSICAL_PROTOCOL


def protocol_supported(proto: Optional[int]) -> bool:
    return proto is not None and int(proto) in SUPPORTED_PROTOCOLS


def fault_names(mask: int) -> List[str]:
    """Telemetry `motor.faults` bits -> chip-neutral names."""
    return [n for i, n in enumerate(FAULT_NAMES) if int(mask) & (1 << i)]


def status_faults(status: Dict[str, Any]) -> List[str]:
    """The driver's faults from a DIAG:STAT? object: `motor.faults` (protocol 3), else the
    protocol 2 flags (stall_a/stall_b fold into `stall`)."""
    m = (status or {}).get("motor", {}) or {}
    if "faults" in m:
        return fault_names(int(m["faults"]))
    flags = {"overcurrent": m.get("ocd"), "undervoltage": m.get("uvlo"),
             "thermal_shutdown": m.get("thermal_sd"), "thermal_warning": m.get("thermal_warn"),
             "stall": m.get("stall_a") or m.get("stall_b"), "command_error": m.get("cmd_err")}
    return [n for n in FAULT_NAMES if flags.get(n)]


def quantize(value: float, step: float, lo: Optional[float] = None, hi: Optional[float] = None) -> float:
    """Round onto a driver's grid (step from DRV:CAPS?), clamped to lo..hi."""
    v = float(value)
    if step and step > 0:
        v = round(v / step) * step
    if lo is not None:
        v = max(lo, v)
    if hi is not None:
        v = min(hi, v)
    return v


def max_microsteps(caps: Dict[str, Any], drive_mode: str) -> int:
    key = "max_microsteps_current" if drive_mode == "current" else "max_microsteps_voltage"
    return int(caps.get(key) or 1)


def microsteps_in_force(caps: Dict[str, Any], asked: int, drive_mode: str) -> int:
    """The resolution the driver runs for a request (capped for the drive mode, a power of two)."""
    n = max(1, min(int(asked), max_microsteps(caps, drive_mode)))
    return 1 << (n.bit_length() - 1)


# ---------------------------------------------------------------------------
# Protocol 2 translation (powerSTEP01 codes <-> physical)
# ---------------------------------------------------------------------------
def settings_from_legacy(cfg: Dict[str, Any]) -> Dict[str, Any]:
    """A DRV:CFG? view (smc_core.views.motor_config) as DRV:CFG:PHYS? would report it."""
    out: Dict[str, Any] = {}
    if "source" in cfg:
        out["source"] = cfg["source"]
    mode = str(cfg.get("drive_mode", "current"))
    out["drive_mode"] = mode
    if cfg.get("step_mode") is not None:
        asked = 1 << int(cfg["step_mode"])
        out["microsteps_asked"] = asked
        out["microsteps"] = microsteps_in_force(POWERSTEP01_CAPS, asked, mode)
    if cfg.get("tval"):
        for k, code in zip(("hold_a", "run_a", "acc_a", "dec_a"), cfg["tval"]):
            out[k] = round(int(code) * U.TVAL_AMPS_PER_UNIT, 4)
    if cfg.get("kval"):
        for k, code in zip(("hold_duty", "run_duty", "acc_duty", "dec_duty"), cfg["kval"]):
            out[k] = int(code) / 256.0
    if cfg.get("acc_raw") is not None:
        out["accel_fsps2"] = round(int(cfg["acc_raw"]) * 14.552, 3)
        out["decel_fsps2"] = round(int(cfg["dec_raw"]) * 14.552, 3)
        out["max_speed_fsps"] = int(cfg["maxspd_raw"]) * 15625.0 / 1024.0      # exact in binary
    if cfg.get("ocd_th") is not None:
        volts = (int(cfg["ocd_th"]) + 1) * _OCD_V_PER_CODE
        out["overcurrent_v"] = volts
        out["overcurrent_a"] = round(volts / POWERSTEP01_CAPS["overcurrent_ohms"], 3)
    if cfg.get("stall_th") is not None:
        out["stall_threshold"] = (int(cfg["stall_th"]) & _STALL_CODE_MAX) / float(_STALL_CODE_MAX)
    faults = cfg.get("faults")
    if faults:
        names = [n for n, k in zip(FAULT_NAMES[:4], _FAULT_FLAGS) if faults.get(k)]
        if faults.get("STALL_A") or faults.get("STALL_B"):
            names.append("stall")
        if faults.get("CMD_ERR"):
            names.append("command_error")
        out["alarms"] = names
    if "action" in cfg:
        out["fault_action"] = cfg["action"]
    if "idle_release_ms" in cfg:
        out["idle_release_ms"] = int(cfg["idle_release_ms"])
    return out


def _legacy_requests(changes: Dict[str, Any], base: Dict[str, Any]) -> List[C.Request]:
    reqs: List[C.Request] = []
    if "drive_mode" in changes:
        reqs.append(C.cfg_drive_mode(changes["drive_mode"] == "current"))
    if changes.get("current_a") is not None:
        reqs.append(C.cfg_tval(*(U.tval_from_amps(a) for a in changes["current_a"])))
    if changes.get("duty") is not None:
        reqs.append(C.cfg_kval(*(max(0, min(255, round(float(d) * 256))) for d in changes["duty"])))
    if changes.get("microsteps") is not None:
        reqs.append(C.cfg_step_mode(min(7, int(changes["microsteps"]).bit_length() - 1)))
    if any(changes.get(k) is not None for k in ("accel_fsps2", "decel_fsps2", "max_speed_fsps")):
        def pick(k, default):
            v = changes.get(k)
            return float(v if v is not None else base.get(k, default))
        # the firmware's conversions: floor, with the slack that keeps a read-back value on its code
        acc = max(1, min(4095, int((pick("accel_fsps2", 2000) * 1000 + 7) // 14552)))
        dec = max(1, min(4095, int((pick("decel_fsps2", 2000) * 1000 + 7) // 14552)))
        spd = max(1, min(1023, int(pick("max_speed_fsps", 1000) * 1024 / 15625 + 1e-6)))
        reqs.append(C.cfg_motion(acc, dec, spd))
    if changes.get("overcurrent_a") is not None:
        volts = float(changes["overcurrent_a"]) * POWERSTEP01_CAPS["overcurrent_ohms"]
        reqs.append(C.cfg_ocd(max(0, min(31, round(volts / _OCD_V_PER_CODE) - 1))))
    if changes.get("stall_threshold") is not None:
        reqs.append(C.cfg_stall(max(0, min(31, round(float(changes["stall_threshold"]) * _STALL_CODE_MAX)))))
    if changes.get("alarms") is not None:
        a = set(changes["alarms"])
        reqs.append(C.cfg_fault_enables("overcurrent" in a, "thermal_shutdown" in a, "thermal_warning" in a,
                                        "undervoltage" in a, "stall" in a, "stall" in a, "command_error" in a))
    return reqs


def _physical_requests(changes: Dict[str, Any]) -> List[C.Request]:
    reqs: List[C.Request] = []
    if "drive_mode" in changes:
        reqs.append(C.cfg_drive_mode(changes["drive_mode"] == "current"))
    if changes.get("current_a") is not None:
        reqs.append(C.cfg_current(*changes["current_a"]))
    if changes.get("duty") is not None:
        reqs.append(C.cfg_duty(*changes["duty"]))
    if changes.get("microsteps") is not None:
        reqs.append(C.cfg_microsteps(int(changes["microsteps"])))
    if changes.get("accel_fsps2") is not None:
        reqs.append(C.cfg_accel(changes["accel_fsps2"]))
    if changes.get("decel_fsps2") is not None:
        reqs.append(C.cfg_decel(changes["decel_fsps2"]))
    if changes.get("max_speed_fsps") is not None:
        reqs.append(C.cfg_max_speed(changes["max_speed_fsps"]))
    if changes.get("overcurrent_a") is not None:
        reqs.append(C.cfg_overcurrent(changes["overcurrent_a"]))
    if changes.get("stall_threshold") is not None:
        reqs.append(C.cfg_stall_threshold(changes["stall_threshold"]))
    if changes.get("alarms") is not None:
        reqs.append(C.cfg_alarms(changes["alarms"]))
    return reqs


def settings_requests(changes: Dict[str, Any], physical: bool = True,
                      base: Optional[Dict[str, Any]] = None) -> List[C.Request]:
    """The DRV:CFG commands for a change set (no APPLY): the physical ones for a protocol 3
    board, the chip-code aliases for a protocol 2 one (`base`, the board's settings, fills
    the values a protocol 2 command needs together, e.g. DRV:CFG:MOTION's three)."""
    reqs = _physical_requests(changes) if physical else _legacy_requests(changes, base or {})
    if changes.get("fault_action") is not None:
        a = changes["fault_action"]
        reqs.append(C.cfg_fault_action(C.FAULT_ACTIONS[a] if isinstance(a, str) else int(a)))
    if changes.get("idle_release_ms") is not None:
        reqs.append(C.cfg_idle_release(int(changes["idle_release_ms"])))
    return reqs


# ---------------------------------------------------------------------------
# One board
# ---------------------------------------------------------------------------
class DriverAccess:
    """The driver settings of one board over whichever protocol it speaks. `call` sends a
    Request and returns the Response (Board.request, ScenarioContext.call, a job's ctx.call)."""

    def __init__(self, call: Callable[[C.Request], Response], proto: Optional[int]):
        self.call = call
        self.proto = proto
        self.physical = supports_physical(proto)
        self._caps: Optional[Dict[str, Any]] = None

    @classmethod
    def for_ctx(cls, ctx) -> "DriverAccess":
        ident = getattr(getattr(ctx, "board", None), "identity", None)
        return cls(ctx.call, getattr(ident, "proto", None))

    def caps(self) -> Dict[str, Any]:
        """DRV:CAPS? (cached), or the powerSTEP01's for a protocol 2 board (or a refusal)."""
        if self._caps is None:
            got = V.driver_caps(self.call(C.driver_caps())) if self.physical else {}
            self._caps = got or dict(POWERSTEP01_CAPS)
            if not got:
                self.physical = False
        return self._caps

    def settings(self) -> Dict[str, Any]:
        """Every driver setting in physical units (DRV:CFG:PHYS?, or DRV:CFG? translated)."""
        if self.physical:
            got = V.driver_physical(self.call(C.driver_physical()))
            if got:
                return got
        return settings_from_legacy(V.motor_config(self.call(C.driver_config())))

    def requests(self, changes: Dict[str, Any], base: Optional[Dict[str, Any]] = None) -> List[C.Request]:
        if not self.physical and base is None and any(
                changes.get(k) is not None for k in ("accel_fsps2", "decel_fsps2", "max_speed_fsps")):
            base = self.settings()
        return settings_requests(changes, self.physical, base)

    def resolution(self) -> Dict[str, Any]:
        """The resolution in force, read back from the driver (microsteps, step_mode, drive_mode)."""
        return V.resolution(self.call(C.microsteps_query() if self.physical else C.step_mode_query()))

    def configured_microsteps(self) -> Optional[int]:
        """The microsteps asked for in the stored settings."""
        if self.physical:
            got = V.driver_setting(self.call(C.cfg_query("USTEP")))
            if "microsteps_asked" in got:
                return int(got["microsteps_asked"])
        m = V.motor_config(self.call(C.driver_config())).get("step_mode")
        return (1 << int(m)) if m is not None else None

    def set_microsteps(self, n: int) -> Response:
        """Store the resolution asked for (no APPLY)."""
        if self.physical:
            return self.call(C.cfg_microsteps(n))
        return self.call(C.cfg_step_mode(min(7, int(n).bit_length() - 1)))
