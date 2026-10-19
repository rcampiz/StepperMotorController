"""
Motor profiles: one JSON file per motor with the nameplate, the drive settings that suit it
and its resonance bands, kept with the client (smc_core/motors/*.json) plus any the user
saves (~/.smc/motors/). The firmware's defaults stay the project's motor; a profile is
applied over them with the ordinary DRV:CFG commands, so nothing in the firmware knows
about profiles. A profile is physical (amps, duty in percent, microsteps, amps of
overcurrent, a 0..1 stall threshold); requests() sends the protocol 3 physical commands, or
with physical=False the protocol 2 chip-code aliases (smc_core.driver translates). Older files
with step_mode / ocd_th / stall_th codes still load and apply.

    profiles = list_profiles()                 # [Profile]
    p = load_profile("23HS22-2804-ME1K")
    for req in p.requests(driver.supports_physical(board.protocol)): board.request(req)
    export_profile(views.driver_physical(resp), name) -> Profile from a board's DRV:CFG:PHYS?
    (a DRV:CFG? view, smc_core.views.motor_config, works too)
"""

import json
import os
from dataclasses import asdict, dataclass, field
from typing import Any, Dict, List, Optional, Tuple

from . import commands as C
from . import driver as D
from . import units as U

PACKAGE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "motors")
USER_DIR = os.path.join(os.path.expanduser("~"), ".smc", "motors")


@dataclass
class Profile:
    name: str                                   # file stem, e.g. 23HS22-2804-ME1K
    title: str = ""                             # what the picker shows
    manufacturer: str = ""
    rated_current_a: float = 0.0
    phase_resistance_ohm: float = 0.0
    inductance_mh: float = 0.0
    full_steps_per_rev: int = 200
    encoder_cpr: int = 0                        # 0 = no encoder
    drive_mode: str = "current"                 # current | voltage
    tval_a: Dict[str, float] = field(default_factory=dict)      # hold/run/acc/dec in amps
    kval_pct: Dict[str, int] = field(default_factory=dict)      # hold/run/acc/dec in percent
    pred: Optional[bool] = None
    tsw_us: Optional[int] = None
    microsteps: Optional[int] = None            # microsteps per full step asked for (power of two)
    overcurrent_a: Optional[float] = None       # overcurrent threshold, amps
    stall_threshold: Optional[float] = None     # 0..1 on the driver's scale
    step_mode: Optional[int] = None             # older files: log2 step-mode code (0..7)
    ocd_th: Optional[int] = None                # older files: powerSTEP01 OCD_TH code
    stall_th: Optional[int] = None              # older files: powerSTEP01 STALL_TH code
    idle_release_ms: Optional[int] = None
    resonance_bands_rpm: List[Tuple[float, float]] = field(default_factory=list)
    notes: str = ""
    source: str = ""                            # file path it came from

    @property
    def label(self) -> str:
        return self.title or self.name

    def resolution(self) -> Optional[int]:
        """Microsteps per full step asked for (the field, or an older file's step-mode code)."""
        if self.microsteps:
            return int(self.microsteps)
        return (1 << int(self.step_mode)) if self.step_mode is not None else None

    def changes(self) -> Dict[str, Any]:
        """The profile as a smc_core.driver change set (physical units)."""
        ch: Dict[str, Any] = {"drive_mode": "current" if self.drive_mode == "current" else "voltage"}
        if ch["drive_mode"] == "current" and self.tval_a:
            t = self.tval_a
            ch["current_a"] = (t.get("hold", 0.0), t.get("run", 0.0), t.get("acc", t.get("run", 0.0)),
                               t.get("dec", t.get("run", 0.0)))
        if self.kval_pct:
            k = self.kval_pct
            ch["duty"] = tuple(min(1.0, max(0.0, float(v) / 100.0)) for v in
                               (k.get("hold", 0), k.get("run", 0), k.get("acc", k.get("run", 0)),
                                k.get("dec", k.get("run", 0))))
        if self.resolution() is not None:
            ch["microsteps"] = self.resolution()
        if self.overcurrent_a is not None:
            ch["overcurrent_a"] = float(self.overcurrent_a)
        if self.stall_threshold is not None:
            ch["stall_threshold"] = float(self.stall_threshold)
        if self.idle_release_ms is not None:
            ch["idle_release_ms"] = int(self.idle_release_ms)
        return ch

    def requests(self, physical: bool = True) -> List[C.Request]:
        """The DRV:CFG commands that put a board into this profile, ending with APPLY: the
        protocol 3 physical commands, or (physical=False, a protocol 2 board) the chip-code
        aliases. PRED/TSW (powerSTEP01 regulation tuning) and an older file's OCD/STALL codes
        go as the protocol 2 commands either way (protocol 3 still answers them)."""
        reqs = D.settings_requests(self.changes(), physical)
        tail = [r for r in reqs if r.name == "DRV:CFG:IDLE"]
        reqs = [r for r in reqs if r.name != "DRV:CFG:IDLE"]
        if self.pred is not None:
            reqs.append(C.cfg_pred(self.pred, self.tsw_us))
        if self.ocd_th is not None and self.overcurrent_a is None:
            reqs.append(C.cfg_ocd(self.ocd_th))
        if self.stall_th is not None and self.stall_threshold is None:
            reqs.append(C.cfg_stall(self.stall_th))
        reqs += tail
        reqs.append(C.cfg_apply())
        return reqs

    def to_dict(self) -> Dict[str, Any]:
        d = asdict(self)
        d.pop("source", None)
        d.pop("name", None)
        return {k: v for k, v in d.items() if v not in (None, "", {}, [])}


