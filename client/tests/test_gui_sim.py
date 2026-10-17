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
    assert fake.json and fake.tlm_ms == 200 and fake.hb_timeout_ms == 500
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
    assert pump(app, lambda: fake.json and fake.tlm_ms == 200)


def test_monitor_mode_uses_the_firmware_name(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    w._motion_panel._on_mode_changed(1)
    assert pump(app, lambda: "CTRL:MODE MONITOR" in fake.received)


def test_panels_render_core_views(gui):
    app, w, fake = gui
    fake.position = 1234
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    w._on_refresh_params()
    assert pump(app, lambda: w._telemetry_panel._encoder_count_label.text() == "1,234")
    assert pump(app, lambda: w._driver_panel._kval_hold_slider.value() == 16)   # 0x29 -> 16 %
    assert pump(app, lambda: w._stepmode_panel._mode_combo.currentIndex() == 3)


def test_step_mode_apply_reads_the_chip_back(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    w._stepmode_panel._mode_combo.setCurrentIndex(7)
    w._stepmode_panel._on_apply()
    assert pump(app, lambda: fake.step_mode == 7 and "DRV:CFG:APPLY" in fake.received)
    label = w._stepmode_panel._current_label
    assert pump(app, lambda: "reg=7" in label.text() and "config says" not in label.text())
    after_apply = fake.received.index("DRV:CFG:APPLY")
    assert "DRV:STEP_MODE?" in fake.received[after_apply:]       # the chip is read back, not the config


def test_drive_mode_apply_sends_mode_tval_and_apply(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    w._driver_panel._drive_mode_combo.setCurrentIndex(1)
    w._driver_panel._tval_sliders[1].setValue(13)          # 1.3 A run -> 0x11 (78 mA/unit)
    w._driver_panel._on_apply_drive()
    assert pump(app, lambda: fake.drive_current and "DRV:CFG:APPLY" in fake.received)
    assert fake.tval[1] == 0x11
    assert fake.received.index("DRV:CFG:DRIVE CURRENT") < fake.received.index("DRV:CFG:APPLY")


def test_protection_apply_sends_typed_requests_in_order(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    w._protection_panel._on_apply()
    assert pump(app, lambda: "DRV:CFG:APPLY" in fake.received)
    tail = [l for l in fake.received if l.startswith("DRV:CFG:")][-5:]
    assert [l.split()[0] for l in tail] == ["DRV:CFG:OCD", "DRV:CFG:STALL", "DRV:CFG:FAULT",
                                            "DRV:CFG:FAULT", "DRV:CFG:APPLY"]
    assert tail[3].startswith("DRV:CFG:FAULT ACTION ")


def test_sysid_panel_runs_its_tests_through_the_core_scenario(gui):
    from smc_gui.sysid_panel import SysIdTestConfig
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    p = w._sysid_panel
    p._tests = [SysIdTestConfig(type="step", speed=800, start_speed=0, duration_ms=1000,
                                settle_ms=100)]
    p._start_tests()
    assert pump(app, lambda: not p._running, timeout=10.0)
    assert len(p._results) == 1 and len(p._results[0].samples) == 95
    assert "CTRL:SYSID:STEP 800 1 1000 100" in fake.received


def test_tests_tab_runs_a_scenario(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    w._tests_panel.start("bringup", {})
    assert pump(app, lambda: w._tests_panel.results, timeout=10.0)
    assert w._tests_panel.results[0].ok
