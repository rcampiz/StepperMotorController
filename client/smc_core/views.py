"""
Typed views of specific replies (client refactor step 4): frontends render dicts and
never parse a reply line themselves (CLIENT_REVIEW section 9, rule 1).

Each function takes a Response (JSON or ASCII) and returns plain data. The text
formats parsed here are the firmware's DBG:MOTOR and DRV:CFG? lines
(debug_service.cpp cmdMotorDebug, config_handlers.cpp cmdMotorConfigShow).
"""

import re
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple

from .codec import Response, multiline_text

FAULT_KEYS = ("OCD", "TH_SD", "TH_WARN", "UVLO", "STALL_A", "STALL_B", "CMD_ERR")
FAULT_ACTIONS = ("HardStop", "HardHiZ", "SoftStop")

_DRIVE = re.compile(r"Drive:\s*(VOLTAGE|CURRENT)")
_TVAL = re.compile(r"TVAL:\s*HOLD=([0-9A-Fa-f]+)\s+RUN=([0-9A-Fa-f]+)\s+ACC=([0-9A-Fa-f]+)\s+DEC=([0-9A-Fa-f]+)")
_CM = re.compile(r"CM:\s*PRED=(\d)\s+TSW=(\d+)us\s+T_FAST=([0-9A-Fa-f]+)\s+TON_MIN=([0-9A-Fa-f]+)\s+TOFF_MIN=([0-9A-Fa-f]+)")
_CFGREG = re.compile(r"CONFIG=([0-9A-Fa-f]{4})\s+T_FAST=([0-9A-Fa-f]+)")
_IDLE = re.compile(r"IdleRelease:\s*(\d+)\s*ms")
_KVAL = re.compile(r"KVAL:\s*HOLD=([0-9A-Fa-f]+)\s*RUN=([0-9A-Fa-f]+)\s*ACC=([0-9A-Fa-f]+)"
                   r"\s*DEC=([0-9A-Fa-f]+)")
_MOTION = re.compile(r"\bACC=(\d+)\s+DEC=(\d+)\s+MAXSPD=(\d+)")
_SPEEDS = re.compile(r"MINSPD=(\d+)\s+FS_SPD=(\d+)")
_STATUS = re.compile(r"STATUS=([0-9A-Fa-f]{4})")
_POS = re.compile(r"POS=(-?\d+)")
_ADC = re.compile(r"ADC_OUT=(\d+)")
_OCD = re.compile(r"OCD_TH=([0-9A-Fa-f]+)")
_STALL = re.compile(r"STALL_TH=([0-9A-Fa-f]+)")
_FAULTS = re.compile(r"Faults:\s*OCD=(\d+)\s+TH_SD=(\d+)\s+TH_W=(\d+)\s+UVLO=(\d+)\s+"
                     r"STALL_A=(\d+)\s+STALL_B=(\d+)\s+CMD=(\d+)")
_ACTION = re.compile(r"Action:\s*(\w+)")
_STEPMODE = re.compile(r"StepMode:\s*(\d+)\s*\(1/(\d+)\)")


def _text(resp_or_text) -> str:
    return multiline_text(resp_or_text) if isinstance(resp_or_text, Response) else str(resp_or_text)


def motor_debug(resp_or_text) -> Dict[str, Any]:
    """DBG:MOTOR -> {status, kval: (hold, run, acc, dec), acc_raw, dec_raw, maxspd_raw,
    position, adc_out, text}. Missing fields are absent."""
    text = _text(resp_or_text)
    out: Dict[str, Any] = {"text": text}
    m = _STATUS.search(text)
    if m:
        out["status"] = int(m.group(1), 16)
    m = _KVAL.search(text)
    if m:
        out["kval"] = tuple(int(g, 16) for g in m.groups())
    m = _MOTION.search(text)
    if m:
        out["acc_raw"], out["dec_raw"], out["maxspd_raw"] = (int(g) for g in m.groups())
    m = _POS.search(text)
    if m:
        out["position"] = int(m.group(1))
    m = _ADC.search(text)
    if m:
        out["adc_out"] = int(m.group(1))
    m = _CFGREG.search(text)
    if m:
        out["config_reg"] = int(m.group(1), 16)
        out["t_fast"] = int(m.group(2), 16)
    return out


