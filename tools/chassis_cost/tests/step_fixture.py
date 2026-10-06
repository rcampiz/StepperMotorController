"""Build STEP assemblies with OpenCascade for the importer tests (no CAD file needed).

A part is a named prototype; the assembly places instances of it. Shapes: boxes for
40x40 / 40x80 rails (one modelled at an angle in its own coordinates), brackets and a
plate, cylinders for screws.
"""

import math

from OCP.BRepBuilderAPI import BRepBuilderAPI_Transform
from OCP.BRepPrimAPI import BRepPrimAPI_MakeBox, BRepPrimAPI_MakeCylinder
from OCP.gp import gp_Ax1, gp_Dir, gp_Pnt, gp_Trsf, gp_Vec
from OCP.STEPCAFControl import STEPCAFControl_Writer
from OCP.STEPControl import STEPControl_AsIs
from OCP.TCollection import TCollection_ExtendedString
from OCP.TDataStd import TDataStd_Name
from OCP.TDocStd import TDocStd_Document
from OCP.TopLoc import TopLoc_Location
from OCP.XCAFDoc import XCAFDoc_DocumentTool


def box(x, y, z):
    return BRepPrimAPI_MakeBox(x, y, z).Shape()


def rotated(shape, deg):
    t = gp_Trsf()
    t.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), math.radians(deg))
    return BRepBuilderAPI_Transform(shape, t, True).Shape()


def screw(d, length):
    return BRepPrimAPI_MakeCylinder(d / 2.0, length).Shape()


def write_assembly(path, parts, placements, root_name="Base"):
    """parts: {name: shape}; placements: [(part name, x, y, z, sub-assembly or None)]."""
    doc = TDocStd_Document(TCollection_ExtendedString("XmlOcaf"))
    tool = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main())
    labels = {}
    for name, shape in parts.items():
        lab = tool.AddShape(shape, False)
        TDataStd_Name.Set_s(lab, TCollection_ExtendedString(name))
        labels[name] = lab
    root = tool.NewShape()
    TDataStd_Name.Set_s(root, TCollection_ExtendedString(root_name))
    subs = {}
    for name, x, y, z, sub in placements:
        parent = root
        if sub:
            if sub not in subs:
                s = tool.NewShape()
                TDataStd_Name.Set_s(s, TCollection_ExtendedString(sub))
                subs[sub] = s
            parent = subs[sub]
        t = gp_Trsf()
        t.SetTranslation(gp_Vec(x, y, z))
        tool.AddComponent(parent, labels[name], TopLoc_Location(t))
    for s in subs.values():
        t = gp_Trsf()
        tool.AddComponent(root, s, TopLoc_Location(t))
    tool.UpdateAssemblies()
    w = STEPCAFControl_Writer()
    w.SetNameMode(True)
    w.Transfer(doc, STEPControl_AsIs)
    w.Write(path)


def mecanum_base(path, screws=48):
    """A small base: 40x40 and 40x80 rails, brackets, a deck, screws in a sub-assembly."""
    parts = {
        "EXT-4040-500": box(40, 40, 500),
        "EXT-4080-450": box(40, 80, 450),
        "Rail": rotated(box(300, 40, 40), 30),      # unnamed, angled in its own frame
        "BRK-corner-40": box(40, 40, 5),
        "PLT-deck": box(450, 500, 3),
        "FST-M8x16": screw(8, 16),
    }
    placements = [("EXT-4040-500", 0, 0, 0, None), ("EXT-4040-500", 400, 0, 0, None),
                  ("EXT-4080-450", 0, 0, 0, None), ("EXT-4080-450", 0, 500, 0, None),
                  ("Rail", 0, 0, 100, None)]
    placements += [("BRK-corner-40", i * 50, 0, 0, None) for i in range(8)]
    placements += [("PLT-deck", 0, 0, 60, None)]
    placements += [("FST-M8x16", i * 10, 0, 0, "Fasteners") for i in range(screws)]
    write_assembly(path, parts, placements)


def slotted_4040(length):
    """A 40x40 T-slot-like profile: four T-slots and a centre bore cut from the bar."""
    from OCP.BRepAlgoAPI import BRepAlgoAPI_Cut
    shape = box(40, 40, length)
    cutters = []
    for (x, y, w, h) in ((16, -1, 8, 7), (16, 34, 8, 7), (-1, 16, 7, 8), (34, 16, 7, 8),     # slot necks
                         (11, 4, 18, 6), (11, 30, 18, 6), (4, 11, 6, 18), (30, 11, 6, 18)):  # slot heads
        t = gp_Trsf()
        t.SetTranslation(gp_Vec(x, y, -1))
        cutters.append(BRepBuilderAPI_Transform(box(w, h, length + 2), t, True).Shape())
    t = gp_Trsf()
    t.SetTranslation(gp_Vec(20, 20, -1))
    cutters.append(BRepBuilderAPI_Transform(BRepPrimAPI_MakeCylinder(4.2, length + 2).Shape(), t, True).Shape())
    for c in cutters:
        shape = BRepAlgoAPI_Cut(shape, c).Shape()
    return shape


def grooved_bolt(d, length, grooves=24):
    """A bolt with many ring grooves: a stand-in for a modelled thread (lots of faces)."""
    from OCP.BRepAlgoAPI import BRepAlgoAPI_Cut
    from OCP.BRepPrimAPI import BRepPrimAPI_MakeTorus
    shape = screw(d, length)
    pitch = length / (grooves + 1)
    for i in range(1, grooves + 1):
        t = gp_Trsf()
        t.SetTranslation(gp_Vec(0, 0, i * pitch))
        ring = BRepBuilderAPI_Transform(BRepPrimAPI_MakeTorus(d / 2.0, pitch / 3.0).Shape(), t, True).Shape()
        shape = BRepAlgoAPI_Cut(shape, ring).Shape()
    return shape


def realistic_base(path, lengths=(500, 450, 400, 350, 300, 250, 200, 150), screws=200):
    """Distinct slotted rails (one per length) and threaded-looking bolts."""
    parts = {f"EXT-4040-{L}": slotted_4040(L) for L in lengths}
    parts["FST-M8x20"] = grooved_bolt(8, 20)
    parts["FST-M8x30"] = grooved_bolt(8, 30, grooves=36)
    placements = [(f"EXT-4040-{L}", 0, i * 50, 0, None) for i, L in enumerate(lengths) for _ in range(2)]
    placements += [("FST-M8x20", i * 10, 0, 0, "Fasteners") for i in range(screws)]
    placements += [("FST-M8x30", i * 10, 50, 0, "Fasteners") for i in range(screws // 2)]
    write_assembly(path, parts, placements)
