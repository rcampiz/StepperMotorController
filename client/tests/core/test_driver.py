"""Driver boundary step 4 on the host: DRV:CAPS?, the physical settings, the protocol 2
fallback (smc_core.driver), against the fake board in both protocols."""

import pytest

from smc_core import commands as C
from smc_core import driver as D
from smc_core import views as V
from smc_core.board import Board, BoardConfig, Liveness
from smc_core.codec import Response, parse_response
from smc_core.sim import FakeBoard
from smc_core.transport.memory import MemoryTransport


def _open(protocol):
    t = MemoryTransport()
    fake = FakeBoard(t, protocol=protocol)
    live = Liveness(max_age_s=5.0)
    live.touch()
    b = Board(t, BoardConfig(heartbeat=False, telemetry_period_ms=0, reply_timeout_ms=500),
              liveness=live, name="p%d" % protocol)
    errors = []
    b.subscribe("error", lambda *a: errors.append(a))
    assert b.open(wait=True, timeout=3.0)
    b._fake, b._errors = fake, errors
    return b


@pytest.fixture(params=[3, 2])
def board(request):
    b = _open(request.param)
    yield b
    b.close()
    b._fake.stop()


def _call(b):
    return lambda req: b.request(req, timeout=2.0)


def test_the_client_takes_protocol_2_and_3_and_refuses_another():
    for proto in (2, 3):
        b = _open(proto)
        try:
            assert b.protocol == proto and not b._errors
        finally:
            b.close(); b._fake.stop()
    b = _open(4)
    try:
        assert any("protocol 4" in str(e) for e in b._errors)
    finally:
        b.close(); b._fake.stop()


def test_caps_come_from_the_board_or_the_powerstep01_assumption(board):
    drv = D.DriverAccess(_call(board), board.protocol)
    caps = drv.caps()
    assert caps["max_microsteps_current"] == 16 and caps["max_microsteps_voltage"] == 128
    assert caps["current_step_a"] == 0.078 and caps["overcurrent_sense"] == "voltage"
    assert caps["faults_alarmable"] == list(D.FAULT_NAMES)
    assert drv.physical == (board.protocol == 3)
    assert caps.get("protocol_2", False) == (board.protocol == 2)


def test_settings_read_in_physical_units_on_either_protocol(board):
    s = D.DriverAccess(_call(board), board.protocol).settings()
    assert (s["hold_a"], s["run_a"], s["acc_a"], s["dec_a"]) == (0.312, 1.326, 1.56, 1.56)
    assert s["drive_mode"] == "current" and s["microsteps_asked"] == 8 and s["microsteps"] == 8
    assert abs(s["accel_fsps2"] - 7276.0) < 0.01 and abs(s["max_speed_fsps"] - 12207.03) < 0.05
    assert abs(s["overcurrent_a"] - 234.375) < 1e-3 and s["overcurrent_v"] == 0.28125
    assert "stall" not in s["alarms"] and "overcurrent" in s["alarms"]


def test_changes_land_on_the_drivers_grid_through_either_protocol(board):
    drv = D.DriverAccess(_call(board), board.protocol)
    reqs = drv.requests({"current_a": (0.3, 1.3, 1.6, 1.6), "microsteps": 128,
                         "accel_fsps2": 5005.9, "max_speed_fsps": 1327.6, "overcurrent_a": 573,
                         "alarms": ["overcurrent", "stall"]})
    names = [r.name for r in reqs]
    if board.protocol == 3:
        assert names == ["DRV:CFG:CURRENT", "DRV:CFG:USTEP", "DRV:CFG:ACCEL", "DRV:CFG:MAXSPD",
                         "DRV:CFG:OVERCURRENT", "DRV:CFG:ALARMS"]
    else:
        assert names == ["DRV:CFG:TVAL", "DRV:CFG:STEPMODE", "DRV:CFG:MOTION", "DRV:CFG:OCD",
                         "DRV:CFG:FAULT"]
    for r in reqs:
        assert board.request(r, timeout=2.0).success, r.line
    s = drv.settings()
    assert (s["hold_a"], s["run_a"], s["acc_a"]) == (0.312, 1.326, 1.638)
    assert s["microsteps_asked"] == 128 and s["microsteps"] == 16        # current mode: 1/16
    assert abs(s["accel_fsps2"] - 5005.888) < 0.01                       # ACC 344
    assert abs(s["max_speed_fsps"] - 1327.515) < 0.01                    # MAX_SPEED 87
    assert abs(s["overcurrent_v"] - 0.6875) < 1e-6                       # OCD_TH 21
    assert s["alarms"] == ["overcurrent", "stall"]
    r = drv.resolution()                                                 # (the fake has no APPLY step)
    assert r["microsteps"] == 16 and r["step_mode"] == 4
    assert drv.configured_microsteps() == 128


