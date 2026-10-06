"""
bom_core: the Fusion-independent half of the chassis BOM export.

The Fusion script (SMC_BOM_Export.py) only walks the design and collects raw facts per
component; everything that decides something (category, extrusion profile and length,
vendor part number, aggregation, file formats) lives here, so it is unit-tested on a
normal Python install (tools/chassis_cost/tests) and behaves the same inside Fusion.

Units in the export: millimetres and kilograms.
"""

import csv
import io
import json
import re
from dataclasses import asdict, dataclass, field
from typing import Any, Dict, List, Optional, Sequence, Tuple

SCHEMA = "smc-chassis-bom/1"

# Naming convention (README, "Naming conventions"): a prefix sets the category.
PREFIX_CATEGORY = {
    "EXT": "extrusion", "BRK": "bracket", "FST": "fastener", "PLT": "plate",
    "DRV": "drive", "ELE": "electronics", "MISC": "other",
}
CATEGORIES = ("extrusion", "bracket", "fastener", "plate", "drive", "electronics", "other")

# EXT-1010-450   EXT-2020-300mm   EXT_1515_18in
EXT_NAME_RE = re.compile(r"^EXT[-_ ](?P<profile>\d{4})[-_ ](?P<length>\d+(?:\.\d+)?)\s*(?P<unit>mm|in)?",
                         re.IGNORECASE)
PREFIX_RE = re.compile(r"^(?P<prefix>[A-Z]{3,4})[-_ ]", re.IGNORECASE)
# McMaster-Carr part numbers look like 47065T101, 91251A540, 5537T21
MCMASTER_RE = re.compile(r"\b(\d{4,5}[A-Z]\d{1,4})\b")

# Standard T-slot cross-sections (mm, smaller side first) -> series name
STANDARD_PROFILES: Dict[Tuple[float, float], str] = {
    (20.0, 20.0): "2020", (20.0, 40.0): "2040", (20.0, 60.0): "2060", (20.0, 80.0): "2080",
    (30.0, 30.0): "3030", (30.0, 60.0): "3060", (40.0, 40.0): "4040", (40.0, 80.0): "4080",
    (25.4, 25.4): "1010", (25.4, 50.8): "1020", (25.4, 76.2): "1030", (50.8, 50.8): "2020in",
    (38.1, 38.1): "1515", (38.1, 76.2): "1530", (76.2, 76.2): "3030in",
}
PROFILE_TOLERANCE_MM = 0.3

KEYWORDS = (
    ("fastener", ("screw", "bolt", "nut", "washer", "t-nut", "tnut", "rivet", "standoff", "insert")),
    ("bracket", ("bracket", "gusset", "corner", "angle", "connector", "joining", "plate fastener")),
    ("plate", ("plate", "panel", "sheet", "deck")),
    ("drive", ("motor", "wheel", "mecanum", "hub", "coupler", "coupling", "shaft", "bearing", "mount")),
    ("electronics", ("nucleo", "board", "pcb", "battery", "raspberry", "pi ", "driver", "cable")),
    ("extrusion", ("extrusion", "t-slot", "tslot", "framing", "rail")),
)


@dataclass
class RawItem:
    """What the Fusion script reads for one component (and how many times it is used)."""
    component_id: str
    name: str
    part_number: str = ""
    description: str = ""
    material: str = ""
    mass_kg: float = 0.0                       # per instance
    bbox_mm: Tuple[float, float, float] = (0.0, 0.0, 0.0)   # oriented if available
    bbox_oriented: bool = False
    quantity: int = 1
    paths: List[str] = field(default_factory=list)
    attributes: Dict[str, str] = field(default_factory=dict)
    hidden: int = 0                            # instances that are hidden in the design


@dataclass
class PartRecord:
    key: str                                   # vendor part number, else a name-based key
    name: str
    category: str
    quantity: int
    part_number: str = ""
    vendor: str = ""
    vendor_part_number: str = ""
    description: str = ""
    material: str = ""
    mass_kg: float = 0.0
    bbox_mm: Tuple[float, float, float] = (0.0, 0.0, 0.0)
    extrusion: Optional[Dict[str, Any]] = None  # {profile, length_mm, source}
    attributes: Dict[str, str] = field(default_factory=dict)
    paths: List[str] = field(default_factory=list)
    warnings: List[str] = field(default_factory=list)


def sorted_dims(bbox: Sequence[float]) -> Tuple[float, float, float]:
    a = sorted(abs(float(x)) for x in bbox)
    return (a[0], a[1], a[2])


def match_profile(w: float, h: float) -> Optional[str]:
    lo, hi = sorted((w, h))
    for (pw, ph), name in STANDARD_PROFILES.items():
        if abs(lo - pw) <= PROFILE_TOLERANCE_MM and abs(hi - ph) <= PROFILE_TOLERANCE_MM:
            return name
    return None


