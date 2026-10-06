"""Scenarios through the runner against the fake board (client refactor step 5)."""

import pytest

from smc_core import runner
from smc_core.board import Board, BoardConfig, Liveness
from smc_core.sim import FakeBoard
from smc_core.transport.memory import MemoryTransport


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
    b._live = live
    yield b
    b.close()
    fake.stop()


def run(board, name, **params):
    return runner.run(name, board, params, timeout=30, while_waiting=board._live.touch)


def test_registry_lists_the_builtin_scenarios():
    names = [s.name for s in runner.list_scenarios()]
    assert names == ["bringup", "estop_latency", "heartbeat_loss", "step_mode_readback", "sysid"]


def test_bringup_passes_and_measures_identity(board):
    r = run(board, "bringup")
    assert r.ok, r.summary
    assert r.measurements["serial"] == "SIM00001" and r.measurements["step_mode"] == 3
    assert "*IDN?" in r.tx and not any(l.startswith("SYST:HB ") for l in r.tx)


def test_estop_latency_confirms_and_refuses_motion(board):
    r = run(board, "estop_latency", rpm=20.0, run_s=0.1)
    assert r.ok, r.summary
    assert r.measurements["confirm_ms"] >= 0
    assert board._fake.state == "IDLE"                       # cleared at the end


def test_heartbeat_loss_shows_the_watchdog_working(board):
    r = run(board, "heartbeat_loss", timeout_ms=200, margin_ms=200)
    assert r.ok, r.summary
    assert board._fake.hb_timeout_ms == 1000                 # restored


def test_step_mode_readback_restores_the_original(board):
    r = run(board, "step_mode_readback", modes=[0, 5, 7])
    assert r.ok, r.summary
    assert board._fake.step_mode == 3


def test_sysid_downloads_every_sample(board):
    r = run(board, "sysid", type="step", speed=800, duration_ms=1000, settle_ms=100, chunk=40)
    assert r.ok, r.summary
    assert r.measurements["total"] == 95 and len(r.measurements["samples"]) == 95
    assert tuple(r.measurements["samples"][0]) == (0, 800, 760, 10)


def test_unknown_parameter_is_refused(board):
    with pytest.raises(KeyError):
        runner.submit("bringup", board, {"nope": 1})


def test_cancel_job_stops_a_running_scenario(board):
    board._fake.sysid_run_s = 5.0
    ticket = runner.submit("sysid", board, {"timeout_s": 10})
    import time
    time.sleep(0.3)
    board.cancel_job()
    r = ticket.wait(5.0)
    assert not r.ok and r.error.startswith("cancelled")


def test_results_serialise_to_one_json_line(board, tmp_path):
    r = run(board, "bringup")
    path = tmp_path / "out.jsonl"
    runner.write_jsonl(str(path), [r, r])
    rows = runner.read_jsonl(str(path))
    assert len(rows) == 2 and rows[0]["name"] == "bringup" and rows[0]["ok"] is True
