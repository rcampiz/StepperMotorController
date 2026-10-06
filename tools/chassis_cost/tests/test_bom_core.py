"""bom_core rules and the summary tool, on the synthetic sample (no Fusion needed)."""

import json
import os
import subprocess
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(TOOL, "fusion", "SMC_BOM_Export"))
sys.path.insert(0, os.path.join(TOOL, "samples"))
import bom_core  # noqa: E402
import make_sample  # noqa: E402


def records():
    return bom_core.build_records(make_sample.ITEMS)


def by_name(name):
    return next(r for r in records() if r.name == name)


@pytest.mark.parametrize("name,expect", [
    ("EXT-1010-500", {"profile": "1010", "length_mm": 500.0, "source": "name"}),
    ("EXT_2020_300mm", {"profile": "2020", "length_mm": 300.0, "source": "name"}),
    ("EXT-1515-18in", {"profile": "1515", "length_mm": 457.2, "source": "name"}),
])
def test_extrusion_from_name(name, expect):
    assert bom_core.detect_extrusion(name, (0, 0, 0)) == expect


def test_extrusion_from_geometry_needs_a_standard_section_and_a_long_part():
    assert bom_core.detect_extrusion("Rail", (25.4, 147.0, 25.4)) == \
        {"profile": "1010", "length_mm": 147.0, "source": "geometry"}
    assert bom_core.detect_extrusion("Rail", (25.4, 60.0, 25.4)) is None        # too short
    assert bom_core.detect_extrusion("Block", (25.0, 300.0, 33.0)) is None      # not a profile


def test_name_and_model_disagreeing_is_reported():
    r = by_name("EXT-1010-120")
    assert r.extrusion["model_length_mm"] == 125.0
    assert any("model is 125.0 mm" in w for w in r.warnings)


def test_categories_from_prefixes_and_keywords():
    assert by_name("BRK-corner-1in").category == "bracket"
    assert by_name("FST-tnut-1/4-20").category == "fastener"
    assert by_name("PLT-deck").category == "plate"
    assert by_name("Rail").category == "extrusion"
    assert bom_core.classify("Socket head screw M5", "", None) == "fastener"
    assert bom_core.classify("Mystery", "", None) == "other"


def test_part_number_is_the_key_and_copies_merge():
    bolts = [r for r in records() if r.vendor_part_number == "00000A030"]
    assert len(bolts) == 1 and bolts[0].quantity == 48 and bolts[0].vendor == "McMaster-Carr"
    rails = [r for r in records() if r.vendor_part_number == "00000T001"]
    assert sorted((r.extrusion["length_mm"], r.quantity) for r in rails) == \
        [(120.0, 4), (147.0, 2), (350.0, 3), (500.0, 2)]


def test_missing_part_number_and_hidden_instances_warn():
    deck = by_name("PLT-deck")
    assert deck.vendor_part_number == "" and any("no part number" in w for w in deck.warnings)
    nucleo = by_name("ELE-nucleo-f401re")
    assert any("hidden" in w for w in nucleo.warnings)


def test_json_round_trip_and_csv_columns():
    text = bom_core.export_json(records(), {"design": "t"})
    doc = bom_core.load_export(text)
    assert doc["schema"] == bom_core.SCHEMA and doc["units"]["length"] == "mm"
    header = bom_core.export_csv(records()).splitlines()[0].split(",")
    assert header == list(bom_core.CSV_FIELDS)
    with pytest.raises(ValueError):
        bom_core.load_export(json.dumps({"schema": "other"}))


def test_committed_sample_is_current():
    path = os.path.join(TOOL, "samples", "sample_bom.json")
    with open(path, encoding="utf-8") as fh:
        committed = json.load(fh)
    fresh = json.loads(bom_core.export_json(records(), {k: committed[k] for k in (
        "design", "document", "exported_at", "exporter", "fusion_version")}))
    assert committed == fresh, "run samples/make_sample.py"


def test_summary_and_cutlist_tool():
    path = os.path.join(TOOL, "samples", "sample_bom.json")
    out = subprocess.run([sys.executable, os.path.join(TOOL, "bom_tool.py"), "cutlist", path],
                         capture_output=True, text=True, check=True).stdout
    assert "1010  00000T001" in out and "2 x    500.0 mm" in out
    out = subprocess.run([sys.executable, os.path.join(TOOL, "bom_tool.py"), "summary", path],
                         capture_output=True, text=True, check=True).stdout
    assert "extrusion" in out and "2 part(s) have no part number" in out
