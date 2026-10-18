"""The client core against the virtual Nucleo: the firmware's real L2 + L3 and task bodies.

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


@pytest.fixture
def board():
    with vnucleo.boards(1) as procs:
        b = connect(procs[0][0])
        yield b
        b.close()


def test_identity_and_protocol(board):
    assert board.identity.serial == "VN000001" and board.identity.proto == 2
    ver = board.request(C.version())
    assert ver.lines[0].startswith("Stepper Motor Controller v")


@pytest.mark.parametrize("name,params", [
    ("bringup", {}),
    ("step_mode_readback", {"modes": [0, 4, 7]}),
    ("estop_latency", {"rpm": 30.0, "run_s": 0.3}),
    ("heartbeat_loss", {"timeout_ms": 300, "margin_ms": 300}),
    ("sysid", {"type": "step", "speed": 400, "duration_ms": 600, "settle_ms": 150}),
    ("motion_scale", {"usteps": 25600, "speed": 400, "rev": 0.5}),      # the sim boots at 1/128
    ("telemetry_load", {"period_ms": 50, "usteps": 25600, "speed": 200}),
    ("step_mode_chip", {"mode": 7}),          # APPLY routes STEP_MODE through the Motor task when the bridges are on
    ("drive_mode_chip", {}),                  # CM_VM (current mode) takes the same Hi-Z path
])
def test_scenarios_pass_on_the_real_firmware_code(board, name, params):
    r = run(board, name, **params)
    assert r.ok, (r.summary, r.checks, r.error)


def test_motion_moves_the_fake_motor_and_the_encoder_follows(board):
    board.request(C.fault_clear())
    board.request(C.mot_enable())
    assert board.request(C.mot_move(1600, 1)).success              # 1600 microsteps
    assert wait(lambda: board.last_status.get("motor", {}).get("busy") is False
                and board.last_status["motor"]["position"] == 1600, timeout=5.0)
    assert board.last_status["encoder"]["count"] != 0


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


def test_telemetry_push_and_event_seq(board):
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


def test_closing_the_link_lets_the_watchdog_stop_the_motor():
    with vnucleo.boards(1) as procs:
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
