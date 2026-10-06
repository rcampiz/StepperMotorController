"""
STEP import for the chassis cost tool: read a STEP assembly exported from Fusion (or any
CAD) and produce the same records as the Fusion script (bom_core), without Fusion.

    python step_import.py base.step -o base_bom.json      # also writes base_bom.csv

What comes from the file:
- the assembly tree (OpenCascade XCAF): part names and how many times each part is used,
  through nested sub-assemblies;
- each part's size from an oriented bounding box of its own geometry, so an extrusion's
  length and cross-section are right whatever its placement in the assembly;
- volume (mm^3; STEP files rarely carry density, so no mass);
- PRODUCT id/name/description records read from the file text: when the exporting CAD
  writes a part number into the PRODUCT id, it becomes the part number.

Needs cadquery-ocp (OpenCascade bindings): pip install cadquery-ocp
"""

import argparse
import os
import re
import sys
import time
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_HERE, "fusion", "SMC_BOM_Export"))
import bom_core  # noqa: E402

PRODUCT_RE = re.compile(r"=\s*PRODUCT\s*\(\s*'((?:[^']|'')*)'\s*,\s*'((?:[^']|'')*)'\s*,\s*'((?:[^']|'')*)'",
                        re.IGNORECASE)


@dataclass
class StepStats:
    parts: int
    instances: int
    read_s: float
    measure_s: float


def _decode(s: str) -> str:
    """STEP string escapes: '' and \\X2\\hhhh...\\X0\\ (UTF-16)."""
    s = s.replace("''", "'")

    def x2(m):
        h = m.group(1)
        return "".join(chr(int(h[i:i + 4], 16)) for i in range(0, len(h), 4))
    return re.sub(r"\\X2\\([0-9A-Fa-f]+)\\X0\\", x2, s)


def product_table(path: str) -> Dict[str, Tuple[str, str]]:
    """PRODUCT name -> (id, description), from the file text (cheap, no geometry)."""
    table: Dict[str, Tuple[str, str]] = {}
    with open(path, encoding="latin-1", errors="replace") as fh:
        text = fh.read()
    for pid, name, desc in PRODUCT_RE.findall(text):
        table.setdefault(_decode(name), (_decode(pid), _decode(desc)))
    return table


def _label_name(label) -> str:
    from OCP.TDataStd import TDataStd_Name
    attr = TDataStd_Name()
    if label.FindAttribute(TDataStd_Name.GetID_s(), attr):
        return attr.Get().ToExtString()
    return ""


def _measure(shape) -> Tuple[Tuple[float, float, float], float]:
    """(oriented box dims in mm, smallest first; volume in mm^3)."""
    from OCP.Bnd import Bnd_OBB
    from OCP.BRepBndLib import BRepBndLib
    from OCP.BRepGProp import BRepGProp
    from OCP.GProp import GProp_GProps
    obb = Bnd_OBB()
    BRepBndLib.AddOBB_s(shape, obb, False, True, False)   # no triangulation, optimal box
    dims = tuple(sorted((2 * obb.XHSize(), 2 * obb.YHSize(), 2 * obb.ZHSize())))
    props = GProp_GProps()
    BRepGProp.VolumeProperties_s(shape, props)
    return dims, abs(props.Mass())


def read_step(path: str) -> Tuple[List[bom_core.RawItem], StepStats]:
    from OCP.IFSelect import IFSelect_RetDone
    from OCP.STEPCAFControl import STEPCAFControl_Reader
    from OCP.TCollection import TCollection_ExtendedString
    from OCP.TDF import TDF_Label
    try:                                         # OCP 7.x
        from OCP.TDF import TDF_LabelSequence
    except ImportError:                          # OCP 8.x binds NCollection types here
        from OCP.collections import Sequence_TDF_Label as TDF_LabelSequence
    from OCP.TDocStd import TDocStd_Document
    from OCP.XCAFDoc import XCAFDoc_DocumentTool

    t0 = time.perf_counter()
    doc = TDocStd_Document(TCollection_ExtendedString("XmlOcaf"))
    reader = STEPCAFControl_Reader()
    reader.SetNameMode(True)
    if reader.ReadFile(path) != IFSelect_RetDone:
        raise ValueError(f"cannot read STEP file {path}")
    if not reader.Transfer(doc):
        raise ValueError(f"STEP transfer failed for {path}")
    tool = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main())
    read_s = time.perf_counter() - t0

    t1 = time.perf_counter()
    products = product_table(path)
    items: Dict[str, bom_core.RawItem] = {}

    def part(label, inst_path: str) -> None:
        key = label.EntryDumpToString() if hasattr(label, "EntryDumpToString") else _label_name(label)
        it = items.get(key)
        if it is None:
            name = _label_name(label) or "unnamed"
            pid, desc = products.get(name, ("", ""))
            dims, volume = _measure(tool.GetShape_s(label))
            it = bom_core.RawItem(
                component_id=key, name=name,
                part_number=pid if pid and pid != name else "",
                description=desc if desc and desc != name else "",
                bbox_mm=dims, bbox_oriented=True, quantity=0,
                attributes={"volume_mm3": "%.1f" % volume, "source": "step"})
            items[key] = it
        it.quantity += 1
        it.paths.append(inst_path)

    def walk(label, inst_path: str) -> None:
        if tool.IsAssembly_s(label):
            comps = TDF_LabelSequence()
            tool.GetComponents_s(label, comps, False)
            for i in range(1, comps.Length() + 1):
                comp = comps.Value(i)
                ref = TDF_Label()
                target = ref if tool.GetReferredShape_s(comp, ref) else comp
                child = _label_name(comp) or _label_name(target)
                walk(target, f"{inst_path}/{child}")
        else:
            part(label, inst_path)

    roots = TDF_LabelSequence()
    tool.GetFreeShapes(roots)
    for i in range(1, roots.Length() + 1):
        root = roots.Value(i)
        walk(root, _label_name(root) or "root")
    measure_s = time.perf_counter() - t1
    raw = list(items.values())
    return raw, StepStats(len(raw), sum(r.quantity for r in raw), read_s, measure_s)


def import_step(path: str) -> Tuple[str, str, StepStats]:
    """STEP file -> (JSON text, CSV text, stats) in the bom_core export format."""
    raw, stats = read_step(path)
    records = bom_core.build_records(raw)
    meta = {"design": os.path.splitext(os.path.basename(path))[0], "document": os.path.basename(path),
            "exported_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "exporter": "step_import 1",
            "fusion_version": "n/a (STEP)",
            "step_stats": {"parts": stats.parts, "instances": stats.instances,
                           "read_s": round(stats.read_s, 3), "measure_s": round(stats.measure_s, 3)}}
    return bom_core.export_json(records, meta), bom_core.export_csv(records), stats


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="STEP assembly -> chassis BOM (JSON + CSV)")
    ap.add_argument("step", help="STEP file (.step / .stp)")
    ap.add_argument("-o", "--out", help="output JSON (default: <step>_bom.json); CSV beside it")
    a = ap.parse_args(argv)
    out = a.out or os.path.splitext(a.step)[0] + "_bom.json"
    js, csv_text, stats = import_step(a.step)
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(js)
    with open(os.path.splitext(out)[0] + ".csv", "w", encoding="utf-8", newline="") as fh:
        fh.write(csv_text)
    print(f"{stats.parts} distinct parts, {stats.instances} pieces; read {stats.read_s:.2f} s, "
          f"measured {stats.measure_s:.2f} s -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
