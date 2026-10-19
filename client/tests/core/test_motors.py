"""Motor profiles: the files ship with the client, apply as DRV:CFG commands, export from a
board's config and round-trip through the user's folder."""
import os

import pytest

from smc_core import motors
from smc_core import views as V
from smc_core.board import Board, BoardConfig, Liveness
from smc_core.sim import FakeBoard
from smc_core.transport.memory import MemoryTransport


def test_the_project_motor_ships_as_a_profile():
    names = [p.name for p in motors.list_profiles()]
    assert "23HS22-2804-ME1K" in names
    p = motors.load_profile("23HS22-2804-ME1K")
    assert p.rated_current_a == 2.8 and p.encoder_cpr == 4000 and p.drive_mode == "current"
    assert p.resonance_bands_rpm == [(55.0, 66.0), (105.0, 122.0)]
    assert "NEMA 23" in p.label


def test_profile_requests_are_the_physical_drive_config_commands_in_order():
    p = motors.load_profile("23HS22-2804-ME1K")
    lines = [r.line for r in p.requests()]
    assert lines[0] == "DRV:CFG:DRIVE CURRENT"
    assert lines[1] == "DRV:CFG:CURRENT 0.31 1.33 1.56 1.56"   # amps, as the file stores them
    assert "DRV:CFG:USTEP 8" in lines
    assert "DRV:CFG:PRED ON 48" in lines and "DRV:CFG:IDLE 3000" in lines
    assert lines[-1] == "DRV:CFG:APPLY"


def test_a_protocol_2_board_gets_the_chip_codes():
    p = motors.load_profile("23HS22-2804-ME1K")
    lines = [r.line for r in p.requests(physical=False)]
    assert lines[:3] == ["DRV:CFG:DRIVE CURRENT", "DRV:CFG:TVAL 04 11 14 14", "DRV:CFG:STEPMODE 3"]
    assert lines[-1] == "DRV:CFG:APPLY"
    old = motors._from_dict("old", {"drive_mode": "current", "tval_a": {"hold": 0.31, "run": 1.33},
                                    "step_mode": 4, "ocd_th": 21})
    assert [r.line for r in old.requests()][1:4] == ["DRV:CFG:CURRENT 0.31 1.33 1.33 1.33",
                                                     "DRV:CFG:USTEP 16", "DRV:CFG:OCD 21"]
    with pytest.raises(KeyError):
        motors.load_profile("no-such-motor")


@pytest.fixture
def board():
    t = MemoryTransport()
    fake = FakeBoard(t)
    live = Liveness(max_age_s=0.5)
    b = Board(t, BoardConfig(heartbeat_period_s=0.02, heartbeat_timeout_ms=1000,
                             telemetry_period_ms=100, reply_timeout_ms=500),
              liveness=live, name="sim")
    live.touch()
    assert b.open(wait=True, timeout=3.0)
    b._fake = fake
    yield b
    b.close()
    fake.stop()


def test_applying_a_profile_configures_the_fake_board(board):
    p = motors.load_profile("generic-nema17-1.7A")
    for req in p.requests():
        resp = board.request(req, timeout=2.0)
        assert resp.success, (req.line, resp.error_message)
    fake = board._fake
    assert fake.drive_current and fake.step_mode == 4
    cfg = V.motor_config(board.request(motors.C.driver_config(), timeout=2.0))
    assert cfg["tval"][1] == 15                               # 1.17 A -> 0x0F


def test_export_from_the_physical_view(board, tmp_path):
    phys = V.driver_physical(board.request(motors.C.driver_physical(), timeout=2.0))
    p = motors.export_profile(phys, "phys", rated_current_a=2.8, regulation={"pred": True, "tsw_us": 48})
    assert p.tval_a == {"hold": 0.312, "run": 1.326, "acc": 1.56, "dec": 1.56}
    assert p.microsteps == 8 and p.overcurrent_a == 234.375 and p.pred is True
    lines = [r.line for r in p.requests()]
    assert "DRV:CFG:CURRENT 0.312 1.326 1.56 1.56" in lines and "DRV:CFG:OVERCURRENT 234.375" in lines
    for req in p.requests():
        assert board.request(req, timeout=2.0).success, req.line


def test_export_round_trips_through_the_user_folder(board, tmp_path):
    cfg = V.motor_config(board.request(motors.C.driver_config(), timeout=2.0))
    p = motors.export_profile(cfg, "bench-tuned", title="Bench tuned", rated_current_a=2.8,
                              resonance_bands_rpm=[(55, 66)])
    path = motors.save_profile(p, directory=str(tmp_path))
    assert os.path.exists(path)
    saved = motors._from_dict("bench-tuned", __import__("json").load(open(path, encoding="utf-8")), path)
    assert saved.drive_mode == p.drive_mode and saved.tval_a == p.tval_a
    assert saved.resonance_bands_rpm == [(55.0, 66.0)]
    assert [r.line for r in saved.requests()][-1] == "DRV:CFG:APPLY"
