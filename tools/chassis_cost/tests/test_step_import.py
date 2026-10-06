"""STEP import: names, quantities through sub-assemblies, oriented lengths, profiles, speed.

Needs cadquery-ocp (tools/chassis_cost/.venv); skipped otherwise.
"""

import json
import os
import sys
import time

import pytest

pytest.importorskip("OCP")

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
import step_fixture  # noqa: E402
import step_import  # noqa: E402


@pytest.fixture(scope="module")
def base(tmp_path_factory):
    path = str(tmp_path_factory.mktemp("step") / "base.step")
    step_fixture.mecanum_base(path)
    js, _, stats = step_import.import_step(path)
    return json.loads(js), stats


def part(doc, name):
    return next(p for p in doc["parts"] if p["name"] == name)


def test_quantities_follow_the_assembly_tree(base):
    doc, stats = base
    assert part(doc, "EXT-4040-500")["quantity"] == 2
    assert part(doc, "BRK-corner-40")["quantity"] == 8
    assert part(doc, "FST-M8x16")["quantity"] == 48          # inside a sub-assembly
    assert stats.parts == 6 and stats.instances == 2 + 2 + 1 + 8 + 1 + 48


def test_40mm_single_and_double_profiles_and_lengths(base):
    doc, _ = base
    single = part(doc, "EXT-4040-500")["extrusion"]
    double = part(doc, "EXT-4080-450")["extrusion"]
    assert (single["profile"], single["length_mm"]) == ("4040", 500.0)
    assert (double["profile"], double["length_mm"]) == ("4080", 450.0)
    assert "model_length_mm" not in single and "model_length_mm" not in double


def test_unnamed_angled_rail_is_measured_by_its_oriented_box(base):
    doc, _ = base
    rail = part(doc, "Rail")
    assert rail["category"] == "extrusion"
    assert rail["extrusion"] == {"profile": "4040", "length_mm": 300.0, "source": "geometry"}


def test_categories_and_volume(base):
    doc, _ = base
    assert part(doc, "PLT-deck")["category"] == "plate"
    assert part(doc, "FST-M8x16")["category"] == "fastener"
    vol = float(part(doc, "PLT-deck")["attributes"]["volume_mm3"])
    assert abs(vol - 450 * 500 * 3) < 1.0


def test_import_speed_for_a_few_hundred_pieces(tmp_path):
    path = str(tmp_path / "big.step")
    step_fixture.mecanum_base(path, screws=400)
    t0 = time.perf_counter()
    _, _, stats = step_import.import_step(path)
    total = time.perf_counter() - t0
    print(f"\n{stats.instances} pieces, {os.path.getsize(path) / 1e6:.1f} MB: read {stats.read_s:.2f} s, "
          f"measure {stats.measure_s:.2f} s, total {total:.2f} s")
    assert stats.instances > 400 and total < 30.0


def test_slotted_profiles_and_threaded_bolts(tmp_path):
    path = str(tmp_path / "real.step")
    step_fixture.realistic_base(path)
    t0 = time.perf_counter()
    js, _, stats = step_import.import_step(path)
    total = time.perf_counter() - t0
    doc = json.loads(js)
    rails = [p for p in doc["parts"] if p["category"] == "extrusion"]
    assert len(rails) == 8 and all(p["extrusion"]["profile"] == "4040" for p in rails)
    assert all("model_length_mm" not in p["extrusion"] for p in rails)     # slots do not change length
    print(f"\nslotted 4040 x 8 lengths + grooved bolts: {stats.instances} pieces, "
          f"{os.path.getsize(path) / 1e6:.1f} MB: read {stats.read_s:.2f} s, "
          f"measure {stats.measure_s:.2f} s, total {total:.2f} s")
    assert total < 60.0


def test_bom_tool_reads_step_directly(tmp_path):
    import subprocess
    path = str(tmp_path / "base.step")
    step_fixture.mecanum_base(path)
    tool = os.path.join(os.path.dirname(HERE), "bom_tool.py")
    out = subprocess.run([sys.executable, tool, "cutlist", path], capture_output=True, text=True,
                         check=True).stdout
    assert "4040" in out and "4080" in out and "500.0 mm" in out
    subprocess.run([sys.executable, tool, "import", path], capture_output=True, text=True, check=True)
    assert os.path.exists(str(tmp_path / "base_bom.json")) and os.path.exists(str(tmp_path / "base_bom.csv"))
