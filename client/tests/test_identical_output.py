"""GUI and CLI run the same scenarios with identical results and command streams
(CLIENT_REVIEW section 9 rule 5, with changes B: heartbeat/setup traffic filtered,
polling runs collapsed). Each frontend gets its own fake board; the virtual Nucleo
replaces the fake once it exists."""

import json
import time

import pytest

pytest.importorskip("PySide6")

from smc_core import runner  # noqa: E402
from smc_core.scenarios import Check, ScenarioResult  # noqa: E402

SCENARIOS = [
    ("bringup", {}),
    ("step_mode_readback", {"modes": [0, 5, 7]}),
    ("sysid", {"type": "step", "speed": 800, "duration_ms": 1000, "settle_ms": 100}),
    ("estop_latency", {"rpm": 20.0, "run_s": 0.1}),
    ("heartbeat_loss", {"timeout_ms": 200, "margin_ms": 200}),
]


def _pump(app, cond, timeout=20.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        app.processEvents()
        if cond():
            return True
        time.sleep(0.005)
    return False


def gui_results(qapp):
    from smc_core.sim import FakeBoard
    from smc_core.transport.memory import MemoryTransport
    from smc_gui.main_window import MainWindow
    w = MainWindow()
    t = MemoryTransport(name="sim")
    fake = FakeBoard(t)
    try:
        assert w.attach_transport(t, "sim")
        assert _pump(qapp, lambda: w._board.session_ready)
        _pump(qapp, lambda: False, timeout=0.5)            # let the panel refresh settle
        for name, params in SCENARIOS:
            n = len(w._tests_panel.results)
            w._tests_panel.start(name, params)
            assert _pump(qapp, lambda: len(w._tests_panel.results) > n), name
        return w._tests_panel.results
    finally:
        w.close()
        fake.stop()


def cli_results(tmp_path):
    from smc_cli.__main__ import main
    out = tmp_path / "cli.jsonl"
    for name, params in SCENARIOS:
        argv = ["test", "run", "--sim", name, "--out", str(out)]
        for k, v in params.items():
            argv += ["-p", f"{k}={json.dumps(v)}"]
        main(argv)
    rows = runner.read_jsonl(str(out))
    results = []
    for row in rows:
        row["checks"] = [Check(**c) for c in row["checks"]]
        results.append(ScenarioResult(**row))
    return results


def test_gui_and_cli_produce_identical_scenario_output(qapp, tmp_path, capsys):
    gui = [runner.comparable(r) for r in gui_results(qapp)]
    cli = [runner.comparable(r) for r in cli_results(tmp_path)]
    capsys.readouterr()
    assert [g["name"] for g in gui] == [n for n, _ in SCENARIOS]
    assert all(g["ok"] for g in gui), [g for g in gui if not g["ok"]]
    for g, c in zip(gui, cli):
        assert g == c, g["name"]