def test_physical_setters_reply_the_rounded_values_and_the_query():
    b = _open(3)
    try:
        got = V.driver_setting(b.request(C.cfg_current(0.3, 1.3, 1.6, 1.6), timeout=2.0))
        assert got == {"hold_a": 0.312, "run_a": 1.326, "acc_a": 1.638, "dec_a": 1.638, "drive_mode": "current"}
        assert V.driver_setting(b.request(C.cfg_query("CURRENT"), timeout=2.0)) == got
        got = V.driver_setting(b.request(C.cfg_overcurrent(573), timeout=2.0))
        assert got == {"overcurrent_a": 572.917, "overcurrent_v": 0.6875}
        bad = b.request(C.cfg_microsteps(256), timeout=2.0)
        assert not bad.success and bad.error_code == "OUT_OF_RANGE"
        r = V.resolution(b.request(C.microsteps(64), timeout=2.0))
        assert r["microsteps"] == 16 and r["step_mode"] == 4
        assert V.resolution(b.request(C.microsteps_query(), timeout=2.0))["microsteps"] == 16
    finally:
        b.close(); b._fake.stop()


def test_a_protocol_2_board_refuses_the_physical_commands():
    b = _open(2)
    try:
        r = b.request(C.driver_caps(), timeout=2.0)
        assert not r.success and r.error_code == "UNKNOWN_CMD"
        assert V.driver_caps(r) == {}
        assert V.resolution(b.request(C.step_mode_query(), timeout=2.0))["microsteps"] == 8
    finally:
        b.close(); b._fake.stop()


def test_views_read_the_ascii_forms_too():
    lines = ["max_current_a=9.906", "voltage_mode=1", "faults_reported=overcurrent,stall",
             "faults_alarmable=NONE", "overcurrent_sense=voltage", "max_microsteps_current=16"]
    caps = V.driver_caps(Response(True, "DRV:CAPS?", {"lines": lines, "count": len(lines)}))
    assert caps == {"max_current_a": 9.906, "voltage_mode": True, "faults_reported": ["overcurrent", "stall"],
                    "faults_alarmable": [], "overcurrent_sense": "voltage", "max_microsteps_current": 16}
    one = parse_response("OK CURRENT hold_a=0.312 run_a=1.326 acc_a=1.56 dec_a=1.56 drive_mode=current "
                         "(not saved; DRV:CFG:APPLY puts it in force)", "DRV:CFG:CURRENT")
    assert V.driver_setting(one) == {"hold_a": 0.312, "run_a": 1.326, "acc_a": 1.56, "dec_a": 1.56,
                                     "drive_mode": "current"}
    p2 = parse_response("OK STEP_MODE=7 MICROSTEP_RATIO=1/128 MICROSTEPS_PER_REV=25600", "DRV:STEP_MODE?")
    assert V.resolution(p2) == {"microsteps": 128, "step_mode": 7, "microsteps_per_rev": 25600}


def test_the_bench_boards_v4_codes_translate_like_the_firmware():
    # TVAL 04/11/14/14, current mode, step mode 7, ACC/DEC 344, MAX_SPEED 87, OCD_TH 21
    cfg = {"drive_mode": "current", "tval": (0x04, 0x11, 0x14, 0x14), "kval": (0x29,) * 4, "step_mode": 7,
           "acc_raw": 344, "dec_raw": 344, "maxspd_raw": 87, "ocd_th": 21, "stall_th": 0,
           "faults": {"OCD": True, "TH_SD": True, "TH_WARN": True, "UVLO": True, "STALL_A": False,
                      "STALL_B": False, "CMD_ERR": False}, "action": "HardHiZ", "idle_release_ms": 3000}
    s = D.settings_from_legacy(cfg)
    assert (s["hold_a"], s["run_a"], s["acc_a"], s["dec_a"]) == (0.312, 1.326, 1.56, 1.56)
    assert s["microsteps_asked"] == 128 and s["microsteps"] == 16
    assert s["accel_fsps2"] == 5005.888 and s["max_speed_fsps"] == 1327.5146484375
    assert s["overcurrent_v"] == 0.6875 and s["overcurrent_a"] == 572.917
    assert s["alarms"] == ["overcurrent", "undervoltage", "thermal_shutdown", "thermal_warning"]
    # and back: the same codes
    lines = [r.line for r in D.settings_requests(
        {"current_a": (s["hold_a"], s["run_a"], s["acc_a"], s["dec_a"]), "microsteps": 128,
         "accel_fsps2": s["accel_fsps2"], "decel_fsps2": s["decel_fsps2"],
         "max_speed_fsps": s["max_speed_fsps"], "overcurrent_a": s["overcurrent_a"]}, physical=False)]
    assert lines == ["DRV:CFG:TVAL 04 11 14 14", "DRV:CFG:STEPMODE 7", "DRV:CFG:MOTION 344 344 87",
                     "DRV:CFG:OCD 21"]


def test_telemetry_faults_in_chip_neutral_names():
    assert D.fault_names(0x11) == ["overcurrent", "stall"]
    assert D.status_faults({"motor": {"faults": 4}}) == ["thermal_shutdown"]
    assert D.status_faults({"motor": {"ocd": True, "stall_b": True}}) == ["overcurrent", "stall"]
    assert D.microsteps_in_force(D.POWERSTEP01_CAPS, 128, "current") == 16
    assert D.microsteps_in_force(D.POWERSTEP01_CAPS, 128, "voltage") == 128
    assert D.quantize(1.3, 0.078) == pytest.approx(1.326)