def motor_config(resp_or_text) -> Dict[str, Any]:
    """DRV:CFG? -> {source, kval, ocd_th, stall_th, acc_raw, dec_raw, maxspd_raw,
    minspd_raw, fs_spd_raw, faults{OCD..CMD_ERR: bool}, action, action_index,
    step_mode, microsteps, text}."""
    text = _text(resp_or_text)
    out: Dict[str, Any] = {"text": text}
    first = text.split("\n", 1)[0]
    if "flash" in first:
        out["source"] = "flash"
    elif "defaults" in first:
        out["source"] = "defaults"
    m = _KVAL.search(text)
    if m:
        out["kval"] = tuple(int(g, 16) for g in m.groups())
    m = _DRIVE.search(text)
    if m:
        out["drive_mode"] = m.group(1).lower()
    m = _TVAL.search(text)
    if m:
        out["tval"] = tuple(int(g, 16) for g in m.groups())
    m = _CM.search(text)
    if m:
        out["cm"] = {"pred": m.group(1) == "1", "tsw_us": int(m.group(2)), "t_fast": int(m.group(3), 16),
                     "ton_min": int(m.group(4), 16), "toff_min": int(m.group(5), 16)}
    m = _IDLE.search(text)
    if m:
        out["idle_release_ms"] = int(m.group(1))
    m = _OCD.search(text)
    if m:
        out["ocd_th"] = int(m.group(1), 16)
    m = _STALL.search(text)
    if m:
        out["stall_th"] = int(m.group(1), 16)
    m = _MOTION.search(text)
    if m:
        out["acc_raw"], out["dec_raw"], out["maxspd_raw"] = (int(g) for g in m.groups())
    m = _SPEEDS.search(text)
    if m:
        out["minspd_raw"], out["fs_spd_raw"] = (int(g) for g in m.groups())
    m = _FAULTS.search(text)
    if m:
        out["faults"] = {k: g != "0" for k, g in zip(FAULT_KEYS, m.groups())}
    m = _ACTION.search(text)
    if m:
        out["action"] = m.group(1)
        out["action_index"] = FAULT_ACTIONS.index(m.group(1)) if m.group(1) in FAULT_ACTIONS else 0
    m = _STEPMODE.search(text)
    if m:
        out["step_mode"], out["microsteps"] = int(m.group(1)), int(m.group(2))
    return out


def encoder(resp: Response) -> Dict[str, Any]:
    """CTRL:ENC? -> {count, velocity, index_seen, index_tick, revolutions, index_period_us}."""
    d = resp.data or {}
    if "count" in d:                                         # JSON
        return {"count": int(d.get("count", 0)), "velocity": int(d.get("velocity", 0)),
                "index_seen": bool(d.get("index_seen", False)),
                "index_tick": int(d.get("index_tick", 0)),
                "revolutions": int(d.get("revolutions", 0)),
                "index_period_us": int(d.get("index_period_us", 0))}
    kv = dict(p.split("=", 1) for p in str(d.get("message", resp.raw)).split() if "=" in p)
    try:
        return {"count": int(kv.get("count", 0)), "velocity": int(kv.get("vel", 0)),
                "index_seen": kv.get("idx") == "1", "index_tick": int(kv.get("idx_tick", 0)),
                "revolutions": int(kv.get("rev", 0)), "index_period_us": 0}
    except ValueError:
        return {}


def sysid_status(resp: Response) -> Dict[str, int]:
    """CTRL:SYSID:STATUS? -> {phase, samples} (phase 4 = DONE)."""
    d = resp.data or {}
    if "phase" in d:
        return {"phase": int(d["phase"]), "samples": int(d.get("samples", 0))}
    kv = dict(p.split("=", 1) for p in str(d.get("message", resp.raw)).split() if "=" in p)
    try:
        return {"phase": int(kv.get("phase", -1)), "samples": int(kv.get("samples", 0))}
    except ValueError:
        return {"phase": -1, "samples": 0}


SYSID_DONE = 4


@dataclass
class SysIdChunk:
    offset: int
    total: int
    samples: List[Tuple[int, int, int, int]] = field(default_factory=list)
    # each sample: (time_ms, setpoint_tps, actual_tps, pos_error)


