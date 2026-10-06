#!/usr/bin/env python3
"""
Check and summarise a chassis BOM export (from fusion/SMC_BOM_Export) before costing it.

    python tools/chassis_cost/bom_tool.py summary  path/to/design_bom.json
    python tools/chassis_cost/bom_tool.py cutlist  path/to/design_bom.json
    python tools/chassis_cost/bom_tool.py warnings path/to/design_bom.json

No prices here yet: the costing GUI and the price catalog come next, once the open
questions in README.md are answered.
"""

import argparse
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "fusion", "SMC_BOM_Export"))
import bom_core  # noqa: E402


def load(path):
    with open(path, encoding="utf-8") as fh:
        return bom_core.load_export(fh.read())


def summary(doc) -> str:
    parts = doc["parts"]
    lines = [f"{doc.get('design', '?')}  (exported {doc.get('exported_at', '?')})",
             f"{len(parts)} distinct parts, {sum(p['quantity'] for p in parts)} pieces", ""]
    by_cat = defaultdict(list)
    for p in parts:
        by_cat[p["category"]].append(p)
    for cat in bom_core.CATEGORIES:
        if cat not in by_cat:
            continue
        items = by_cat[cat]
        mass = sum(p["mass_kg"] * p["quantity"] for p in items)
        lines.append(f"{cat:12s} {len(items):3d} distinct  {sum(p['quantity'] for p in items):4d} pcs  "
                     f"{mass:7.3f} kg")
        for p in items:
            ext = p.get("extrusion") or {}
            extra = f"  {ext['profile']} x {ext['length_mm']:g} mm" if ext else ""
            pn = p["vendor_part_number"] or "(no part number)"
            lines.append(f"    {p['quantity']:3d} x {pn:12s} {p['name'][:40]:40s}{extra}")
    missing = sum(1 for p in parts if not p["vendor_part_number"])
    if missing:
        lines += ["", f"{missing} part(s) have no part number; costing needs one per part."]
    return "\n".join(lines)


def cutlist(doc) -> str:
    """Extrusion lengths per profile: what a cut-to-length order or a cutting plan needs."""
    rows = defaultdict(lambda: defaultdict(int))
    for p in doc["parts"]:
        ext = p.get("extrusion")
        if ext:
            rows[(ext["profile"], p["vendor_part_number"] or "?")][ext["length_mm"]] += p["quantity"]
    out = []
    for (profile, pn), lengths in sorted(rows.items()):
        total = sum(l * n for l, n in lengths.items())
        out.append(f"{profile}  {pn}   total {total / 1000:.3f} m")
        for length, n in sorted(lengths.items(), reverse=True):
            out.append(f"    {n:3d} x {length:8.1f} mm")
    return "\n".join(out) if out else "no extrusions found"


def warnings(doc) -> str:
    out = [f"{p['name']}: {w}" for p in doc["parts"] for w in p["warnings"]]
    return "\n".join(out) if out else "no warnings"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("command", choices=("summary", "cutlist", "warnings"))
    ap.add_argument("export", help="<design>_bom.json from SMC_BOM_Export")
    a = ap.parse_args(argv)
    doc = load(a.export)
    print({"summary": summary, "cutlist": cutlist, "warnings": warnings}[a.command](doc))
    return 0


if __name__ == "__main__":
    sys.exit(main())
