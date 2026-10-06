"""The real MainWindow on the core Board, against the in-process fake board (no hardware)."""

import time

import pytest

pytest.importorskip("PySide6")


def pump(app, cond, timeout=4.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        app.processEvents()
        if cond():
            return True
        time.sleep(0.005)
    return False


@pytest.fixture
def gui(qapp):
    from smc_core.sim import FakeBoard
    from smc_core.transport.memory import MemoryTransport
    from smc_gui.main_window import MainWindow
    w = MainWindow()
    t = MemoryTransport(name="sim")
    fake = FakeBoard(t)
    assert w.attach_transport(t, "sim")
    yield qapp, w, fake
    w.close()
    fake.stop()
    qapp.processEvents()


def test_connect_populates_panels_and_telemetry_flows(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._motor_panel.isEnabled())
    assert pump(app, lambda: bool(w._board.last_status))
    assert fake.json and fake.tlm_ms == 100 and fake.hb_timeout_ms >= 2000
    assert pump(app, lambda: "DRV:CFG?" in fake.received and "CTRL:TRIM?" in fake.received)
    assert "DIAG:STAT?" not in fake.received                 # pushed, never polled
    assert "SYST:FAULT:CLEAR" in fake.received               # historical connect behaviour


def test_panel_command_goes_through_the_board(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._motor_panel.isEnabled())
    w._send_command("MOT:EN")
    assert pump(app, lambda: "MOT:EN" in fake.received)


def test_estop_button_path_confirms(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    results = []
    w._bridge.estop_result.connect(results.append)
    w._on_estop_requested()
    assert pump(app, lambda: results)
    assert results[0].confirmed
    assert fake.state == "ESTOP"


def test_typed_estop_line_never_enters_the_fifo(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    w._send_command("SYST:ESTOP")
    assert pump(app, lambda: fake.state == "ESTOP")
    assert any(k == "purge" for k, _ in w._board.transport.log)


def test_board_reset_is_shown_and_session_restored(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    fake.reboot()
    assert pump(app, lambda: "BOARD RESET" in w._status_bar.currentMessage())
    assert pump(app, lambda: fake.json and fake.tlm_ms == 100)
