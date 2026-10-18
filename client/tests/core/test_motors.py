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


def test_profile_requests_are_the_drive_config_commands_in_order():
    p = motors.load_profile("23HS22-2804-ME1K")
    lines = [r.line for r in p.requests()]
    assert lines[0] == "DRV:CFG:DRIVE CURRENT"
    assert lines[1] == "DRV:CFG:TVAL 05 16 1A 1A"            # 0.39 / 1.72 / 2.03 / 2.03 A at 78 mA per unit
    assert "DRV:CFG:PRED ON 48" in lines
    assert "DRV:CFG:STEPMODE 3" in lines and "DRV:CFG:IDLE 3000" in lines
    assert lines[-1] == "DRV:CFG:APPLY"
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
