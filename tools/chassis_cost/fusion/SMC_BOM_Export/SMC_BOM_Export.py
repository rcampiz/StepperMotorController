"""
SMC_BOM_Export: Fusion 360 script that exports the active design's parts list for the
chassis cost tool (tools/chassis_cost/README.md).

Run it from Fusion: Utilities > Add-Ins > Scripts and Add-Ins > Scripts tab > "+" next to
"My Scripts" > pick this folder > select SMC_BOM_Export > Run. It asks for an output
folder and writes <design>_bom.json and <design>_bom.csv.

For every component that has solid bodies it records: name, Part Number, Description,
material, mass per instance, the size of its bodies (an orientation-independent box when
Fusion provides one), how many times the design uses it, where, and any attributes in the
"chassis_cost" group. Sub-assemblies without bodies are walked but not listed.

Written against the documented Fusion API; not run inside Fusion by the author. The
decision logic is in bom_core.py and is unit-tested outside Fusion.
"""

import os
import sys
import traceback
from datetime import datetime, timezone

import adsk.core
import adsk.fusion

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)
import bom_core  # noqa: E402

ATTRIBUTE_GROUP = "chassis_cost"
CM_TO_MM = 10.0


def _body_dims_mm(comp):
    """Size of the component's bodies in mm, smallest first, and whether it is oriented.

    BRepBody.orientedMinimumBoundingBox (when available) gives the true length of a
    rotated or angled extrusion; otherwise the component-space axis-aligned box is used.
    """
    dims = []
    oriented = True
    for body in comp.bRepBodies:
        if not body.isSolid:
            continue
        box = None
        try:
            obb = body.orientedMinimumBoundingBox
            if obb is not None:
                box = (obb.length, obb.width, obb.height)
        except Exception:  # noqa: BLE001 - older Fusion builds
            box = None
        if box is None:
            oriented = False
            bb = body.boundingBox
            box = (bb.maxPoint.x - bb.minPoint.x, bb.maxPoint.y - bb.minPoint.y,
                   bb.maxPoint.z - bb.minPoint.z)
        dims.append(tuple(sorted(abs(x) * CM_TO_MM for x in box)))
    if not dims:
        return (0.0, 0.0, 0.0), False
    # several bodies: report the largest one (the part a buyer orders)
    return max(dims, key=lambda d: d[0] * d[1] * d[2]), oriented


def _mass_kg(comp):
    try:
        return comp.getPhysicalProperties(adsk.fusion.CalculationAccuracy.LowCalculationAccuracy).mass
    except Exception:  # noqa: BLE001
        try:
            return comp.physicalProperties.mass
        except Exception:  # noqa: BLE001
            return 0.0


def _material(comp):
    try:
        if comp.material is not None:
            return comp.material.name
    except Exception:  # noqa: BLE001
        pass
    for body in comp.bRepBodies:
        try:
            if body.material is not None:
                return body.material.name
        except Exception:  # noqa: BLE001
            pass
    return ""


def _attributes(comp):
    out = {}
    try:
        for i in range(comp.attributes.count):
            a = comp.attributes.item(i)
            if a.groupName == ATTRIBUTE_GROUP:
                out[a.name] = a.value
    except Exception:  # noqa: BLE001
        pass
    return out


def _component_key(comp):
    for attr in ("id", "entityToken"):
        try:
            v = getattr(comp, attr)
            if v:
                return str(v)
        except Exception:  # noqa: BLE001
            pass
    return comp.name


def collect(design):
    root = design.rootComponent
    items = {}

    def add(comp, path, visible):
        if comp.bRepBodies.count == 0:
            return
        key = _component_key(comp)
        it = items.get(key)
        if it is None:
            dims, oriented = _body_dims_mm(comp)
            it = bom_core.RawItem(
                component_id=key, name=comp.name, part_number=comp.partNumber or "",
                description=comp.description or "", material=_material(comp),
                mass_kg=_mass_kg(comp), bbox_mm=dims, bbox_oriented=oriented, quantity=0,
                attributes=_attributes(comp))
            items[key] = it
        it.quantity += 1
        it.paths.append(path)
        if not visible:
            it.hidden += 1

    add(root, root.name, True)
    for occ in root.allOccurrences:
        try:
            visible = occ.isLightBulbOn
        except Exception:  # noqa: BLE001
            visible = True
        add(occ.component, occ.fullPathName, visible)
    return list(items.values())


def run(context):
    ui = None
    try:
        app = adsk.core.Application.get()
        ui = app.userInterface
        design = adsk.fusion.Design.cast(app.activeProduct)
        if design is None:
            ui.messageBox("Open a design (not a drawing) first.")
            return
        dlg = ui.createFolderDialog()
        dlg.title = "Folder for the chassis BOM export"
        if dlg.showDialog() != adsk.core.DialogResults.DialogOK:
            return
        items = collect(design)
        records = bom_core.build_records(items)
        name = design.rootComponent.name or "design"
        safe = "".join(c if c.isalnum() or c in "-_" else "_" for c in name)
        meta = {
            "design": name,
            "document": app.activeDocument.name if app.activeDocument else "",
            "exported_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
            "exporter": "SMC_BOM_Export 1",
            "fusion_version": app.version,
        }
        json_path = os.path.join(dlg.folder, f"{safe}_bom.json")
        csv_path = os.path.join(dlg.folder, f"{safe}_bom.csv")
        with open(json_path, "w", encoding="utf-8") as fh:
            fh.write(bom_core.export_json(records, meta))
        with open(csv_path, "w", encoding="utf-8", newline="") as fh:
            fh.write(bom_core.export_csv(records))
        counts = {}
        for r in records:
            counts[r.category] = counts.get(r.category, 0) + r.quantity
        warned = sum(1 for r in records if r.warnings)
        ui.messageBox(
            f"{len(records)} distinct parts, {sum(r.quantity for r in records)} pieces\n"
            + "\n".join(f"  {k}: {v}" for k, v in sorted(counts.items()))
            + f"\n\n{warned} part(s) with warnings (see the CSV 'warnings' column)\n\n"
            f"{json_path}\n{csv_path}")
    except Exception:  # noqa: BLE001
        if ui:
            ui.messageBox("SMC_BOM_Export failed:\n" + traceback.format_exc())