def sysid_data(resp: Response) -> SysIdChunk:
    """CTRL:SYSID:DATA? -> SysIdChunk. JSON carries offset/total as fields; ASCII has a
    `SYSID_DATA <offset> <samples> <total>` header line."""
    d = resp.data or {}
    offset, total = int(d.get("offset", 0)), int(d.get("total", -1))
    rows: List[Tuple[int, int, int, int]] = []
    for line in resp.lines or resp.raw.split("\n"):
        line = line.strip()
        if line.startswith("SYSID_DATA"):
            parts = line.split()
            if len(parts) >= 4:
                offset, total = int(parts[1]), int(parts[3])
            continue
        parts = line.split(",")
        if len(parts) >= 4:
            try:
                rows.append(tuple(int(p) for p in parts[:4]))   # type: ignore[arg-type]
            except ValueError:
                continue
    return SysIdChunk(offset, total, rows)


def capture_rows(resp: Response) -> List[Tuple[int, int]]:
    """CTRL:CAP:DATA? -> [(encoder_count, sync_count)], 16-bit wrapped values as sent."""
    rows: List[Tuple[int, int]] = []
    for line in resp.lines or resp.raw.split("\n"):
        parts = line.strip().split(",")
        if len(parts) == 2:
            try:
                rows.append((int(parts[0]), int(parts[1])))
            except ValueError:
                continue
    return rows


def unwrap16(values: List[int]) -> List[int]:
    """Continuous values from a 16-bit wrapped series (deltas within +-32768)."""
    out: List[int] = []
    acc = 0
    prev = None
    for v in values:
        if prev is None:
            acc = v
        else:
            d = (v - prev + 32768) % 65536 - 32768
            acc += d
        out.append(acc)
        prev = v
    return out


# DIAG:MEM? lines (system_handlers.cpp cmdMem): "<region> <used-key>=<b> total=<b>" and
# "task <name> stack=<words> hwm=<words>" (a task name may contain spaces)
_MEM_REGION = re.compile(r"^(flash|ram|stack_pool|msp)\s+(?:used|static)=(\d+)\s+total=(\d+)\s*$")
_MEM_TASK = re.compile(r"^task\s+(.+?)\s+stack=(\d+)\s+hwm=(\d+)\s*$")
STACK_WORD_BYTES = 4

# Headroom bands shared by the LCD page and the GUI: below 70 % fine, to 90 % tight, beyond full
MEM_TIGHT_PCT = 70.0
MEM_FULL_PCT = 90.0


def mem_level(pct: float) -> str:
    """'ok' below 70 %, 'tight' to 90 %, 'full' beyond (the LCD Memory page's colours, on the
    percentage rounded the way the LCD prints it)."""
    p = int(pct + 0.5)
    return "full" if p > MEM_FULL_PCT else "tight" if p >= MEM_TIGHT_PCT else "ok"


def _region(used: int, total: int) -> Dict[str, Any]:
    pct = 100.0 * used / total if total else 0.0
    return {"used": used, "total": total, "free": max(total - used, 0), "pct": pct, "level": mem_level(pct)}


def memory(resp_or_text) -> Dict[str, Any]:
    """DIAG:MEM? -> {flash, ram, stack_pool, msp: {used, total, free, pct, level} (bytes),
    split: {stacks, globals, free} (bytes of RAM), tasks: [{name, stack_words, hwm_words,
    size, used, free, pct, level}] biggest stack first, text}. Parts the board did not send
    are absent (msp on a board without a main-stack reserve; tasks without task stats)."""
    text = _text(resp_or_text)
    out: Dict[str, Any] = {"text": text}
    tasks: List[Dict[str, Any]] = []
    for line in text.split("\n"):
        line = line.strip()
        m = _MEM_REGION.match(line)
        if m:
            out[m.group(1)] = _region(int(m.group(2)), int(m.group(3)))
            continue
        m = _MEM_TASK.match(line)
        if m:
            words, hwm = int(m.group(2)), int(m.group(3))
            size, free = words * STACK_WORD_BYTES, hwm * STACK_WORD_BYTES
            used = max(size - free, 0)
            pct = 100.0 * used / size if size else 0.0
            tasks.append({"name": m.group(1), "stack_words": words, "hwm_words": hwm, "size": size,
                          "used": used, "free": free, "pct": pct, "level": mem_level(pct)})
    if "flash" in out or "ram" in out or tasks:
        tasks.sort(key=lambda t: (-t["size"], t["name"]))
        out["tasks"] = tasks
    if "ram" in out:
        ram = out["ram"]
        stacks = min(out.get("stack_pool", {}).get("total", 0), ram["used"])
        out["split"] = {"stacks": stacks, "globals": ram["used"] - stacks, "free": ram["free"]}
    return out


