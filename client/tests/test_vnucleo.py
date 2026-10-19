"""The client core against the virtual Nucleo: the firmware's real L2 + L3 and task bodies.

Most tests run twice, once per motor driver board (vnucleo --driver): the powerSTEP01 and a
generic STEP/DIR chip (the firmware's real L4 StepDirDriver on fake pins). That is driver
boundary step 5's proof: the same L2/L3 and the same scenarios on two unrelated chips, the
differences decided by DRV:CAPS? alone (docs/design/DRIVER_BOUNDARY.md).

Skipped when sim/ has not been built (cmake --preset vnucleo; cmake --build --preset vnucleo).
"""

import time

import pytest

from smc_core import commands as C
from smc_core import runner, views
from smc_core.board import Board, BoardConfig, Liveness
from smc_core.transport import open_transport

from . import vnucleo


def connect(url, **cfg):
    live = Liveness(max_age_s=0.3)
    live.touch()
    b = Board(open_transport(url), BoardConfig(**cfg), liveness=live, name=url)
    assert b.open(wait=True, timeout=5.0)
    b._live = live
    return b


def run(b, name, **params):
    return runner.run(name, b, params, timeout=60, while_waiting=b._live.touch)


def wait(cond, timeout=3.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if cond():
            return True
        time.sleep(0.01)
    return False


DRIVERS = ("powerstep", "stepdir")


@pytest.fixture
def board():
    """A powerSTEP01 board (the tests of that chip's own numbers)."""
    with vnucleo.boards(1) as procs:
        b = connect(procs[0][0])
        yield b
        b.close()


@pytest.fixture(params=DRIVERS)
def any_board(request):
    """One board per motor driver: the tests that must not care which chip is below L3."""
    with vnucleo.boards(1, driver=request.param) as procs:
        b = connect(procs[0][0])
        b._driver = request.param
        yield b
        b.close()


def test_identity_and_protocol(board):
    assert board.identity.serial == "VN000001" and board.identity.proto == 3 and board.protocol == 3
    ver = board.request(C.version())
    assert ver.lines[0].startswith("Stepper Motor Controller v")


@pytest.mark.parametrize("name,params", [
    ("bringup", {}),
    ("step_mode_readback", {"modes": [0, 4, 7]}),            # 1/128: past a STEP/DIR chip's 1/32, refused
    ("estop_latency", {"rpm": 30.0, "run_s": 0.3}),
    ("heartbeat_loss", {"timeout_ms": 300, "margin_ms": 300}),
    ("sysid", {"type": "step", "speed": 400, "duration_ms": 600, "settle_ms": 150}),
    ("motion_scale", {"usteps": 25600, "speed": 400, "rev": 0.5}),      # 1/128 asked: 1/16 or 1/32 in force
    ("telemetry_load", {"period_ms": 50, "usteps": 25600, "speed": 200}),
    ("step_mode_chip", {"mode": 7}),          # APPLY routes the resolution through the Motor task when the bridges are on
    ("drive_mode_chip", {}),                  # powerSTEP01: CM_VM takes the same Hi-Z path; STEP/DIR: refused (one mode)
    ("position_capture", {"rpm": 60, "rate_hz": 2000, "settle_s": 0.6}),   # DMA rings + SYNC count (or the RUN speed)
])
def test_scenarios_pass_on_the_real_firmware_code(any_board, name, params):
    r = run(any_board, name, **params)
    assert r.ok, (any_board._driver, r.summary, r.checks, r.error)


def test_driver_caps_and_physical_settings_on_the_real_firmware_code(board):
    from smc_core import driver
    caps = views.driver_caps(board.request(C.driver_caps()))
    assert caps["max_microsteps_current"] == 16 and caps["max_microsteps_voltage"] == 128
    assert caps["max_current_a"] == 9.906 and caps["current_step_a"] == 0.078
    assert caps["overcurrent_sense"] == "voltage" and caps["faults_alarmable"] == list(driver.FAULT_NAMES)
    got = views.driver_setting(board.request(C.cfg_current(0.3, 1.3, 1.6, 1.6)))
    assert (got["hold_a"], got["run_a"], got["acc_a"], got["dec_a"]) == (0.312, 1.326, 1.638, 1.638)
    bad = board.request(C.cfg_current(0.3, 1.3, 10.5, 1.0))
    assert not bad.success and bad.error_code == "OUT_OF_RANGE" and "9.906 A" in bad.error_message
    # the protocol 2 alias lands in the same settings
    assert board.request(C.cfg_tval(0x04, 0x11, 0x14, 0x14)).success
    got = views.driver_setting(board.request(C.cfg_query("CURRENT")))
    assert (got["hold_a"], got["run_a"], got["acc_a"], got["dec_a"]) == (0.312, 1.326, 1.56, 1.56)
    assert views.driver_setting(board.request(C.cfg_accel(5005.9)))["accel_fsps2"] == 5005.89
    assert views.driver_setting(board.request(C.cfg_max_speed(1327.6)))["max_speed_fsps"] == 1327.51
    oc = views.driver_setting(board.request(C.cfg_overcurrent(573)))
    assert oc == {"overcurrent_a": 572.917, "overcurrent_v": 0.6875}
    assert views.motor_config(board.request(C.driver_config()))["ocd_th"] == 21
    us = views.driver_setting(board.request(C.cfg_microsteps(128)))
    drive = us["drive_mode"]
    assert us["microsteps_asked"] == 128 and us["microsteps"] == (16 if drive == "current" else 128)
    board.request(C.mot_disable())
    assert board.request(C.cfg_apply()).success
    assert views.resolution(board.request(C.microsteps_query()))["microsteps"] == us["microsteps"]
    phys = views.driver_physical(board.request(C.driver_physical()))
    assert phys["microsteps_asked"] == 128 and phys["run_a"] == 1.326 and "overcurrent_v" in phys
    r = views.resolution(board.request(C.microsteps(8)))
    assert r["microsteps"] == 8
    st = board.request(C.status()).data
    assert "faults" in st["motor"] and "driver_raw" in st["motor"] and "base_fsps" in st["control"]
    assert "status_reg" in st["motor"] and "base_spd" in st["control"]          # protocol 2 kept
    s = driver.DriverAccess(board.request, board.protocol).settings()
    assert s["microsteps_asked"] == 8 and driver.DriverAccess(board.request, board.protocol).physical


def test_stepdir_caps_say_what_the_chip_lacks_and_its_settings_are_refused_not_ignored():
    """The STEP/DIR driver's DRV:CAPS? on the real L2/L3: no software current or voltage drive,
    no stall detection, no hardware positioning or sync output, a fixed overcurrent trip; the
    physical setters it has no input for are refused, the rest quantised by the driver."""
    with vnucleo.boards(1, driver="stepdir") as procs:
        b = connect(procs[0][0])
        try:
            caps = views.driver_caps(b.request(C.driver_caps()))
            assert (caps["current_mode"], caps["voltage_mode"]) == (True, False)
            assert caps["current_settable"] is False             # the VREF pot sets it
            assert (caps["max_microsteps_current"], caps["max_microsteps_voltage"]) == (32, 0)
            assert caps["stall_detect"] is False and caps["hardware_positioning"] is False
            assert caps["standby_pin"] is True and caps["sync_pulses_per_full_step"] == 0
            assert caps["faults_reported"] == ["overcurrent", "thermal_shutdown"]
            assert caps["faults_alarmable"] == []
            assert caps["overcurrent_min_a"] == caps["overcurrent_max_a"] == 3
            assert caps["max_speed_fsps"] == 1562.5
            bad = b.request(C.cfg_current(0.3, 1.3, 1.6, 1.6))
            assert not bad.success and bad.error_code == "INVALID_STATE"
            assert not b.request(C.cfg_duty(0.1, 0.2, 0.3, 0.3)).success
            assert not b.request(C.cfg_overcurrent(50)).success
            assert not b.request(C.cfg_stall_threshold(0.5)).success
            assert not b.request(C.cfg_microsteps(64)).success
            assert not b.request(C.cfg_drive_mode(False)).success          # no voltage mode
            assert views.driver_setting(b.request(C.cfg_accel(2500.4)))["accel_fsps2"] == 2500
            assert views.driver_setting(b.request(C.cfg_max_speed(992.3)))["max_speed_fsps"] == 992.297
            us = views.driver_setting(b.request(C.cfg_microsteps(16)))
            assert (us["microsteps_asked"], us["microsteps"], us["drive_mode"]) == (16, 16, "current")
            assert b.request(C.cfg_apply()).success
            assert views.resolution(b.request(C.microsteps_query()))["microsteps"] == 16
            phys = views.driver_physical(b.request(C.driver_physical()))
            assert phys["overcurrent_a"] == 3 and "hold_a" not in phys and "hold_duty" not in phys
            assert "stall_threshold" not in phys and "overcurrent_v" not in phys
            # The protocol 2 aliases are the powerSTEP01's codes: this driver has none, so they
            # are refused (INVALID_STATE, naming the protocol 3 command) and change nothing
            for r in (b.request(C.cfg_tval(0x04, 0x11, 0x14, 0x14)), b.request(C.cfg_ocd(3))):
                assert not r.success and r.error_code == "INVALID_STATE", r.raw
            assert views.driver_physical(b.request(C.driver_physical())) == phys
        finally:
            b.close()


def test_motion_moves_the_fake_motor_and_the_encoder_follows(any_board):
    board = any_board
    board.request(C.fault_clear())
    board.request(C.mot_enable())
    assert board.request(C.mot_move(1600, 1)).success              # 1600 microsteps
    assert wait(lambda: board.last_status.get("motor", {}).get("busy") is False
                and board.last_status["motor"]["position"] == 1600, timeout=5.0)
    assert board.last_status["encoder"]["count"] != 0
    # the encoder saw the shaft angle the driver says it commanded (1000 PPR, x4)
    upr = views.resolution(board.request(C.microsteps_query()))["microsteps_per_rev"]
    assert abs(board.request(C.status()).data["encoder"]["count"] - 1600 * 4000 / upr) <= 2


def test_bitmap_upload_is_base64_text_through_the_real_handler(board):
    """UI:DISP:BITMAP on the real L2 code: the payload (with 0x18 and CR/LF bytes in it) goes
    out as base64 with the CRC encoded in it and the board stays in session (no stop)."""
    assert board.request(C.ui_mode(True)).success
    pix = bytes([0x18, 0x0A, 0xFF, 0x00, 0x12, 0x34, 0x18, 0x0D] * 8)       # 8x4 px
    r = board.request(C.disp_bitmap(0, 0, 8, 4, pix, crc=True))
    assert r.success, (r.error_code, r.error_message)
    st = board.request(C.status())
    assert st.success and st.data["state"] != "ESTOP"
    board.request(C.ui_mode(False))


@pytest.mark.parametrize("driver", DRIVERS)
def test_compact_telemetry_runs_at_50_ms_on_the_real_firmware_code(driver):
    with vnucleo.boards(1, driver=driver) as procs:
        b = connect(procs[0][0], telemetry_period_ms=50, telemetry_compact=True)
        try:
            q = b.request(C.telemetry_format_query())
            assert q.success and q.data["format"] == "compact"
            r = b.request(C.telemetry_rate_query())
            assert r.success and r.data["period_ms"] == 50               # below the full floor (139)
            b.request(C.fault_clear()); b.request(C.mot_enable())
            assert b.request(C.mot_move(1600, 1)).success
            assert wait(lambda: b.last_status.get("motor", {}).get("position") == 1600, 5.0)
            assert "system" in b.last_status                             # merged, never partial
        finally:
            b.close()


def test_telemetry_push_and_event_seq(any_board):
    board = any_board
    seqs = []
    board.subscribe("event", lambda ev: seqs.append(ev.seq))
    assert wait(lambda: bool(board.last_status), 2.0)
    board.request(C.fault_clear())
    board.request(C.mot_enable())
    board.request(C.mot_move(400, 1))                                  # MOTION_DONE event
    assert wait(lambda: seqs, 5.0)
    assert seqs == list(range(seqs[0], seqs[0] + len(seqs)))
    assert not board.gaps


def test_dev_role_works_on_the_sim(board):
    """The sim initialises the identity store the firmware forgets (LINK_BUDGET.md finding 1)."""
    assert board.request(C.role("FL")).success
    assert board.request(C.role_query()).data["role"] == "FL"


@pytest.mark.parametrize("driver", DRIVERS)
def test_closing_the_link_lets_the_watchdog_stop_the_motor(driver):
    with vnucleo.boards(1, driver=driver) as procs:
        url = procs[0][0]
        b = connect(url, heartbeat_timeout_ms=300)
        b.request(C.fault_clear())
        b.request(C.mot_enable())
        b.request(C.mot_run_rpm(30, 1))
        b.close()                                                       # host gone, watchdog armed
        time.sleep(0.6)
        b2 = connect(url, heartbeat=False)
        st = b2.request(C.status())
        b2.close()
        assert st.data["state"] == "ESTOP" and st.data["motor"]["hi_z"]


def test_four_boards_have_distinct_identities():
    with vnucleo.boards(4) as procs:
        bs = [connect(url) for url, _ in procs]
        try:
            assert sorted(b.identity.serial for b in bs) == ["VN000001", "VN000002", "VN000003", "VN000004"]
            assert len({b.boot_id for b in bs}) == 4
        finally:
            for b in bs:
                b.close()
