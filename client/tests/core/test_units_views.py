"""smc_core.units and smc_core.views (client refactor step 4)."""

from smc_core import units as U
from smc_core import views as V
from smc_core.codec import parse_response

G = U.Geometry(full_steps_per_rev=200, step_mode=3)        # 1600 microsteps/rev


def test_geometry_and_speed_units():
    assert G.microsteps_per_rev == 1600
    assert U.speed_to_sps(60, 0, G) == 200           # 60 RPM = 1 rev/s = 200 full steps/s
    assert U.speed_to_sps(2, 1, G) == 400
    assert U.speed_to_sps(123.4, 2, G) == 123
    assert U.sps_to_speed(200, 0, G) == 60.0


def test_position_and_accel_units():
    assert U.position_to_microsteps(1.5, 0, G) == 2400
    assert U.position_to_microsteps(90, 1, G) == 400
    assert U.microsteps_to_position(800, 1, G) == 180.0
    assert U.accel_to_sps2(2, 0, G) == 400


def test_register_scaling_round_trips():
    assert U.kval_from_percent(100) == 255 and U.kval_to_percent(0x29) == 16
    raw = U.acc_raw_from_sps2(500)
    assert raw == 34 and abs(U.acc_sps2_from_raw(raw) - 500) < U.ACC_DEC_FACTOR
    assert U.maxspd_raw_from_sps(0) == 1                     # never 0 (register minimum)


# Lines exactly as the firmware prints them (config_handlers.cpp, debug_service.cpp)
MCONFIG = [
    "Motor Config (from flash)",
    "KVAL: HOLD=29 RUN=29 ACC=29 DEC=29",
    "OCD_TH=08 STALL_TH=0A",
    "ACC=34 DEC=34 MAXSPD=52 MINSPD=0 FS_SPD=1023",
    "Faults: OCD=1 TH_SD=1 TH_W=1 UVLO=1 STALL_A=0 STALL_B=0 CMD=1",
    "Action: HardHiZ",
    "StepMode: 4 (1/16)",
]


def _json_lines(name, lines):
    import json
    return parse_response(json.dumps({"status": "ok", "command": name,
                                      "data": {"lines": lines, "count": len(lines)}}))


def test_motor_config_view_from_json_and_text():
    for resp in (_json_lines("MCONFIG", MCONFIG), "\n".join(MCONFIG)):
        cfg = V.motor_config(resp)
        assert cfg["source"] == "flash"
        assert cfg["kval"] == (0x29, 0x29, 0x29, 0x29)
        assert (cfg["ocd_th"], cfg["stall_th"]) == (8, 10)
        assert (cfg["acc_raw"], cfg["dec_raw"], cfg["maxspd_raw"]) == (34, 34, 52)
        assert cfg["faults"] == {"OCD": True, "TH_SD": True, "TH_WARN": True, "UVLO": True,
                                 "STALL_A": False, "STALL_B": False, "CMD_ERR": True}
        assert (cfg["action"], cfg["action_index"]) == ("HardHiZ", 1)
        assert (cfg["step_mode"], cfg["microsteps"]) == (4, 16)


def test_motor_debug_view():
    dbg = V.motor_debug(_json_lines("MOTOR_DEBUG", [
        "STATUS=7E03 (HiZ Idle UVLO)", "KVAL: HOLD=10 RUN=20 ACC=30 DEC=40",
        "ACC=34 DEC=35 MAXSPD=52 POS=-1200"]))
    assert dbg["status"] == 0x7E03 and dbg["kval"] == (0x10, 0x20, 0x30, 0x40)
    assert (dbg["acc_raw"], dbg["dec_raw"], dbg["maxspd_raw"], dbg["position"]) == (34, 35, 52, -1200)


def test_encoder_view_both_formats():
    j = V.encoder(parse_response('{"status":"ok","command":"ENC","data":{"count":-5,"velocity":12,'
                                 '"index_seen":true,"index_tick":9,"revolutions":1,"index_period_us":0}}'))
    a = V.encoder(parse_response("OK count=-5 vel=12 idx=1 idx_tick=9 rev=1"))
    assert j == a and j["count"] == -5 and j["index_seen"]


def test_sysid_views_both_formats():
    st = V.sysid_status(parse_response('{"status":"ok","command":"CTRL:SYSID:STATUS",'
                                       '"data":{"phase":4,"samples":120}}'))
    assert st == {"phase": V.SYSID_DONE, "samples": 120}
    assert V.sysid_status(parse_response("OK phase=2 samples=7"))["phase"] == 2
    j = V.sysid_data(parse_response('{"status":"ok","command":"SYSID_DATA","data":{"offset":40,'
                                    '"samples":2,"total":120,"lines":["0,100,98,-1","5,100,99,0"],'
                                    '"count":2}}'))
    assert (j.offset, j.total, j.samples) == (40, 120, [(0, 100, 98, -1), (5, 100, 99, 0)])
    from smc_core.codec import Response
    a = V.sysid_data(Response(True, "CTRL:SYSID:DATA?", {"lines": [
        "SYSID_DATA 40 2 120", "0,100,98,-1", "5,100,99,0"]}))
    assert (a.offset, a.total, a.samples) == (j.offset, j.total, j.samples)