# ---------------------------------------------------------------------------
# Protocol 3: the motor driver in physical units (docs/PROTOCOL.md section 7)
# ---------------------------------------------------------------------------
# Keys whose values are not numbers, in DRV:CAPS? / DRV:CFG:PHYS? / the setter replies
_KV_FLAGS = {"voltage_mode", "current_mode", "stall_detect", "standby_pin", "hardware_positioning"}
_KV_LISTS = {"fault_bits", "faults_reported", "faults_alarmable", "alarms"}
_KV_TEXT = {"overcurrent_sense", "drive_mode", "source", "fault_action"}


def _kv_value(key: str, raw: str) -> Any:
    if key in _KV_FLAGS:
        return raw.strip().lower() in ("1", "true")
    if key in _KV_LISTS:
        raw = raw.strip()
        return [] if raw in ("", "NONE") else [s for s in raw.split(",") if s]
    if key in _KV_TEXT:
        return raw.strip()
    try:
        return int(raw)
    except ValueError:
        try:
            return float(raw)
        except ValueError:
            return raw.strip()


def _named_values(resp: Response) -> Dict[str, Any]:
    """The named values of a protocol 3 reply in either format: the JSON data object as is,
    or ASCII `key=value` lines (DRV:CAPS?, DRV:CFG:PHYS?) / one `OK <WHAT> key=value ...` line."""
    d = resp.data or {}
    if d and "lines" not in d and "message" not in d and "raw" not in d:
        return dict(d)                                        # JSON object
    out: Dict[str, Any] = {}
    texts = resp.lines or str(d.get("message", d.get("raw", resp.raw))).split()
    for item in texts:
        key, sep, raw = str(item).strip().partition("=")
        if sep and key and " " not in key:
            out[key] = _kv_value(key, raw)
    return out


def driver_caps(resp: Response) -> Dict[str, Any]:
    """DRV:CAPS? -> {voltage_mode, current_mode, max_microsteps_voltage, max_microsteps_current,
    max_current_a, current_step_a, duty_step, overcurrent_sense ('voltage'|'current'),
    overcurrent_ohms, overcurrent_min_a/max_a/step_a, stall_detect, stall_step, max_speed_fsps,
    max_speed_limit_fsps, speed_limit_step_fsps, accel_min/max/step_fsps2,
    min/max_position_usteps, fault_bits, faults_reported, faults_alarmable, ...}; {} when the
    board refused (a protocol 2 board answers UNKNOWN_CMD)."""
    return _named_values(resp) if resp.success else {}


def driver_physical(resp: Response) -> Dict[str, Any]:
    """DRV:CFG:PHYS? -> {source, drive_mode, microsteps_asked, microsteps (in force), hold_a,
    run_a, acc_a, dec_a, hold_duty..dec_duty, accel_fsps2, decel_fsps2, max_speed_fsps,
    overcurrent_a, overcurrent_v (voltage-sensing driver), stall_threshold, alarms,
    fault_action, idle_release_ms, full_steps_per_rev}; {} on an error."""
    return _named_values(resp) if resp.success else {}


def driver_setting(resp: Response) -> Dict[str, Any]:
    """A physical setter's or its query's reply (DRV:CFG:CURRENT ..., DRV:USTEP?) -> its named
    values, rounded by the driver ({hold_a, run_a, ...} or {microsteps_asked, microsteps}...)."""
    return _named_values(resp) if resp.success else {}


def resolution(resp: Response) -> Dict[str, Any]:
    """DRV:USTEP? (protocol 3) or DRV:STEP_MODE? (protocol 2) -> {microsteps, step_mode (log2),
    microsteps_per_rev, drive_mode}: the resolution in force, as the driver reads it back."""
    if not resp.success:
        return {}
    d = _named_values(resp)
    out: Dict[str, Any] = {}
    if "microsteps" in d:
        out["microsteps"] = int(d["microsteps"])
    elif "step_mode" in d:
        out["microsteps"] = 1 << int(d["step_mode"])
    elif "STEP_MODE" in d:                                     # protocol 2 ASCII line
        out["microsteps"] = 1 << int(d["STEP_MODE"])
    if "microsteps" in out:
        out["step_mode"] = int(out["microsteps"]).bit_length() - 1
    for k in ("microsteps_per_rev", "MICROSTEPS_PER_REV"):
        if k in d:
            out["microsteps_per_rev"] = int(d[k])
    if "drive_mode" in d:
        out["drive_mode"] = str(d["drive_mode"])
    return out
