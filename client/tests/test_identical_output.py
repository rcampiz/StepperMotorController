"""GUI and CLI run the same scenarios with identical results and command streams
(CLIENT_REVIEW section 9 rule 5, with changes B: heartbeat/setup traffic filtered,
polling runs collapsed). Each frontend gets its own fresh board: the Python fake, and
the virtual Nucleo (the firmware's real L2 + L3) when sim/ has been built."""

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
    ("heartbeat_loss", {"timeout_ms": 300, "margin_ms": 300}),
]

from contextlib import contextmanager  # noqa: E402

from . import vnucleo  # noqa: E402


@contextmanager
def a_board(backend):
    """Yield (transport factory for the GUI, --port/--sim argv for the CLI) for one fresh board."""
    if backend == "fake":
        yield None, ["--sim"]
        return
    with vnucleo.boards(1) as procs:
        url = procs[0][0]
        yield url, ["--port", url]


def _pump(app, cond, timeout=20.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        app.processEvents()
        if cond():
            return True
        time.sleep(0.005)
    return False


def gui_results(qapp, url=None):
    from smc_core.sim import FakeBoard
    from smc_core.transport import open_transport
    from smc_core.transport.memory import MemoryTransport
    from smc_gui.main_window import MainWindow
    w = MainWindow()
    fake = None
    if url is None:
        t = MemoryTransport(name="sim")
        fake = FakeBoard(t)
    else:
        t = open_transport(url)
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
        if fake is not None:
            fake.stop()


def cli_results(tmp_path, board_argv):
    from smc_cli.__main__ import main
    out = tmp_path / "cli.jsonl"
    for name, params in SCENARIOS:
        argv = ["test", "run", *board_argv, name, "--out", str(out)]
        for k, v in params.items():
            argv += ["-p", f"{k}={json.dumps(v)}"]
        main(argv)
    rows = runner.read_jsonl(str(out))
    results = []
    for row in rows:
        row["checks"] = [Check(**c) for c in row["checks"]]
        results.append(ScenarioResult(**row))
    return results


@pytest.mark.parametrize("backend", ["fake", "vnucleo"])
def test_gui_and_cli_produce_identical_scenario_output(qapp, tmp_path, capsys, backend):
    if backend == "vnucleo":
        vnucleo.require()
    with a_board(backend) as (url, _):
        gui = [runner.comparable(r) for r in gui_results(qapp, url)]
    with a_board(backend) as (_, argv):
        cli = [runner.comparable(r) for r in cli_results(tmp_path, argv)]
    capsys.readouterr()
    assert [g["name"] for g in gui] == [n for n, _ in SCENARIOS]
    assert all(g["ok"] for g in gui), [g for g in gui if not g["ok"]]
    for g, c in zip(gui, cli):
        assert g == c, g["name"]
