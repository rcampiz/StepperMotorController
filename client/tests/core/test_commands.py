"""The typed command API matches docs/PROTOCOL.md, both ways (client refactor step 2)."""

import inspect
import os
import subprocess
import sys

import pytest

from smc_core import commands as C
from smc_core.protocol_spec import COMMANDS, PROTOCOL_VERSION

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

# min_args in the catalogue is read from the handler's `argCount < N` checks. These
# handlers check argCount only to choose between a query and a set form, so the real
# minimum is 0 for the spelling listed here.
MIN_ARGS_HEURISTIC_EXCEPTIONS = {"CTRL:ENC:FILT?"}

# Arguments for builders whose parameters are constrained (everything else gets 1 / 1.0 / True)
SAMPLE_ARGS = {
    "fmt": (True,),
    "control_mode": ("PID",),
    "role": ("FL",),
    "disp_text": (0, 0, 0xFFFF, 0, "hello"),
    "disp_bitmap_b64": (0, 0, 2, 1, "AAAAAA=="),
    "disp_bitmap": (0, 0, 2, 1, b"\x00\x00\x00\x00"),
    "disp_bitmap_rle": (0, 0, 2, 1, b"\x01\x00\x00"),
    "flash_upload": (0, b"\x00\x00"),
    "flash_upload_rle": (0, b"\x01\x00\x00"),
    "baud": (115200,),
    "enc_filter_legacy": ("EMA",),
    "cfg_fault_action": (1,),
    "disp_indicator": (90, 1, True),
}


def _sample(fn):
    if fn.__name__ in SAMPLE_ARGS:
        return SAMPLE_ARGS[fn.__name__]
    args = []
    for p in inspect.signature(fn).parameters.values():
        if p.default is not inspect.Parameter.empty or p.kind == p.VAR_POSITIONAL:
            continue
        ann = p.annotation
        args.append(1.0 if ann is float else True if ann is bool else "X" if ann is str else 1)
    return tuple(args)


def test_every_catalogue_command_has_a_builder_and_vice_versa():
    missing = set(COMMANDS) - C.covered()
    unknown = C.covered() - set(COMMANDS)
    assert not missing, f"commands in PROTOCOL.md without a typed builder: {sorted(missing)}"
    assert not unknown, f"builders for commands not in PROTOCOL.md: {sorted(unknown)}"


@pytest.mark.parametrize("name", sorted(C.BUILDERS))
def test_each_builder_emits_a_declared_catalogue_command(name):
    fn = getattr(C, name)
    req = fn(*_sample(fn))
    assert req.name in C.BUILDERS[name]
    spec = COMMANDS[req.name]
    if req.name not in MIN_ARGS_HEURISTIC_EXCEPTIONS:
        assert len(req.args) >= spec.min_args, (req.line, spec.min_args)
    assert req.reply == (C.Reply.READY if spec.binary else
                         C.Reply.LINES if spec.multiline else C.Reply.SINGLE)


def test_spec_is_fresh_against_the_firmware_parser():
    r = subprocess.run([sys.executable, os.path.join(REPO, "scripts", "gen_protocol.py"), "--check"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr


def test_protocol_version_is_two():
    assert PROTOCOL_VERSION == 2


@pytest.mark.parametrize("req,line", [
    (C.mot_move(400, 1), "MOT:MOVE 400 1"),
    (C.mot_move_rev(1.5, 0, 200), "MOT:MOVE 1.5000 REV 0 200"),
    (C.mot_goto_rev(-2, None), "MOT:GOTO -2.0000 REV"),
    (C.mot_run_rpm(60, 1), "MOT:RUN 60.0 RPM 1"),
    (C.mot_stop(hard=True), "MOT:STOP hard"),
    (C.heartbeat(7), "SYST:HB 7"),
    (C.telemetry_rate(100), "SYST:TLM:RATE 100"),
    (C.events_enable(), "SYST:EVT:EN"),
    (C.events_enable(0x05), "SYST:EVT:EN 0x05"),
    (C.cfg_kval(0x29, 0x29, 0x29, 0x29), "DRV:CFG:KVAL 29 29 29 29"),
    (C.cfg_fault_enables(1, 1, 1, 1, 0, 0, 1), "DRV:CFG:FAULT 1 1 1 1 0 0 1"),
    (C.cfg_fault_action(1), "DRV:CFG:FAULT ACTION 1"),
    (C.trim_gains(0.5, 0.125), "CTRL:TRIM:GAINS 0.50 0.12"),
    (C.trim_gains(0.5, 0.125, decimals=4), "CTRL:TRIM:GAINS 0.5000 0.1250"),
    (C.enc_filter_ema(True, 51), "CTRL:ENC:FILT:EMA 1 51"),
    (C.sysid_step(800, 1), "CTRL:SYSID:STEP 800 1"),
    (C.sysid_step(800, 1, 5000, 1000), "CTRL:SYSID:STEP 800 1 5000 1000"),
    (C.disp_clear(0xF800), "UI:DISP:CLEAR F800"),
    (C.disp_indicator(370, -1, False), "UI:DISP:INDICATOR 10 -1 0"),
    (C.flash_upload_rle(2, b"abc"), "UI:FLASH:UPLOAD:RLE 2 3 CRC"),
    (C.queue_move(100, 1), "SYNC:QUEUE MOVE 100 1"),
    (C.debug_raw("SPI_STOP"), "SPI_STOP"),
])
def test_wire_spelling(req, line):
    assert req.line == line


def test_framing_limits_are_enforced_on_the_host():
    with pytest.raises(C.CommandError):
        C.disp_text(0, 0, 0, 0, "two words")          # the parser splits on spaces
    with pytest.raises(C.CommandError):
        C.disp_text(0, 0, 0, 0, "x" * 32)              # args are at most 31 chars
    with pytest.raises(C.CommandError):
        C.request("NOT:A:COMMAND")
    with pytest.raises(C.CommandError):
        C.mot_move(10, 2)                              # direction is 0 or 1
    with pytest.raises(C.CommandError):
        C.cfg_kval(0x100, 0, 0, 0)


def test_binary_and_multiline_requests_carry_their_reply_shape():
    assert C.flash_upload(0, b"\x00\x00").reply == C.Reply.READY
    assert C.driver_config().reply == C.Reply.LINES
    assert C.status().reply == C.Reply.SINGLE
    assert C.debug_raw("PS01_DIAG").reply == C.Reply.LINES
