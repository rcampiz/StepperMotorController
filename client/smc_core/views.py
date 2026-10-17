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
_KVAL = re.compile(r"KVAL:\s*HOLD=([0-9A-Fa-f]+)\s*RUN=([0-9A-Fa-f]+)\s*ACC=([0-9A-Fa-f]+)"
                   r"\s*DEC=([0-9A-Fa-f]+)")
_MOTION = re.compile(r"\bACC=(\d+)\s+DEC=(\d+)\s+MAXSPD=(\d+)")
_SPEEDS = re.compile(r"MINSPD=(\d+)\s+FS_SPD=(\d+)")
_STATUS = re.compile(r"STATUS=([0-9A-Fa-f]{4})")
_POS = re.compile(r"POS=(-?\d+)")
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
    position, text}. Missing fields are absent."""
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
