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
    assert names == ["bringup", "drive_mode_chip", "estop_latency", "filter_bode", "heartbeat_loss",
                     "motion_scale", "position_capture", "resonance_map", "resonance_sweep",
                     "step_mode_chip", "step_mode_readback", "sysid", "telemetry_load"]


def test_bringup_passes_and_measures_identity(board):
    r = run(board, "bringup")
    assert r.ok, r.summary
    assert r.measurements["serial"] == "SIM00001" and r.measurements["microsteps"] == 8
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


def test_motion_scale_measures_position_and_encoder(board):
    r = run(board, "motion_scale", usteps=1600, speed=400, rev=0.5)
    assert r.ok, (r.summary, r.checks)
    assert r.measurements["microsteps_per_rev"] == 1600          # 200 x 1/8 on the fake
    assert r.measurements["encoder_counts_per_rev_dir1"] == 1600.0
    assert board._fake.hi_z                                      # output left disabled


def test_step_mode_chip_follows_config_in_hi_z_and_enabled(board):
    r = run(board, "step_mode_chip", mode=7)
    assert r.ok, (r.summary, r.checks)
    assert board._fake.step_mode == r.measurements["original"]


def test_drive_mode_chip_toggles_current_mode_and_restores_it(board):
    r = run(board, "drive_mode_chip")
    assert r.ok, (r.summary, r.checks)
    assert board._fake.drive_current is True and r.measurements["original"] == "current"


def test_resonance_sweep_measures_each_speed(board):
    r = run(board, "resonance_sweep", rpm_from=60, rpm_to=70, rpm_step=10, dwell_s=0.3, settle_s=0.05)
    assert r.ok, (r.summary, r.checks)
    assert set(r.measurements) >= {"rpm_60", "rpm_70", "worst_rpm", "worst_std"}
    assert r.measurements["rpm_60"]["samples"] >= 5


def test_resonance_map_sweeps_each_mode_and_restores_the_original(board):
    board._fake.step_mode = 7
    board._fake.drive_current = False          # voltage mode: 1/32 is real (current mode caps at 1/16)
    r = run(board, "resonance_map", modes=[3, 5], rpm_from=60, rpm_to=70, rpm_step=10,
            dwell_s=0.3, settle_s=0.05)
    assert r.ok, (r.summary, r.checks)
    assert r.measurements["mode_8"]["usteps_per_rev"] == 1600
    assert set(r.measurements["mode_32"]["std"]) == {"60", "70"}
    assert board._fake.step_mode == 7


def test_resonance_map_in_current_mode_maps_1_16_once_for_the_finer_modes(board):
    board._fake.step_mode = 7                  # the fake starts in current mode
    r = run(board, "resonance_map", modes=[3, 5, 7], rpm_from=60, rpm_to=60, rpm_step=10,
            dwell_s=0.2, settle_s=0.05)
    assert r.ok, (r.summary, r.checks)
    assert sorted(k for k in r.measurements if k.startswith("mode_")) == ["mode_16", "mode_8"]
    assert board._fake.step_mode == 7          # the configured 1/128 restored


def test_position_capture_reads_the_rings_and_finds_the_oscillation(board):
    r = run(board, "position_capture", rpm=60, rate_hz=2000, settle_s=0.1)
    assert r.ok, (r.summary, r.checks)
    cap = r.measurements["capture"]
    assert cap["n"] == 4096 and cap["rate_hz"] == 2000 and cap["sync"]
    assert abs(cap["peak_hz"] - 120.0) <= 3.0                     # the fake's 120 Hz wobble
    assert 2.5 <= cap["peak_amp"] <= 4.5 and cap["err_std"] < 4.0
    assert board._fake.enc_rate == 1000 and not board._fake._cap_frozen   # restored, resumed


def test_filter_bode_measures_lag_and_recommends_the_least_lagging_quiet_chain(board):
    fake = board._fake
    r = run(board, "filter_bode", freqs_hz=[0.5, 1.0, 2.0], cycles=2, mode="search", apply=False)
    assert r.ok, (r.summary, r.checks, r.error)
    bode = r.measurements["bode"]
    conf = bode["configured"]
    assert 60 <= conf["lag_at_target_ms"] <= 95                 # the fake's chain: 20+25+15+10+16 ms
    assert bode["no filters"]["lag_at_target_ms"] < conf["lag_at_target_ms"]
    assert all(0.85 <= g <= 1.05 for g in conf["gain"])
    # "no filters" lags least but is noisier; the quiet constraint picks the biquad 20 Hz chain
    assert r.measurements["recommended"] in ("biquad 20 Hz", "no filters")
    assert fake.enc_filter["ema_enabled"] and fake.enc_filter["biquad_cutoff_hz"] == 10   # restored


def test_telemetry_load_sees_the_encoder_advance(board):
    r = run(board, "telemetry_load", period_ms=20, usteps=800, speed=400)
    assert r.ok, (r.summary, r.checks)
    assert board._fake.tlm_ms == 100                             # restored to the board config


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