def vendor_part_number(name: str, part_number: str, description: str = "") -> Tuple[str, str]:
    """(vendor, part number) when one can be read; the Part Number field wins."""
    for text in (part_number, name, description):
        m = MCMASTER_RE.search(text or "")
        if m:
            return "McMaster-Carr", m.group(1)
    return ("", part_number.strip()) if part_number.strip() else ("", "")


def detect_extrusion(name: str, bbox_mm: Sequence[float]) -> Optional[Dict[str, Any]]:
    """Profile and length from the name (EXT-<profile>-<length>[mm|in]) or, failing that,
    from a bounding box whose cross-section is a standard profile and whose length is at
    least 3 x the larger side."""
    m = EXT_NAME_RE.match(name.strip())
    if m:
        length = float(m.group("length"))
        if (m.group("unit") or "mm").lower() == "in":
            length *= 25.4
        out = {"profile": m.group("profile"), "length_mm": round(length, 1), "source": "name"}
        d = sorted_dims(bbox_mm)
        if d[2] > 0 and abs(d[2] - length) > 1.0:
            out["model_length_mm"] = round(d[2], 1)       # name and model disagree: report both
        return out
    a, b, length = sorted_dims(bbox_mm)
    profile = match_profile(a, b)
    if profile is not None and length >= 3.0 * b:
        return {"profile": profile, "length_mm": round(length, 1), "source": "geometry"}
    return None


def classify(name: str, description: str, extrusion: Optional[Dict[str, Any]]) -> str:
    m = PREFIX_RE.match(name.strip())
    if m and m.group("prefix").upper() in PREFIX_CATEGORY:
        return PREFIX_CATEGORY[m.group("prefix").upper()]
    if extrusion is not None:
        return "extrusion"
    text = f" {name} {description} ".lower()
    for category, words in KEYWORDS:
        if any(w in text for w in words):
            return category
    return "other"


def build_records(items: Sequence[RawItem]) -> List[PartRecord]:
    """One record per (vendor part number or name, extrusion length); quantities summed."""
    records: Dict[Tuple[str, Any], PartRecord] = {}
    for it in items:
        ext = detect_extrusion(it.name, it.bbox_mm)
        vendor, vpn = vendor_part_number(it.name, it.part_number, it.description)
        category = classify(it.name, it.description, ext)
        key = vpn or re.sub(r"\s*\(\d+\)$", "", it.name.strip())     # Fusion's "Bolt (1)" copies
        group = (key, ext["length_mm"] if ext else None)
        warnings = []
        if not vpn:
            warnings.append("no part number: set the component's Part Number to the vendor's")
        if category == "extrusion" and ext is None:
            warnings.append("extrusion without a readable profile/length: name it EXT-<profile>-<length>")
        if ext and "model_length_mm" in ext:
            warnings.append(f"name says {ext['length_mm']} mm, model is {ext['model_length_mm']} mm")
        if it.hidden:
            warnings.append(f"{it.hidden} instance(s) hidden in the design (still counted)")
        rec = records.get(group)
        if rec is None:
            records[group] = PartRecord(
                key=key, name=it.name, category=category, quantity=it.quantity,
                part_number=it.part_number, vendor=vendor, vendor_part_number=vpn,
                description=it.description, material=it.material, mass_kg=round(it.mass_kg, 5),
                bbox_mm=tuple(round(x, 2) for x in sorted_dims(it.bbox_mm)), extrusion=ext,
                attributes=dict(it.attributes), paths=list(it.paths), warnings=warnings)
        else:
            rec.quantity += it.quantity
            rec.paths.extend(it.paths)
            for w in warnings:
                if w not in rec.warnings:
                    rec.warnings.append(w)
    return sorted(records.values(), key=lambda r: (CATEGORIES.index(r.category), r.key,
                                                  (r.extrusion or {}).get("length_mm", 0)))


def export_json(records: Sequence[PartRecord], meta: Dict[str, Any]) -> str:
    doc = {"schema": SCHEMA, "units": {"length": "mm", "mass": "kg"}, **meta,
           "parts": [asdict(r) for r in records]}
    return json.dumps(doc, indent=2)


CSV_FIELDS = ("key", "category", "quantity", "vendor", "vendor_part_number", "name", "description",
              "material", "mass_kg", "profile", "length_mm", "bbox_mm", "warnings")


def export_csv(records: Sequence[PartRecord]) -> str:
    buf = io.StringIO()
    w = csv.writer(buf, lineterminator="\n")
    w.writerow(CSV_FIELDS)
    for r in records:
        w.writerow([r.key, r.category, r.quantity, r.vendor, r.vendor_part_number, r.name,
                    r.description, r.material, r.mass_kg,
                    (r.extrusion or {}).get("profile", ""), (r.extrusion or {}).get("length_mm", ""),
                    "x".join("%g" % x for x in r.bbox_mm), " | ".join(r.warnings)])
    return buf.getvalue()


def load_export(text: str) -> Dict[str, Any]:
    doc = json.loads(text)
    if doc.get("schema") != SCHEMA:
        raise ValueError(f"not a {SCHEMA} file (schema={doc.get('schema')!r})")
    return doc