def _from_dict(name: str, d: Dict[str, Any], source: str = "") -> Profile:
    known = {f for f in Profile.__dataclass_fields__} - {"name", "source"}
    kwargs = {k: v for k, v in d.items() if k in known}
    if "resonance_bands_rpm" in kwargs:
        kwargs["resonance_bands_rpm"] = [tuple(float(x) for x in b) for b in kwargs["resonance_bands_rpm"]]
    return Profile(name=name, source=source, **kwargs)


def profile_dirs() -> List[str]:
    return [PACKAGE_DIR, USER_DIR]


def list_profiles() -> List[Profile]:
    """Package profiles first, then the user's; a user file with the same name wins."""
    found: Dict[str, Profile] = {}
    for d in profile_dirs():
        if not os.path.isdir(d):
            continue
        for fn in sorted(os.listdir(d)):
            if not fn.endswith(".json"):
                continue
            path = os.path.join(d, fn)
            try:
                with open(path, encoding="utf-8") as fh:
                    found[fn[:-5]] = _from_dict(fn[:-5], json.load(fh), path)
            except (OSError, ValueError, TypeError):
                continue
    return [found[k] for k in sorted(found, key=lambda s: s.lower())]


def load_profile(name: str) -> Profile:
    for p in list_profiles():
        if p.name == name or p.label == name:
            return p
    raise KeyError(f"no motor profile named {name!r}; known: {[p.name for p in list_profiles()]}")


def save_profile(profile: Profile, directory: Optional[str] = None) -> str:
    """Write the profile as JSON (the user's folder by default); returns the path."""
    d = directory or USER_DIR
    os.makedirs(d, exist_ok=True)
    path = os.path.join(d, profile.name + ".json")
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(profile.to_dict(), fh, indent=2)
        fh.write("\n")
    profile.source = path
    return path


def export_profile(config: Dict[str, Any], name: str, title: str = "", **extra) -> Profile:
    """A profile from a board's DRV:CFG:PHYS? view (smc_core.views.driver_physical) or, from a
    protocol 2 board, its DRV:CFG? view (smc_core.views.motor_config), so a tuned board can be
    kept as a file. The current-regulation tuning (PRED/TSW) comes from a DRV:CFG? view only:
    pass its "cm" dict as `regulation` to keep it with a physical export."""
    regulation = extra.pop("regulation", None)
    if "hold_a" in config or "microsteps_asked" in config:
        return _export_physical(config, name, title, regulation, **extra)
    p = Profile(name=name, title=title or name, **extra)
    mode = str(config.get("drive_mode", "current")).lower()
    p.drive_mode = "current" if mode.startswith("cur") else "voltage"
    if config.get("tval"):
        h, r, a, d = config["tval"]
        p.tval_a = {"hold": U.tval_to_amps(h), "run": U.tval_to_amps(r), "acc": U.tval_to_amps(a), "dec": U.tval_to_amps(d)}
    if config.get("kval"):
        h, r, a, d = config["kval"]
        p.kval_pct = {"hold": U.kval_to_percent(h), "run": U.kval_to_percent(r), "acc": U.kval_to_percent(a), "dec": U.kval_to_percent(d)}
    cm = config.get("cm") or {}
    if "pred" in cm:
        p.pred = bool(cm["pred"])
        p.tsw_us = int(cm.get("tsw_us")) if cm.get("tsw_us") else None
    if config.get("step_mode") is not None:
        p.step_mode = int(config["step_mode"])
    if config.get("ocd_th") is not None:
        p.ocd_th = int(config["ocd_th"])
    if config.get("stall_th") is not None:
        p.stall_th = int(config["stall_th"])
    if config.get("idle_release_ms") is not None:
        p.idle_release_ms = int(config["idle_release_ms"])
    return p


def _export_physical(phys: Dict[str, Any], name: str, title: str, regulation, **extra) -> Profile:
    p = Profile(name=name, title=title or name, **extra)
    p.drive_mode = "voltage" if str(phys.get("drive_mode", "current")).startswith("vol") else "current"
    if "hold_a" in phys:
        p.tval_a = {k: round(float(phys["%s_a" % k]), 3) for k in ("hold", "run", "acc", "dec")}
    if "hold_duty" in phys:
        p.kval_pct = {k: int(round(float(phys["%s_duty" % k]) * 100)) for k in ("hold", "run", "acc", "dec")}
    if phys.get("microsteps_asked"):
        p.microsteps = int(phys["microsteps_asked"])
    if phys.get("overcurrent_a") is not None:
        p.overcurrent_a = round(float(phys["overcurrent_a"]), 3)
    if phys.get("stall_threshold") is not None:
        p.stall_threshold = round(float(phys["stall_threshold"]), 4)
    if phys.get("idle_release_ms") is not None:
        p.idle_release_ms = int(phys["idle_release_ms"])
    cm = regulation or {}
    if "pred" in cm:
        p.pred = bool(cm["pred"])
        p.tsw_us = int(cm.get("tsw_us")) if cm.get("tsw_us") else None
    return p
