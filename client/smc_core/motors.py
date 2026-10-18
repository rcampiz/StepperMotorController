"""
Motor profiles: one JSON file per motor with the nameplate, the drive settings that suit it
and its resonance bands, kept with the client (smc_core/motors/*.json) plus any the user
saves (~/.smc/motors/). The firmware's defaults stay the project's motor; a profile is
applied over them with the ordinary DRV:CFG commands, so nothing in the firmware knows
about profiles.

    profiles = list_profiles()                 # [Profile]
    p = load_profile("23HS22-2804-ME1K")
    for req in p.requests(): board.request(req)   # then DRV:CFG:SAVE if wanted
    export_profile(views.motor_config(resp), name) -> Profile from a board's DRV:CFG?
"""

import json
import os
from dataclasses import asdict, dataclass, field
from typing import Any, Dict, List, Optional, Tuple

from . import commands as C
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
    step_mode: Optional[int] = None             # 0..7 register value
    ocd_th: Optional[int] = None
    stall_th: Optional[int] = None
    idle_release_ms: Optional[int] = None
    resonance_bands_rpm: List[Tuple[float, float]] = field(default_factory=list)
    notes: str = ""
    source: str = ""                            # file path it came from

    @property
    def label(self) -> str:
        return self.title or self.name

    def requests(self) -> List[C.Request]:
        """The DRV:CFG commands that put a board into this profile, ending with APPLY."""
        reqs: List[C.Request] = []
        current = self.drive_mode == "current"
        reqs.append(C.cfg_drive_mode(current))
        if current and self.tval_a:
            t = self.tval_a
            reqs.append(C.cfg_tval(U.tval_from_amps(t.get("hold", 0)), U.tval_from_amps(t.get("run", 0)),
                                   U.tval_from_amps(t.get("acc", t.get("run", 0))),
                                   U.tval_from_amps(t.get("dec", t.get("run", 0)))))
        if self.kval_pct:
            k = self.kval_pct
            reqs.append(C.cfg_kval(U.kval_from_percent(k.get("hold", 0)), U.kval_from_percent(k.get("run", 0)),
                                   U.kval_from_percent(k.get("acc", k.get("run", 0))),
                                   U.kval_from_percent(k.get("dec", k.get("run", 0)))))
        if self.pred is not None:
            reqs.append(C.cfg_pred(self.pred, self.tsw_us))
        if self.step_mode is not None:
            reqs.append(C.cfg_step_mode(self.step_mode))
        if self.ocd_th is not None:
            reqs.append(C.cfg_ocd(self.ocd_th))
        if self.stall_th is not None:
            reqs.append(C.cfg_stall(self.stall_th))
        if self.idle_release_ms is not None:
            reqs.append(C.cfg_idle_release(self.idle_release_ms))
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
    """A profile from a board's DRV:CFG? view (smc_core.views.motor_config), so a tuned
    board can be kept as a file."""
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
