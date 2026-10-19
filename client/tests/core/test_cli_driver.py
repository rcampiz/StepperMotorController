"""The smc CLI's driver commands: capabilities and physical settings from the board, motor
profiles sent as the physical commands (or the chip codes for a protocol 2 board)."""

import json

from smc_cli.__main__ import main


def _json(capsys):
    return json.loads(capsys.readouterr().out.strip().splitlines()[-1])


def test_driver_caps_and_show_on_the_fake_board(capsys):
    assert main(["driver", "caps", "--sim"]) == 0
    out = _json(capsys)
    assert out["protocol"] == 3 and out["from_board"] is True
    assert out["caps"]["max_microsteps_current"] == 16 and out["caps"]["overcurrent_sense"] == "voltage"
    assert main(["driver", "show", "--sim"]) == 0
    out = _json(capsys)
    assert out["settings"]["run_a"] == 1.326 and out["in_force"]["microsteps"] == 8


def test_motor_show_and_apply_pick_the_commands_by_protocol(capsys):
    assert main(["motor", "show", "23HS22-2804-ME1K"]) == 0
    assert "DRV:CFG:CURRENT 0.31 1.33 1.56 1.56" in _json(capsys)["commands"]
    assert main(["motor", "show", "23HS22-2804-ME1K", "--protocol", "2"]) == 0
    assert "DRV:CFG:TVAL 04 11 14 14" in _json(capsys)["commands"]
    assert main(["motor", "apply", "generic-nema17-1.7A", "--sim"]) == 0
    sent = [s["command"] for s in _json(capsys)["sent"]]
    assert "DRV:CFG:USTEP 16" in sent and sent[-1] == "DRV:CFG:APPLY"
