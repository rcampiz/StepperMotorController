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
    # the sim is in current mode: the TVAL faders follow DRV:CFG? and the KVAL faders are left
    # alone (the KVAL registers hold TVAL in current mode)
    assert pump(app, lambda: w._driver_panel._tval_sliders[1].value() == 0x11)
    assert w._driver_panel._kval_hold_slider.value() == 16 and w._driver_panel._is_current_mode()   # 0x29 from DRV:CFG?
    assert pump(app, lambda: w._stepmode_panel._mode_combo.currentIndex() == 3)


def test_step_mode_apply_reads_the_chip_back(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    w._stepmode_panel._mode_combo.setCurrentIndex(7)
    w._stepmode_panel._on_apply()
    assert pump(app, lambda: fake.step_mode == 7 and "DRV:CFG:APPLY" in fake.received)
    label = w._stepmode_panel._current_label
    # the fake board is in current mode: the chip runs 1/16 and that is not a mismatch
    assert pump(app, lambda: "reg=4" in label.text() and "current mode runs 1/16" in label.text()
                and "config says" not in label.text())
    after_apply = fake.received.index("DRV:CFG:APPLY")
    assert "DRV:STEP_MODE?" in fake.received[after_apply:]       # the chip is read back, not the config


def test_drive_mode_apply_sends_mode_tval_and_apply(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    w._driver_panel.set_drive_mode(True)
    w._driver_panel._rated_spin.setValue(2.0)               # faders capped at 2.0 A -> 0x1A
    w._driver_panel._tval_spins[1].setValue(1.3)            # 1.3 A run -> 0x11 (78 mA/unit)
    assert w._driver_panel._tval_sliders[1].value() == 0x11
    assert w._driver_panel._tval_sliders[2].maximum() == 0x1A
    w._driver_panel._idle_spin.setValue(2.5)
    w._driver_panel._on_apply_drive()
    assert pump(app, lambda: fake.drive_current and "DRV:CFG:APPLY" in fake.received)
    assert fake.tval[1] == 0x11 and fake.idle_release_ms == 2500
    assert fake.received.index("DRV:CFG:DRIVE CURRENT") < fake.received.index("DRV:CFG:APPLY")


def test_current_regulation_apply_sends_pred_decay_and_apply(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    p = w._driver_panel
    p._pred_cb.setChecked(True)
    p._tsw_spin.setValue(84)
    p._fast_step_spin.setValue(6)
    p._toff_fast_spin.setValue(22)
    p._toff_min_spin.setValue(24.5)
    p._on_apply_cm()
    assert pump(app, lambda: fake.cm["pred"] and "DRV:CFG:APPLY" in fake.received)
    assert fake.cm["tsw_us"] == 84 and fake.cm["t_fast"] == 0x2A and fake.cm["toff_min"] == 0x30
    assert fake.received.index("DRV:CFG:PRED ON 84") < fake.received.index("DRV:CFG:APPLY")


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


def test_tests_tab_graphs_a_resonance_map(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    fake.drive_current = False                 # voltage mode, where 1/128 is real
    w._tests_panel.start("resonance_map", {"modes": [4, 7], "rpm_from": 60, "rpm_to": 80, "rpm_step": 10,
                                           "dwell_s": 0.2, "settle_s": 0.05})
    assert pump(app, lambda: w._tests_panel.results, timeout=20.0)
    res = w._tests_panel.results[0]
    assert res.ok, (res.summary, res.checks)
    curves = w._tests_panel.curves_of(res.measurements)
    assert [c[0].split(" ")[0] for c in curves] == ["1/16", "1/128"]
    assert [p[0] for p in curves[0][1]] == [60, 70, 80]
    assert len(w._tests_panel._plot.getPlotItem().listDataItems()) == 2
    w._tests_panel._steps_axis_cb.setChecked(True)
    assert w._tests_panel._plot.getPlotItem().getAxis("top").isVisible()


def test_tests_tab_graphs_a_position_capture(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    w._tests_panel.start("position_capture", {"rpm": 60, "rate_hz": 2000, "settle_s": 0.05})
    assert pump(app, lambda: w._tests_panel.results, timeout=30.0)
    res = w._tests_panel.results[0]
    assert res.ok, (res.summary, res.checks)
    items = w._tests_panel._plot.getPlotItem().listDataItems()
    assert len(items) == 1 and len(items[0].xData) == 4096


def test_status_strip_follows_telemetry_and_the_tabs_are_regrouped(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    fake.position = 4000
    assert pump(app, lambda: "rev" in w._status_strip._pos.value.text())
    assert w._status_strip._state.value.text() in ("IDLE", "ESTOP", "RUNNING", "ARMED", "FAULT")
    assert "COM" in w._status_strip._link.value.text() or w._status_strip._link.value.text() != "disconnected"
    tabs = [w._right_tabs.tabText(i) for i in range(w._right_tabs.count())]
    assert tabs == ["Dashboard", "Drive", "Motor", "Encoder", "Diagnostics", "Fleet"]
    assert w.minimumWidth() >= 1100


def test_drive_mode_switch_keeps_both_fader_sets_live_and_shows_the_chip(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    p = w._driver_panel
    assert pump(app, lambda: "CURRENT" in p._chip_mode_label.text())     # DRV:STEP_MODE? readback
    p.set_drive_mode(False)
    assert not p._is_current_mode() and p._kval_row.isEnabled() and p._tval_row.isEnabled()
    assert p._apply_kval_btn.isEnabled() and p._apply_drive_btn.isEnabled()
    p._on_apply_drive()
    assert pump(app, lambda: "DRV:CFG:DRIVE VOLTAGE" in fake.received)
    p.set_drive_mode(True)
    assert p._coil_a_curve.xData is not None and len(p._coil_a_curve.xData) > 30    # 4 x 8 staircase (sim is 1/8)


def test_encoder_tab_dial_follows_the_count_and_capture_button_runs_the_scenario(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    fake.position = 800                                       # 1/8 step sim: 800 usteps = half a rev
    assert pump(app, lambda: w._graph_panel._dial._count != 0)
    assert w._graph_panel._dial._cpr == fake.enc_ppr
    w._graph_panel._capture_btn.click()
    assert w._right_tabs.currentIndex() == 4
    assert pump(app, lambda: w._tests_panel.results, timeout=30.0)
    assert w._tests_panel.results[0].name == "position_capture" and w._tests_panel.results[0].ok


def test_light_mode_toggle_switches_the_palette_and_is_remembered(gui):
    app, w, fake = gui
    from smc_gui import theme
    w.set_light_mode(True)
    assert theme.MODE == "light" and theme.BG == "#f3f5f9"
    assert "#f3f5f9" in app.styleSheet()
    assert w._light_action.isChecked()
    assert theme.saved_mode() == "light"
    w.set_light_mode(False)
    assert theme.MODE == "dark" and "#080810" in app.styleSheet()
    assert theme.saved_mode() == "dark"


def test_controller_drives_the_motor_from_the_drive_tab(gui):
    app, w, fake = gui
    from smc_core.gamepad import SoftwareGamepad
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    pad = SoftwareGamepad()
    w._controller_panel.use_gamepad(pad)
    w._controller_panel.stick.slew_rpm_per_s = 100000
    w._controller_panel.set_driving(True)
    pad.set(ly=1.0)
    assert pump(app, lambda: any(r.startswith("MOT:RUN 250.0 RPM 1") for r in fake.received), timeout=3.0), fake.received[-5:]
    assert pump(app, lambda: fake.speed > 0)
    pad.set(ly=0.0)
    assert pump(app, lambda: fake.speed == 0, timeout=3.0)
    pad.set(buttons={"START"})
    assert pump(app, lambda: fake.state == "ESTOP", timeout=5.0)
    w._controller_panel.set_driving(False)


def test_motor_profile_applies_to_the_board_and_sets_the_rating(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    assert pump(app, lambda: "DRV:CFG?" in fake.received)
    w._profile_panel.select("generic-nema17-1.7A")
    w._profile_panel._save_cb.setChecked(True)
    w._profile_panel._on_apply()
    assert pump(app, lambda: "DRV:CFG:SAVE" in fake.received, timeout=5.0), fake.received[-8:]
    assert any(r.startswith("DRV:CFG:TVAL 04 0F") for r in fake.received)
    assert fake.step_mode == 4
    assert pump(app, lambda: abs(w._driver_panel.rated_current() - 1.7) < 1e-6, timeout=5.0), w._driver_panel.rated_current()


def test_vcom_slider_and_black_test_screen(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    w._display_panel._vcom_slider.setValue(0x19)
    assert pump(app, lambda: "UI:LCD:REG BB 19" in fake.received, timeout=3.0), fake.received[-5:]
    assert fake.panel_regs[0xBB] == [0x19]
    w._on_panel_test_requested()
    assert pump(app, lambda: any(r.startswith("UI:DISP:RECT 180 46 50 10 001F fill") for r in fake.received), timeout=5.0)
    assert "UI:MODE REMOTE" in fake.received


def test_the_first_indicator_reaches_the_board_after_the_mode_switch(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    w._on_indicator_send_requested(135, 1, True, 40, 60)
    assert pump(app, lambda: getattr(fake, "indicator", None) == (135, 1, True, 40, 60), timeout=3.0), \
        [r for r in fake.received if r.startswith("UI:")]


def test_gui_notices_the_board_was_taken_back_to_its_menu(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    w._on_indicator_send_requested(90, 1, True, 50, 50)
    assert pump(app, lambda: getattr(fake, "ui_mode", "") == "REMOTE", timeout=3.0)
    fake.ui_mode = "LOCAL"                                   # someone held CENTER on the board
    assert pump(app, lambda: not w._remote_mode_sent, timeout=4.0)
    w._on_indicator_send_requested(80, 1, True, 50, 50)     # the next one takes the LCD again
    assert pump(app, lambda: fake.ui_mode == "REMOTE", timeout=3.0)


def test_tests_tab_runs_a_scenario(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    w._tests_panel.start("bringup", {})
    assert pump(app, lambda: w._tests_panel.results, timeout=10.0)
    assert w._tests_panel.results[0].ok


def test_memory_panel_polls_only_while_visible_and_renders(gui):
    app, w, fake = gui
    assert pump(app, lambda: w._board is not None and w._board.session_ready)
    panel = w._memory_panel
    assert not panel.polling and "DIAG:MEM?" not in fake.received       # off screen: never asked
    w.show()
    w._right_tabs.setCurrentIndex(4)                                     # Diagnostics
    w._diag_tabs.setCurrentWidget(panel)
    assert pump(app, lambda: panel.polling and "ram" in panel.view)
    assert "DIAG:MEM?" in fake.received
    assert panel.flash.region["level"] == "ok" and panel.ram.region["level"] == "tight"   # 55 %, 84 %
    assert panel.split.split["stacks"] == 4096 * 4
    names = [r["name"] for r in panel.stacks.rows]
    assert names[0] == "ISR (main)" and names[1] == "Comms" and names[-1] == "IDLE"   # biggest stack first
    # the figures move: the next poll shows them
    fake.mem["ram"] = (95 * 1024, 96 * 1024)
    panel.refresh_requested.emit()
    assert pump(app, lambda: panel.ram.region["level"] == "full")
    # a board that cannot answer: said on the panel, not in the status bar
    fake.mem = {}
    fake.mem_tasks = []
    panel.refresh_requested.emit()
    assert pump(app, lambda: "unavailable" in panel._status.text())
    # another page: polling stops
    w._diag_tabs.setCurrentIndex(0)
    app.processEvents()
    assert not panel.polling
