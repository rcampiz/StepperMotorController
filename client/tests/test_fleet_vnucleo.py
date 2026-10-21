"""Fleet on four virtual Nucleos (FL, FR, RL, RR): identity, kinematics on the real
firmware code, base ESTOP, one-board-lost policy, synchronised start, merged telemetry.

The synchronised-start test checks that both start paths start every wheel. It does not
measure the hardware's start skew: on Windows every board process wakes on the same OS
timer tick (15.6 ms), which aligns the boards artificially (docs/MULTI_BOARD.md section 2
gives the estimate for real boards). Skipped when sim/ has not been built."""

import time

import pytest

from smc_core import commands as C
from smc_core import kinematics as K
from smc_core.board import BoardConfig
from smc_core.fleet import Fleet, FleetConfig, FleetError, ROLES

from . import vnucleo

SERIALS = {"FL": "VN000001", "FR": "VN000002", "RL": "VN000003", "RR": "VN000004"}


def make_fleet(procs, serials=SERIALS, **board):
    cfg = FleetConfig(addresses={r: procs[i][0] for i, r in enumerate(ROLES)}, serials=serials,
                      board=BoardConfig(**board) if board else BoardConfig())
    f = Fleet(cfg)
    f.open(timeout=8.0)
    return f


def pump(fleet, cond, timeout=5.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        fleet.liveness.touch()
        if cond():
            return True
        time.sleep(0.01)
    return False


@pytest.fixture
def four(tmp_path):
    with vnucleo.boards(4, log_dir=str(tmp_path)) as procs:
        yield procs, str(tmp_path)


def test_roles_are_bound_to_serials_and_a_swap_is_caught(four):
    procs, _ = four
    f = make_fleet(procs)
    assert {r: b.identity.serial for r, b in f.boards.items()} == SERIALS
    f.close(estop=False)
    swapped = dict(SERIALS, FL="VN000002", FR="VN000001")
    with pytest.raises(FleetError, match="wrong corner"):
        make_fleet(procs, serials=swapped)


def test_a_body_twist_drives_the_four_wheels_and_reads_back(four):
    procs, _ = four
    f = make_fleet(procs)
    try:
        f._all(C.fault_clear())
        cmd = K.Twist(vx=0.20, vy=0.10, wz=0.30)
        assert f.drive(cmd) == 1.0
        assert pump(f, lambda: (lambda t: t is not None and abs(t.vx - cmd.vx) < 0.01
                                and abs(t.vy - cmd.vy) < 0.01 and abs(t.wz - cmd.wz) < 0.05)
                    (f.measured_twist()), timeout=5.0), f.measured_twist()
        f.stop()
        assert pump(f, lambda: all(d.get("motor", {}).get("speed", 1) == 0
                                   for _, d in f.frame().boards.values()), timeout=5.0)
    finally:
        f.close()


def test_braked_wheels_slow_with_the_brake_and_hold_at_full(four):
    """A brake a side (the pad's triggers): half a brake runs those wheels at half speed, a
    full one stops and holds them, the others keep the twist throughout; letting go runs
    them again."""
    procs, _ = four
    f = make_fleet(procs)
    speed = lambda r: abs(f.frame().boards.get(r, (0, {}))[1].get("motor", {}).get("speed", 0))
    try:
        f._all(C.fault_clear())
        f.drive(K.Twist(vx=0.2), brake={"FL": 0.5, "RL": 0.5})
        assert pump(f, lambda: speed("FR") > 0 and 0.4 < speed("FL") / speed("FR") < 0.6, timeout=5.0), \
            (speed("FL"), speed("FR"))
        f.drive(K.Twist(vx=0.2), brake={"FL": 1.0, "RL": 1.0})
        assert f._sent["FL"] == C.mot_stop().line and f._sent["RL"] == C.mot_stop().line
        assert pump(f, lambda: speed("FL") == 0 and speed("RL") == 0, timeout=5.0)
        assert speed("FR") > 0 and speed("RR") > 0
        f.drive(K.Twist(vx=0.2))                                # let go
        assert pump(f, lambda: speed("FL") > 0 and speed("RL") > 0, timeout=5.0)
    finally:
        f.close()


def test_base_estop_writes_every_board_before_confirming(four):
    procs, _ = four
    f = make_fleet(procs)
    try:
        f._all(C.fault_clear())
        f.drive(K.Twist(vx=0.2))
        pump(f, lambda: False, timeout=0.3)
        r = f.estop()
        assert r.confirmed, r
        assert r.write_skew_ms < 50.0
        print(f"\nbase ESTOP: write skew across 4 boards {r.write_skew_ms:.2f} ms; "
              f"confirmed in {[round((x.t_confirmed - x.t_write) * 1000) for x in r.results.values()]} ms")
    finally:
        f.close()


def test_one_board_lost_stops_the_other_three(four):
    procs, _ = four
    f = make_fleet(procs)
    faults = []
    f.subscribe("fault", lambda role, why: faults.append((role, why)))
    try:
        f._all(C.fault_clear())
        f.drive(K.Twist(vx=0.2))
        pump(f, lambda: False, timeout=0.3)
        procs[1][1].kill()                                     # FR's USB is gone
        assert pump(f, lambda: faults, timeout=3.0)
        assert faults[0][0] == "FR"
        others = [r for r in ROLES if r != "FR"]
        assert pump(f, lambda: all(f.boards[r].last_status.get("state") == "ESTOP" for r in others),
                    timeout=3.0), {r: f.boards[r].last_status.get("state") for r in others}
    finally:
        f.close(estop=False)


@pytest.mark.parametrize("timed", [False, True])
def test_synchronised_start_starts_every_wheel(four, timed):
    procs, log_dir = four
    f = make_fleet(procs)
    try:
        f._all(C.fault_clear())
        runs = {r: C.queue_run(200, 1) for r in ROLES}
        before = {r: len(vnucleo.motion_log(log_dir, SERIALS[r])) for r in ROLES}
        res = f.start_together(runs, delay_ms=150, timed=timed)
        assert all(x.success for x in res.values()), res
        assert pump(f, lambda: all(len(vnucleo.motion_log(log_dir, SERIALS[r])) > before[r]
                                   for r in ROLES), timeout=3.0)
        starts = {}
        for r in ROLES:
            rows = vnucleo.motion_log(log_dir, SERIALS[r])[before[r]:]
            starts[r] = next(t for t, ev, _ in rows if ev == "run")
        skew_ms = (max(starts.values()) - min(starts.values())) / 1e6
        print(f"\n{'SYNC:START:AT' if timed else 'SYNC:START, 4 ports in turn'}: all four "
              f"wheels started; sim skew {skew_ms:.2f} ms (not representative, see the docstring)")
        assert skew_ms < 100.0
        f.stop()
    finally:
        f.close()


def test_merged_telemetry_frame(four):
    procs, _ = four
    f = make_fleet(procs)
    try:
        assert pump(f, lambda: len(f.frame().boards) == 4, timeout=3.0)
        fr = f.frame()
        assert fr.max_age_s < 1.0 and fr.spread_s < 1.0
        assert set(fr.boards) == set(ROLES)
    finally:
        f.close()


def test_a_board_that_reboots_stops_the_base():
    """A wheel controller that resets has stopped and lost its setup: the base stops."""
    import subprocess
    with vnucleo.boards(4) as procs:
        f = make_fleet(procs)
        faults = []
        f.subscribe("fault", lambda role, why: faults.append((role, why)))
        try:
            f._all(C.fault_clear())
            f.drive(K.Twist(vx=0.2))
            pump(f, lambda: False, timeout=0.3)
            url, proc = procs[2]                              # RL reboots on the same port
            port = url.rsplit(":", 1)[1]
            proc.kill()
            proc.wait(5)
            procs[2] = (url, subprocess.Popen([vnucleo.EXE, "--port", port, "--serial", "VN000003"],
                                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
            assert pump(f, lambda: faults, timeout=3.0)
            assert faults[0][0] == "RL"
            others = [r for r in ROLES if r != "RL"]
            assert pump(f, lambda: all(f.boards[r].last_status.get("state") == "ESTOP" for r in others),
                        timeout=3.0)
        finally:
            f.close(estop=False)


def test_driving_shows_the_base_motion_on_every_lcd_and_close_gives_them_back(four):
    """The arrow (linear) and the ring (turn) on every board's LCD while the base drives: the
    first move takes each LCD (UI:MODE REMOTE, then the indicator, in order), later moves are
    accepted; closing hands the LCDs back to their menus."""
    procs, _ = four
    f = make_fleet(procs)
    try:
        f.drive(K.Twist(vx=0.25, wz=-1.0))                     # ahead at half, turning clockwise
        assert pump(f, lambda: f._lcd_taken == set(ROLES))
        for b in f.boards.values():
            assert pump(f, lambda b=b: "REMOTE" in (b.request(C.ui_mode_query(), timeout=2.0).raw or ""))
        # a later move goes latest-wins and the board takes it (REMOTE, so no refusal)
        f.drive(K.Twist(vy=0.25))
        r = f.boards["FL"].request(C.disp_indicator(*K.indicator(K.Twist(vy=0.25), f.lcd_full_scale)), timeout=2.0)
        assert r.success, r.raw
        f.stop()
        f.release_lcds()
        assert f._lcd_taken == set()
        assert "LOCAL" in (f.boards["RR"].request(C.ui_mode_query(), timeout=2.0).raw or "")
    finally:
        f.close(estop=False)


def test_the_clear_hold_reaches_every_boards_estop_page(four):
    procs, _ = four
    f = make_fleet(procs)
    try:
        f.estop()
        f.show_clear_hold(40)
        for b in f.boards.values():                          # every board takes it (no refusal)
            r = b.request(C.estop_hold(40), timeout=2.0)
            assert r.success, r.raw
        f.show_clear_hold(0)
        f.rearm()
    finally:
        f.close(estop=False)
