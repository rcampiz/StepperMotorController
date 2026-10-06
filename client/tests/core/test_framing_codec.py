"""Framing and codec against lines in the exact formats the firmware emits (protocol 2).

Each sample is copied from the format strings in server/layers/L2_protocol
(command_parser.cpp emitBanner/emitTelemetry, event_codec.cpp, the handlers).
"""

import pytest

from smc_core.codec import (ProtocolError, multiline_text, parse_event, parse_heartbeat_ack,
                            parse_idn, parse_reset, parse_response, parse_telemetry)
from smc_core.framing import FrameKind, classify

BANNER = [
    "!RESET boot=1A2B3C4D fw=0.4.0 proto=2 build=d6b06d5",
    "",
    "========================================",
    "   Stepper Motor Controller v0.4.0",
    "========================================",
    "Type 'SYST:HELP?' for available commands",
    "",
]


@pytest.mark.parametrize("line,kind", [
    (BANNER[0], FrameKind.RESET),
    (BANNER[1], FrameKind.EMPTY),
    (BANNER[2], FrameKind.BANNER),
    (BANNER[3], FrameKind.BANNER),
    (BANNER[5], FrameKind.BANNER),
    ("!FAULT status=0x1234 detail=OCD", FrameKind.EVENT),
    ('{"kind":"event","type":"STALL","seq":7,"ts_ms":1234,"data":{"status":4660,"detail":"A"}}',
     FrameKind.EVENT),
    ('{"kind":"telemetry","data":{"state":"IDLE","tick":1}}', FrameKind.TELEMETRY),
    ("TELEM: pos=-42 spd=1200 enc=99 vel=-7", FrameKind.TELEMETRY),
    ("EVENT JOY LEFT pressed", FrameKind.JOYSTICK),
    ('{"status":"ok","command":"PING","data":{"seq":5}}', FrameKind.RESPONSE),
    ("OK 7", FrameKind.RESPONSE),
    ("ERROR USAGE Usage: SYST:TLM:RATE <ms>", FrameKind.RESPONSE),
    ("Motor Config (defaults)", FrameKind.RESPONSE),
])
def test_classify(line, kind):
    assert classify(line).kind == kind


def test_echo_only_matches_the_command_in_flight():
    assert classify("DIAG:PING 5", "DIAG:PING 5").kind == FrameKind.ECHO
    assert classify("DIAG:PING 5", "SYST:TICK?").kind == FrameKind.RESPONSE


def test_json_frames_carry_the_parsed_object():
    f = classify('{"status":"ok","command":"IDN","data":{"proto":2}}')
    assert f.obj["data"]["proto"] == 2


# --- replies -----------------------------------------------------------------------
def test_ascii_error_with_code_protocol_2():
    r = parse_response("ERROR USAGE Usage: SYST:TLM:RATE <ms>", "SYST:TLM:RATE")
    assert not r.success
    assert r.error_code == "USAGE"
    assert r.error_message == "Usage: SYST:TLM:RATE <ms>"


def test_ascii_error_without_code_protocol_1():
    r = parse_response("ERROR dir must be 0 or 1", "MOT:MOVE")
    assert r.error_code == "ERROR" and r.error_message == "dir must be 0 or 1"


def test_ascii_ok_and_ready():
    assert parse_response("OK ENABLED").data == {"message": "ENABLED"}
    assert parse_response("OK READY 9600").data == {"ready_bytes": 9600}
    assert parse_response("OK").data == {}


def test_json_ok_and_error():
    r = parse_response('{"status":"ok","command":"PING","data":{"seq":1,"rx_tick":100}}')
    assert r.success and r.command == "PING" and r.data["rx_tick"] == 100
    e = parse_response('{"status":"error","command":"MOTOR_DEBUG","code":"DBG_LOCKED",'
                       '"message":"send DBG:UNLOCK first"}')
    assert e.error_code == "DBG_LOCKED" and e.command == "MOTOR_DEBUG"


def test_json_escaped_message_round_trips():
    r = parse_response('{"status":"error","command":"UI_MODE","code":"USAGE",'
                       '"message":"bad \\"x\\" \\\\ value"}')
    assert r.error_message == 'bad "x" \\ value'


def test_bad_json_is_a_protocol_error():
    with pytest.raises(ProtocolError):
        parse_response('{"status":"ok",')


def test_multiline_json_reply_exposes_lines():
    r = parse_response('{"status":"ok","command":"MCONFIG","data":{"lines":["Motor Config",'
                       '"KVAL: HOLD=29"],"count":2}}')
    assert r.lines == ["Motor Config", "KVAL: HOLD=29"]
    assert multiline_text(r) == "Motor Config\nKVAL: HOLD=29"


def test_legacy_ascii_replies():
    hb = parse_response("HEARTBEAT_ACK 7 123456 450 1A2B3C4D")
    assert hb.data == {"seq": 7, "mcu_tick": 123456, "remaining_ms": 450, "boot": 0x1A2B3C4D}
    st = parse_response("STATUS IDLE 1000 0 OPEN_LOOP READY -5 0")
    assert st.data["position"] == -5 and st.data["state"] == "IDLE"


# --- unsolicited frames ---------------------------------------------------------------
def test_reset_line():
    r = parse_reset(BANNER[0])
    assert (r.boot, r.fw, r.proto, r.build) == (0x1A2B3C4D, "0.4.0", 2, "d6b06d5")


def test_events_both_formats():
    a = parse_event("!FAULT status=0x1234 detail=OCD")
    assert (a.type, a.status, a.detail, a.seq) == ("FAULT", 0x1234, "OCD", None)
    j = parse_event('{"kind":"event","type":"STALL","seq":7,"ts_ms":1234,'
                    '"data":{"status":4660,"detail":"A"}}')
    assert (j.type, j.seq, j.ts_ms, j.status, j.detail) == ("STALL", 7, 1234, 4660, "A")
    plain = parse_event("!MOTION_DONE status=0x7E03")
    assert plain.detail is None and plain.status == 0x7E03


def test_telemetry_both_formats():
    j = parse_telemetry('{"kind":"telemetry","data":{"state":"IDLE","motor":{"position":3}}}')
    assert j["motor"]["position"] == 3
    a = parse_telemetry("TELEM: pos=-42 spd=1200 enc=99 vel=-7")
    assert a == {"motor": {"position": -42, "speed": 1200},
                 "encoder": {"count": 99, "velocity": -7}}


def test_identity_both_formats():
    j = parse_idn(parse_response('{"status":"ok","command":"IDN","data":{"vendor":"SMC",'
                                 '"model":"NUCLEO-F401RE-IHM03A1","serial":"HOST0001",'
                                 '"fw":"0.4.0","proto":2}}'))
    a = parse_idn(parse_response("OK SMC,NUCLEO-F401RE-IHM03A1,HOST0001,0.4.0,2"))
    assert j == a
    assert j.proto == 2 and j.serial == "HOST0001"


def test_heartbeat_ack_json_boot_is_decimal():
    ack = parse_heartbeat_ack(parse_response(
        '{"status":"ok","command":"HEARTBEAT","data":{"seq":3,"mcu_tick":99,'
        '"remaining_ms":480,"boot":439041101}}'))
    assert ack.boot == 0x1A2B3C4D and ack.remaining_ms == 480
