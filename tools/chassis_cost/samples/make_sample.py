#!/usr/bin/env python3
"""
Regenerate samples/sample_bom.json and .csv: a SYNTHETIC mecanum base, NOT your model.

It shows the export format and exercises every rule (named and unnamed extrusions, a
name/model length mismatch, a part without a part number, copies named "Bolt (1)").
Part numbers are invented placeholders shaped like McMaster-Carr's (digits, letter,
digits); they are not real catalog numbers.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "fusion", "SMC_BOM_Export"))
import bom_core  # noqa: E402
from bom_core import RawItem  # noqa: E402

ITEMS = [
    # extrusions named by the convention (profile 1010 = 1" x 1", lengths in mm)
    RawItem("c1", "EXT-1010-500", "00000T001", "T-slot rail, 1in, 4 slots", "Aluminum 6105-T5",
            0.34, (25.4, 25.4, 500.0), True, 2, ["Base:1/Frame:1/EXT-1010-500:1",
                                                  "Base:1/Frame:1/EXT-1010-500:2"]),
    RawItem("c2", "EXT-1010-350", "00000T001", "T-slot rail, 1in, 4 slots", "Aluminum 6105-T5",
            0.24, (25.4, 25.4, 350.0), True, 3, ["Base:1/Frame:1/EXT-1010-350:%d" % i for i in (1, 2, 3)]),
    # an extrusion only recognisable by its geometry, and one whose name and model disagree
    RawItem("c3", "Rail", "00000T001", "", "Aluminum 6105-T5", 0.10, (147.0, 25.4, 25.4), True, 2,
            ["Base:1/Frame:1/Rail:1", "Base:1/Frame:1/Rail:2"]),
    RawItem("c4", "EXT-1010-120", "00000T001", "", "Aluminum 6105-T5", 0.09, (25.4, 25.4, 125.0),
            True, 4, ["Base:1/Uprights:1/EXT-1010-120:%d" % i for i in (1, 2, 3, 4)]),
    RawItem("c5", "BRK-corner-1in", "00000T010", "Corner bracket, 1in series", "Aluminum",
            0.03, (25.4, 25.4, 6.0), True, 16, ["Base:1/Frame:1/BRK-corner-1in:%d" % i for i in range(16)]),
    RawItem("c6", "FST-tnut-1/4-20", "00000A020", "Drop-in T-nut 1/4-20", "Zinc-plated steel",
            0.004, (6.0, 10.0, 15.0), True, 48, ["Base:1/Frame:1/tnut:%d" % i for i in range(48)]),
    RawItem("c7", "Bolt (1)", "00000A030", "Button head 1/4-20 x 1/2", "Steel",
            0.006, (6.35, 11.0, 12.7), True, 40, ["Base:1/Frame:1/Bolt (1):%d" % i for i in range(40)]),
    RawItem("c8", "Bolt (2)", "00000A030", "Button head 1/4-20 x 1/2", "Steel",
            0.006, (6.35, 11.0, 12.7), True, 8, ["Base:1/Mounts:1/Bolt (2):%d" % i for i in range(8)]),
    RawItem("c9", "PLT-deck", "", "Top deck, 3 mm", "Aluminum 5052", 0.9, (3.0, 330.0, 480.0), True, 1,
            ["Base:1/PLT-deck:1"]),
    RawItem("c10", "DRV-motor-mount-NEMA17", "00000K040", "NEMA 17 L-mount", "Steel", 0.07,
            (3.0, 42.0, 50.0), True, 4, ["Base:1/Mounts:1/DRV-motor-mount-NEMA17:%d" % i for i in range(4)]),
    RawItem("c11", "DRV-mecanum-wheel-97mm", "00000K050", "97 mm mecanum wheel", "", 0.32,
            (45.0, 97.0, 97.0), True, 4, ["Base:1/Wheels:1/DRV-mecanum-wheel-97mm:%d" % i for i in range(4)]),
    RawItem("c12", "ELE-nucleo-f401re", "", "NUCLEO-F401RE + IHM03A1", "", 0.05, (20.0, 70.0, 82.5),
            True, 4, ["Base:1/Electronics:1/ELE-nucleo-f401re:%d" % i for i in range(4)], hidden=1),
]


def main():
    records = bom_core.build_records(ITEMS)
    meta = {"design": "SAMPLE mecanum base (synthetic)", "document": "sample",
            "exported_at": "2026-10-06T00:00:00+00:00", "exporter": "make_sample.py",
            "fusion_version": "n/a"}
    with open(os.path.join(HERE, "sample_bom.json"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(bom_core.export_json(records, meta))
    with open(os.path.join(HERE, "sample_bom.csv"), "w", encoding="utf-8", newline="") as fh:
        fh.write(bom_core.export_csv(records))
    print(f"wrote sample_bom.json/.csv: {len(records)} records")


if __name__ == "__main__":
    main()
