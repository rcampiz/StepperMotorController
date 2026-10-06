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
