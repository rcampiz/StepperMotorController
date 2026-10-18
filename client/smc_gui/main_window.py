"""
Main application window.

Thin frontend over smc_core (client refactor step 3): one Board per connection owns
the serial port, the threads, the heartbeat, ESTOP and the session; the BoardBridge
turns its callbacks into Qt signals. This window builds Requests and renders replies.
"""

from PySide6.QtWidgets import (
    QApplication,
    QScrollArea,
    QLabel,
    QMainWindow,
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QSplitter,
    QStatusBar,
    QMessageBox,
    QTabWidget,
)
from PySide6.QtCore import Qt, Signal, Slot, QTimer
from PySide6.QtGui import QAction, QCloseEvent
from typing import Optional
import logging
import os

log = logging.getLogger("main_window")

from smc_gui.connection_panel import ConnectionPanel
from smc_gui.motor_panel import MotorControlPanel
from smc_gui.telemetry_panel import TelemetryPanel
from smc_gui.driver_panel import DriverPanel
from smc_gui.protection_panel import ProtectionPanel
from smc_gui.stepmode_panel import StepModePanel
from smc_gui.controller_panel import ControllerPanel
from smc_gui.motor_profile_panel import MotorProfilePanel
from smc_core import motors
from smc_core import views as V
from smc_gui.fleet_panel import FleetPanel
from smc_core.drive_map import SingleMotorDriver
from smc_gui.graph_panel import GraphPanel
from smc_gui.display_panel import DisplayPanel
from smc_gui.motion_panel import MotionPanel
from smc_gui.sysid_panel import SysIdPanel
from smc_gui.tests_panel import TestsPanel
from smc_gui.status_strip import StatusStrip
from smc_gui.rle_codec import encode_rle_rgb565
from smc_gui.image_loader import LCD_WIDTH, LCD_HEIGHT
from smc_gui.bridge import BoardBridge
from smc_gui import theme
from smc_core import commands as C
from smc_core import views as V
from smc_core import runner
from smc_core.board import Board, BoardConfig, EstopResult, Liveness
from smc_core.codec import Response
from smc_core.transport.base import ITransport


def upload_precondition(data: dict) -> tuple:
    """May a blocking flash upload start? (allowed, reason)

    The firmware watchdog is never disarmed, and UI:FLASH:UPLOAD blocks the Comms
    task for ~13 s at 115200 baud, so the stop it will trigger must be harmless:
    the motor has to be idle and in Hi-Z already.
    """
    if not data:
        return False, "no status snapshot yet (wait for telemetry)"
    state = str(data.get("state", "?"))
    motor = data.get("motor", {}) or {}
    hiz = motor.get("hi_z", motor.get("hiz"))
    if state not in ("IDLE", "ESTOP", "FAULT"):
        return False, f"motor must be idle (state={state})"
    if motor.get("busy", False):
        return False, "motor is busy"
    if not hiz:
        return False, "motor must be in Hi-Z (send MOT:DIS first)"
    return True, "idle, Hi-Z"


# Replies that need more than the generic status-bar note, by command name
_ESTOP = C.syst_estop().name
_REPLY_TAG = {
    C.fault_clear().name: "CLEAR_FAULT",
    C.fault_force().name: "CLEAR_FAULT",
    C.control_mode("OPEN_LOOP").name: "SET_CTRL_MODE",
    C.trim_gains(0, 0).name: "APPLY_TRIM",
    C.trim_limits(0, 0).name: "APPLY_TRIM",
    C.trim_max_pct(1).name: "APPLY_TRIM",
    C.trim_save().name: "APPLY_TRIM",
    C.trim_reset().name: "APPLY_TRIM",
}


class MainWindow(QMainWindow):
    """Main application window."""

    # Signals
    connected = Signal()
    disconnected = Signal()
    status_updated = Signal(dict)

    def __init__(self):
        super().__init__()

        self._board: Optional[Board] = None
        self._bridge: Optional[BoardBridge] = None
        self._port_label = ""
        self._remote_mode_sent = False
        # Dead-man token for the board's heartbeat (CLIENT_REVIEW B: liveness is a core
        # input). Only this window's event loop touches it, through _liveness_timer.
        self._liveness = Liveness(max_age_s=0.2)

        self._setup_ui()
        self._setup_menu()
        self._setup_connections()

    def _setup_ui(self):
        """Set up the user interface."""
        self.setWindowTitle("STM32 Motor Controller")
        # The panels were laid out for this size; below the minimum the faders squash
        self.setMinimumSize(1100, 720)
        self.resize(1440, 900)

        # Central widget
        central = QWidget()
        self.setCentralWidget(central)

        # Main layout
        main_layout = QVBoxLayout(central)
        main_layout.setContentsMargins(8, 8, 8, 8)
        main_layout.setSpacing(8)

        # Connection panel at top, then the one-line board status strip
        self._connection_panel = ConnectionPanel()
        main_layout.addWidget(self._connection_panel)
        self._status_strip = StatusStrip()
        main_layout.addWidget(self._status_strip)

        # Splitter for control and telemetry/graphs
        splitter = QSplitter(Qt.Horizontal)

        # Motor control panel (left)
        self._motor_panel = MotorControlPanel()
        self._motor_panel.setEnabled(False)
        control_scroll = QScrollArea()
        control_scroll.setWidgetResizable(True)
        control_scroll.setFrameShape(QScrollArea.NoFrame)
        control_scroll.setWidget(self._motor_panel)
        splitter.addWidget(control_scroll)

        # Tabbed right panel: Dashboard | Drive | Motor | Encoder | Diagnostics | Fleet
        self._right_tabs = QTabWidget()
        self._right_tabs.setDocumentMode(True)
        self._right_tabs.tabBar().setExpanding(True)        # the six tabs span the width
        self._right_tabs.setUsesScrollButtons(False)        # never scroll off the edge

        self._telemetry_panel = TelemetryPanel()
        self._right_tabs.addTab(self._telemetry_panel, "Dashboard")

        self._motion_panel = MotionPanel()
        self._controller_panel = ControllerPanel("motor")
        self._controller_panel.state_changed.connect(self._on_controller_state)
        drive_page = QWidget()
        drive_v = QVBoxLayout(drive_page)
        drive_v.setContentsMargins(0, 0, 0, 0)
        drive_v.addWidget(self._motion_panel, 1)
        drive_v.addWidget(self._controller_panel)
        drive_scroll = QScrollArea()
        drive_scroll.setWidgetResizable(True)
        drive_scroll.setFrameShape(QScrollArea.NoFrame)
        drive_scroll.setWidget(drive_page)
        self._right_tabs.addTab(drive_scroll, "Drive")
        self._stick_driver = None

        # Motor: the driver (current, regulation), step mode and protection on one page
        self._driver_panel = DriverPanel()
        self._stepmode_panel = StepModePanel()
        self._protection_panel = ProtectionPanel()
        motor_page = QWidget()
        motor_v = QVBoxLayout(motor_page)
        motor_v.setContentsMargins(4, 4, 4, 4)
        self._profile_panel = MotorProfilePanel()
        self._profile_panel.apply_requested.connect(self._on_profile_apply)
        self._profile_panel.export_requested.connect(self._on_profile_export)
        motor_v.addWidget(self._profile_panel)
        motor_v.addWidget(self._driver_panel)
        chip_row = QHBoxLayout()
        chip_row.addWidget(self._stepmode_panel, 1)
        chip_row.addWidget(self._protection_panel, 1)
        motor_v.addLayout(chip_row)
        motor_v.addStretch()
        motor_scroll = QScrollArea()
        motor_scroll.setWidgetResizable(True)
        motor_scroll.setFrameShape(QScrollArea.NoFrame)
        motor_scroll.setWidget(motor_page)
        self._right_tabs.addTab(motor_scroll, "Motor")

        self._graph_panel = GraphPanel()
        self._right_tabs.addTab(self._graph_panel, "Encoder")

        # Diagnostics: tests, system identification and the LCD tools as sub-tabs
        self._tests_panel = TestsPanel()
        self._sysid_panel = SysIdPanel()
        self._display_panel = DisplayPanel()
        diag = QTabWidget()
        diag.addTab(self._tests_panel, "Tests")
        diag.addTab(self._sysid_panel, "System ID")
        diag.addTab(self._display_panel, "Display")
        self._right_tabs.addTab(diag, "Diagnostics")

        # Fleet: four boards (FL/FR/RL/RR) driven as one base, the Pi's role on this PC
        self._fleet_panel = FleetPanel()
        self._right_tabs.addTab(self._fleet_panel, "Fleet")

        splitter.addWidget(self._right_tabs)

        # Control pane wide enough for its fader rows and the fault message; the rest to the tabs
        control_scroll.setMinimumWidth(540)
        splitter.setSizes([560, 880])

        main_layout.addWidget(splitter, 1)

        # Status bar
        self._status_bar = QStatusBar()
        # UI dead-man stamp: the board sends heartbeats only while it is fresh (A2)
        # While the GUI owns the LCD (remote mode) it checks once a second that the board has
        # not been taken back to its menu from the joystick
        self._ui_mode_timer = QTimer(self)
        self._ui_mode_timer.setInterval(1000)
        self._ui_mode_timer.timeout.connect(self._poll_ui_mode)
        self._ui_mode_timer.start()
        self._liveness_timer = QTimer(self)
        self._liveness_timer.setInterval(20)
        self._liveness_timer.timeout.connect(self._on_liveness_tick)
        self._last_status: dict = {}      # last DIAG:STAT? snapshot (upload precondition)
        self.setStatusBar(self._status_bar)
        self._status_bar.showMessage("Disconnected")

    def _setup_menu(self):
        """Set up the menu bar."""
        menubar = self.menuBar()

        # File menu
        file_menu = menubar.addMenu("&File")

        exit_action = QAction("E&xit", self)
        exit_action.setShortcut("Ctrl+Q")
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)

        # Connection menu
        conn_menu = menubar.addMenu("&Connection")

        self._connect_action = QAction("&Connect", self)
        self._connect_action.setShortcut("Ctrl+O")
        self._connect_action.triggered.connect(self._on_connect_clicked)
        conn_menu.addAction(self._connect_action)

        self._disconnect_action = QAction("&Disconnect", self)
        self._disconnect_action.setShortcut("Ctrl+D")
        self._disconnect_action.triggered.connect(self._on_disconnect_clicked)
        self._disconnect_action.setEnabled(False)
        conn_menu.addAction(self._disconnect_action)

        conn_menu.addSeparator()

        refresh_action = QAction("&Refresh Ports", self)
        refresh_action.setShortcut("F5")
        refresh_action.triggered.connect(self._connection_panel.refresh_ports)
        conn_menu.addAction(refresh_action)

        # View menu: dark / light
        view_menu = menubar.addMenu("&View")
        self._light_action = QAction("&Light mode", self)
        self._light_action.setCheckable(True)
        self._light_action.setChecked(theme.MODE == "light")
        self._light_action.setToolTip("Light palette for daylight readability; the choice is kept")
        self._light_action.toggled.connect(self.set_light_mode)
        view_menu.addAction(self._light_action)

        # Debug menu
        debug_menu = menubar.addMenu("&Debug")

        self._debug_action = QAction("&Verbose Logging", self)
        self._debug_action.setCheckable(True)
        self._debug_action.setChecked(
            logging.getLogger().level <= logging.DEBUG)
        self._debug_action.triggered.connect(self._toggle_debug)
        debug_menu.addAction(self._debug_action)

        self._open_log_action = QAction("&Open Log File", self)
        self._open_log_action.triggered.connect(self._open_log_file)
        debug_menu.addAction(self._open_log_action)

        debug_menu.addSeparator()
        self._reinit_motor_action = QAction("&Reinit Motor Driver", self)
        self._reinit_motor_action.setToolTip(
            "Re-initialize powerSTEP01 after motor power cycle")
        self._reinit_motor_action.setEnabled(False)
        self._reinit_motor_action.triggered.connect(self._on_reinit_motor)
        debug_menu.addAction(self._reinit_motor_action)

        # Help menu
        help_menu = menubar.addMenu("&Help")

        about_action = QAction("&About", self)
        about_action.triggered.connect(self._show_about)
        help_menu.addAction(about_action)

    @Slot(bool)
    def set_light_mode(self, light: bool):
        """Switch the palette app-wide and restyle the widgets that style themselves."""
        mode = "light" if light else "dark"
        theme.apply_theme(QApplication.instance(), mode)
        theme.save_mode(mode)
        for panel in (self._status_strip, self._graph_panel, self._tests_panel):
            panel.restyle()
        if self._light_action.isChecked() != light:
            self._light_action.setChecked(light)

    def _setup_connections(self):
        """Set up signal/slot connections."""
        # Connection panel
        self._connection_panel.connect_clicked.connect(self._on_connect_clicked)
        self._connection_panel.disconnect_clicked.connect(self._on_disconnect_clicked)

        # Motor panel commands
        self._motor_panel.command_requested.connect(self._send_command)
        self._motor_panel.live_command_requested.connect(self._send_live_command)
        self._motor_panel.estop_requested.connect(self._on_estop_requested)

        # Driver panel refresh and parameter apply
        self._driver_panel.refresh_requested.connect(self._on_refresh_params)
        self._driver_panel.kval_apply_requested.connect(self._on_kval_apply)
        self._driver_panel.drive_apply_requested.connect(self._on_param_apply)
        self._graph_panel.capture_requested.connect(self._on_capture_requested)
        self._driver_panel.param_apply_requested.connect(self._on_param_apply)
        self._driver_panel.maxspd_changed.connect(self._motor_panel.set_max_speed)
        self._driver_panel.maxspd_changed.connect(self._telemetry_panel.set_max_speed)

        # Protection panel
        self._protection_panel.protection_apply_requested.connect(
            self._on_protection_apply)
        self._protection_panel.refresh_requested.connect(self._on_refresh_params)

        # Step mode panel
        self._stepmode_panel.stepmode_apply_requested.connect(
            self._on_stepmode_apply)
        self._stepmode_panel.refresh_requested.connect(self._on_refresh_params)

        # Motion panel commands
        self._motion_panel.command_requested.connect(self._send_command)

        # System ID panel commands
        self._sysid_panel.command_requested.connect(self._send_command)
        self._sysid_panel.scenario_requested.connect(
            lambda name, params: self._run_scenario(name, params, "SCENARIO_SYSID"))
        self._sysid_panel.scenario_cancel_requested.connect(self._cancel_scenario)
        self._tests_panel.run_requested.connect(
            lambda name, params: self._run_scenario(name, params, "SCENARIO_TESTS"))
        self._tests_panel.cancel_requested.connect(self._cancel_scenario)
        self._sysid_panel.gains_applied.connect(self._refresh_trim_config)

        # Telemetry panel encoder filter commands
        self._telemetry_panel.enc_filter_command.connect(
            self._on_enc_filter_scpi)

        # Display panel image transfer
        self._display_panel.image_send_requested.connect(
            self._on_image_send_requested)
        self._display_panel.image_flash_upload_requested.connect(
            self._on_image_flash_upload_requested)

        # Display panel indicator
        self._display_panel.indicator_send_requested.connect(
            self._on_indicator_send_requested)
        self._display_panel.vcom_requested.connect(self._on_vcom_requested)
        self._display_panel.panel_test_requested.connect(self._on_panel_test_requested)

        # Display panel flash operations
        self._display_panel.flash_info_requested.connect(
            self._on_flash_info_requested)
        self._display_panel.flash_show_requested.connect(
            self._on_flash_show_requested)
        self._display_panel.flash_erase_requested.connect(
            self._on_flash_erase_requested)
        self._display_panel.flash_diag_requested.connect(
            self._on_flash_diag_requested)

        # Dashboard clear fault button
        self._telemetry_panel.clear_fault_requested.connect(
            lambda: self._send_command(C.fault_clear()))

        # Window signals
        self.connected.connect(self._on_connected)
        self.disconnected.connect(self._on_disconnected)

    @Slot()
    def _on_connect_clicked(self):
        """Open the selected serial port and hand it to a Board (non-blocking, R5)."""
        port = self._connection_panel.selected_port
        if not port:
            QMessageBox.warning(self, "Connection Error", "Please select a port.")
            return
        from smc_core.transport import open_transport
        transport = open_transport(port, self._connection_panel.baudrate)
        self.attach_transport(transport, port)

    def attach_transport(self, transport: ITransport, label: str) -> bool:
        """Start a session on any transport (serial port, simulator, test pipe)."""
        period_ms = self._connection_panel.heartbeat_period_ms
        hb = self._connection_panel.heartbeat_enabled
        config = BoardConfig(
            heartbeat=hb,
            heartbeat_period_s=period_ms / 1000.0,
            heartbeat_timeout_ms=max(self._connection_panel.watchdog_ms, 3 * period_ms),
            telemetry_period_ms=self._connection_panel.telemetry_ms,
            telemetry_compact=self._connection_panel.telemetry_compact,
            clear_fault_on_connect=True,      # a comms-timeout ESTOP from the last session
        )
        self._liveness.max_age_s = max(0.2, 4 * period_ms / 1000.0)
        board = Board(transport, config, liveness=self._liveness, name=label)
        self._stick_driver = SingleMotorDriver(board, self._controller_panel.stick)
        bridge = BoardBridge(board, self)
        bridge.connected.connect(lambda identity: self.connected.emit())
        bridge.session.connect(self._on_session)
        bridge.telemetry.connect(self._on_telemetry_updated)
        bridge.event.connect(self._on_async_event)
        bridge.event_gap.connect(self._on_event_gap)
        bridge.board_reset.connect(self._on_board_reset)
        bridge.heartbeat.connect(self._on_heartbeat_ack)
        bridge.heartbeat_lost.connect(self._on_heartbeat_timeout)
        bridge.liveness_stale.connect(self._on_liveness_stale)
        bridge.estop_result.connect(self._on_estop_result)
        bridge.link_lost.connect(self._on_connection_lost)
        bridge.error.connect(lambda msg: self._status_bar.showMessage(f"Board: {msg}", 8000))
        bridge.response.connect(self._on_response_received)
        bridge.job_done.connect(self._on_job_done)
        bridge.progress.connect(self._display_panel.update_progress)

        self._status_bar.showMessage(f"Connecting to {label}...")
        self._liveness.touch()
        self._liveness_timer.start()
        try:
            board.open()
        except Exception as e:  # noqa: BLE001 - the port could not be opened
            self._liveness_timer.stop()
            QMessageBox.critical(self, "Connection Error", str(e))
            self._status_bar.showMessage("Connection failed")
            return False
        self._board, self._bridge, self._port_label = board, bridge, label
        return True

    @Slot()
    def _on_disconnect_clicked(self):
        """Stop the board and close the port. The firmware watchdog stays armed: with
        heartbeats gone the board stops the motor after its timeout."""
        self._liveness_timer.stop()
        if self._board is not None:
            self._board.close()
        self._board = None
        self._bridge = None
        self._remote_mode_sent = False
        self._status_strip.set_link(None)
        self._status_strip.clear()
        self.disconnected.emit()

    def _call(self, req: C.Request, tag: str = "GENERIC") -> None:
        if self._bridge is not None:
            self._bridge.call(req, tag)

    def _queue_refresh(self):
        """The three text refreshes (driver registers, encoder, motor config)."""
        self._call(C.motor_debug(), "REFRESH_MOTOR_DEBUG")
        self._call(C.encoder(), "REFRESH_ENC")
        self._call(C.driver_config(), "REFRESH_MCONFIG")

    @Slot()
    def _on_connected(self):
        """First session ready: populate the panels."""
        self._status_bar.showMessage(f"Connected to {self._port_label}")
        self._status_strip.set_link(self._port_label)

        # Update UI state
        self._connection_panel.set_connected(True)
        self._motor_panel.setEnabled(True)
        self._connect_action.setEnabled(False)
        self._disconnect_action.setEnabled(True)
        self._reinit_motor_action.setEnabled(True)
        self._populate()

    def _populate(self):
        """Queries that fill the panels (also re-run after a board reset)."""
        self._queue_refresh()
        # Motor/encoder config for the Motion panel's unit conversion
        for req in (C.step_mode_query(), C.full_steps_query(), C.encoder_ppr_query(),
                    C.transport_delay_query()):
            self._call(req, "REFRESH_DRV_CONFIG")
        self._call(C.trim_query(), "REFRESH_TRIM_CONFIG")
        self._call(C.enc_filter_query(), "REFRESH_ENC_FILTER")

    @Slot(object)
    def _on_session(self, identity):
        if identity is not None:
            self.setWindowTitle(f"STM32 Motor Controller - {identity.serial} "
                                f"(fw {identity.fw}, protocol {identity.proto})")

    @Slot(object)
    def _on_board_reset(self, info):
        """R3/S-6: the board rebooted. The core re-runs the session setup (JSON mode,
        events, telemetry, watchdog); the panels are refilled once it is ready."""
        self._status_bar.showMessage(
            f"BOARD RESET (boot {info.boot:08X}): session restored, check the motor state", 0)
        self._telemetry_panel.set_link_alarm("#ff8800", "board reset")
        QTimer.singleShot(500, self._populate)

    @Slot(object)
    def _on_event_gap(self, gap):
        msg = (f"{gap.missing} event(s) lost between {gap.before_type or '?'} and "
               f"{gap.after_type}; status re-read")
        log.warning(msg)
        self._status_bar.showMessage(("CRITICAL: " if gap.critical else "") + msg, 10000)

    @Slot()
    def _on_disconnected(self):
        """Handle disconnection."""
        self._status_bar.showMessage("Disconnected")

        # Update UI state
        self._connection_panel.set_connected(False)
        self._motor_panel.setEnabled(False)
        self._connect_action.setEnabled(True)
        self._disconnect_action.setEnabled(False)
        self._reinit_motor_action.setEnabled(False)

        # Clear telemetry, driver, and graphs
        self._telemetry_panel.clear()
        self._driver_panel.clear()
        self._protection_panel.clear()
        self._stepmode_panel.clear()
        self._graph_panel.clear()
        self._motion_panel.clear()

    @Slot()
    def _refresh_trim_config(self):
        """Re-query trim gains so the motion panel reflects applied changes."""
        self._call(C.trim_query(), "REFRESH_TRIM_CONFIG")

    @Slot(object)
    def _send_command(self, command):
        """A panel's Request (or a typed line from a console, validated by the core)."""
        try:
            req = command if isinstance(command, C.Request) else C.from_line(command)
        except C.CommandError as e:
            self._status_bar.showMessage(f"Refused: {e}", 5000)
            return
        if req.name == _ESTOP:
            self._on_estop_requested()      # never through the FIFO (S-1)
            return
        self._call(req, _REPLY_TAG.get(req.name, "GENERIC"))

    @Slot()
    def _on_estop_requested(self):
        """ESTOP fast path: the board purges, writes 0x18 + SYST:ESTOP, drops its queue,
        cancels a running upload within one chunk and reads the latch back."""
        if self._board is None or not self._board.connected:
            self._status_bar.showMessage("ESTOP: not connected", 5000)
            return
        try:
            self._board.estop()
        except Exception as e:  # noqa: BLE001 - show, never swallow silently
            self._motor_panel.show_estop_result(False, f"write failed: {e}")
            self._status_bar.showMessage(f"ESTOP WRITE FAILED: {e}", 10000)
            return
        self._status_bar.showMessage("ESTOP sent, awaiting confirmation...", 5000)

    @Slot(object)
    def _on_estop_result(self, result: EstopResult):
        self._motor_panel.show_estop_result(result.confirmed, result.detail)
        msg = "STOPPED, confirmed" if result.confirmed else f"ESTOP NOT CONFIRMED: {result.detail}"
        if result.dropped:
            msg += f" ({result.dropped} queued command(s) dropped)"
        self._status_bar.showMessage(msg, 5000 if result.confirmed else 0)

    @Slot()
    def _on_liveness_tick(self):
        self._liveness.touch()

    @Slot(bool)
    def _on_liveness_stale(self, stale: bool):
        if stale:
            self._status_bar.showMessage(
                "UI STALLED: heartbeats suppressed, firmware watchdog will stop the motor", 0)
            self._telemetry_panel.set_link_alarm("#ff8800", "UI stall")
        else:
            self._status_bar.showMessage("UI responsive again, heartbeats resumed", 5000)

    @Slot(object)
    def _send_live_command(self, req: C.Request):
        """Coalesced live command (latest-wins, ahead of the FIFO)."""
        if self._bridge is not None:
            self._bridge.latest("live", req)

    def _run_scenario(self, name: str, params: dict, tag: str):
        """Run a core scenario as a board job (the CLI runs the same function)."""
        if self._bridge is None:
            self._on_job_done(tag, RuntimeError("not connected"))
            return
        self._bridge.job(lambda job: runner.run_in_job(name, job, params), tag)

    @Slot()
    def _cancel_scenario(self):
        if self._board is not None:
            self._board.cancel_job()

    @Slot(object)
    def _on_controller_state(self, state):
        """Every controller poll: the driver turns it into MOT:RUN / MOT:STOP (coalesced)."""
        if self._stick_driver is None or self._board is None or not self._board.session_ready:
            return
        if state is None and not self._controller_panel.driving and self._stick_driver.last is None:
            return
        self._stick_driver.tick(state)

    @Slot()
    def _on_capture_requested(self):
        """Encoder tab: a 2 kHz position capture through the Tests tab, which graphs it."""
        self._right_tabs.setCurrentIndex(4)                  # Diagnostics
        self._tests_panel.start("position_capture", {"rpm": 60, "rate_hz": 2000, "settle_s": 1.0})

    @Slot()
    def _on_refresh_params(self):
        """Refresh driver parameters and encoder status."""
        self._queue_refresh()

    def _call_all(self, requests: list, tag: str, last_tag: Optional[str] = None):
        for i, req in enumerate(requests):
            self._call(req, last_tag if (last_tag and i == len(requests) - 1) else tag)

    @Slot(list, object)
    def _on_profile_apply(self, requests: list, profile):
        """A motor profile: its DRV:CFG commands, APPLY (and SAVE), then the panels refresh.
        The profile's rating caps the TVAL faders and its bands steer the controller."""
        if self._bridge is None:
            return
        self._pending_rating = profile.rated_current_a or None   # set once the board has it
        if profile.resonance_bands_rpm:
            self._controller_panel.stick.bands_rpm = list(profile.resonance_bands_rpm)
        self._call_all(requests, "GENERIC", last_tag="APPLY_PARAMS")

    @Slot(str, str)
    def _on_profile_export(self, name: str, title: str):
        """The board's DRV:CFG? as a profile file in the user's folder."""
        if self._bridge is None:
            return
        def job(ctx):
            cfg = V.motor_config(ctx.call(C.driver_config()))
            p = motors.export_profile(cfg, name, title, rated_current_a=self._driver_panel.rated_current(),
                                      resonance_bands_rpm=list(self._controller_panel.stick.bands_rpm))
            return motors.save_profile(p)
        self._bridge.job(job, "EXPORT_PROFILE")

    def _poll_ui_mode(self):
        if self._bridge is not None and self._remote_mode_sent:
            self._call(C.ui_mode_query(), "POLL_UI_MODE")

    @Slot(int)
    def _on_vcom_requested(self, code: int):
        """Panel VCOM, live; coalesced so dragging the slider sends only the newest."""
        if self._bridge is not None:
            self._bridge.latest("lcd_vcom", C.lcd_vcom(code))

    @Slot()
    def _on_panel_test_requested(self):
        """Remote mode, then a black field with a 20 px ruler and white/red/green/blue swatches."""
        if self._bridge is None:
            return
        reqs = [C.ui_mode(remote=True), C.disp_clear(0x0000), C.disp_rect(0, 0, 240, 320, 0xFFFF)]
        for y in range(20, 320, 20):
            colour = 0x07FF if (y // 20) % 2 else 0xF81F
            reqs += [C.disp_line(0, y, 239, y, colour), C.disp_text(4, y + 2, colour, 0x0000, "y=%d" % y)]
        for i, c in enumerate((0xFFFF, 0xF800, 0x07E0, 0x001F)):
            reqs.append(C.disp_rect(180, 4 + i * 14, 50, 10, c, fill=True))
        self._remote_mode_sent = True
        self._call_all(reqs, "GENERIC")

    @Slot(object)
    def _on_kval_apply(self, req: C.Request):
        """KVAL + DRV:CFG:APPLY: update the config and push it to the driver."""
        self._call_all([req, C.cfg_apply()], "APPLY_KVAL")

    @Slot(list)
    def _on_param_apply(self, requests: list):
        """Several parameter requests (DRV:CFG:MOTION, DRV:CFG:APPLY)."""
        self._call_all(requests, "APPLY_PARAMS")

    @Slot(list)
    def _on_protection_apply(self, requests: list):
        """OCD, STALL, FAULT, APPLY: only the final APPLY is tagged APPLY_PROTECTION,
        which clears the dirty flag and refreshes."""
        self._call_all(requests, "GENERIC", last_tag="APPLY_PROTECTION")

    @Slot(object)
    def _on_stepmode_apply(self, req: C.Request):
        """DRV:CFG:STEPMODE + DRV:CFG:APPLY, then the chip is read back (APPLY_STEPMODE)."""
        self._call_all([req, C.cfg_apply()], "GENERIC", last_tag="APPLY_STEPMODE")

    @Slot(object)
    def _on_enc_filter_scpi(self, req: C.Request):
        """Encoder filter request from the telemetry panel."""
        self._call(req, "APPLY_ENC_FILTER")

    # Rows per band for chunked bitmap transfer. Each band is a separate
    # UI:DISP:BITMAP:RLE command and the Comms task cannot be fed while it receives one,
    # so a band must fit well inside the watchdog: 5 rows x 240 px x 2 bytes = 2400 raw
    # bytes, at most ~2.4 KB RLE, ~0.21 s at 115200 baud (20 rows took ~0.84 s).
    BITMAP_BAND_ROWS = 5

    @Slot(bytes, bytes)
    def _on_image_send_requested(self, rgb565_data, rle_data):
        """Send the image as RLE bands in one board job. The job feeds the heartbeat
        between bands and an ESTOP cancels it within one 256-byte chunk."""
        if self._bridge is None:
            self._display_panel.transfer_complete(False, "Not connected")
            return
        w, h = LCD_WIDTH, LCD_HEIGHT
        band_h = self.BITMAP_BAND_ROWS
        bytes_per_row = w * 2
        total = len(rgb565_data)
        bands = []
        row = 0
        while row < h:
            rows = min(band_h, h - row)
            raw = rgb565_data[row * bytes_per_row:(row + rows) * bytes_per_row]
            bands.append((row, rows, encode_rle_rgb565(raw), len(raw)))
            row += rows
        progress = self._bridge.progress

        def send_bands(ctx):
            ctx.call(C.ui_mode(remote=True))
            done = 0
            for y, rows, rle, raw_len in bands:
                resp = ctx.call(C.disp_bitmap_rle(0, y, w, rows, rle, crc=True))
                if not resp.success:
                    return resp
                done += raw_len
                progress.emit(done, total)
            return resp

        self._bridge.job(send_bands, "BITMAP_TRANSFER")

    @Slot(int, bytes, bytes)
    def _on_image_flash_upload_requested(self, slot, rgb565_data, rle_data):
        """Upload raw RGB565 to a flash slot (CRC checked), then show it.

        A3: the watchdog is never disarmed. UI:FLASH:UPLOAD is one ~13 s command the
        firmware cannot be fed during, so the motor must be idle in Hi-Z; the expected
        comms-timeout fault is cleared afterwards.
        """
        if self._bridge is None:
            self._display_panel.transfer_complete(False, "Not connected")
            return
        allowed, reason = upload_precondition(self._board.last_status if self._board else {})
        if not allowed:
            self._display_panel.transfer_complete(False, reason)
            self._status_bar.showMessage(f"Flash upload refused: {reason}", 8000)
            return

        def upload(ctx):
            resp = ctx.call(C.flash_upload(slot, rgb565_data, crc=True))
            ctx.call(C.fault_clear())
            if resp.success:
                ctx.call(C.flash_show(slot))
            return resp

        self._status_bar.showMessage(
            "Flash upload: watchdog stays armed; a comms-timeout stop during the "
            "transfer is expected and cleared afterwards", 8000)
        self._bridge.job(upload, "FLASH_UPLOAD_SLOT")

    @Slot(str, object)
    def _on_job_done(self, tag: str, result):
        if tag == "SCENARIO_SYSID":
            self._sysid_panel.on_scenario_result(result)
            return
        if tag == "SCENARIO_TESTS":
            self._tests_panel.show_result(result)
            return
        if isinstance(result, Exception):
            self._status_bar.showMessage(f"{tag} failed: {result}", 8000)
            self._display_panel.transfer_complete(False, str(result))
            return
        self._on_response_received(tag, tag, result)

    # --- Indicator handler ---

    @Slot(int, int, bool, int, int)
    def _on_indicator_send_requested(self, angle: int, rotation_dir: int,
                                     has_translation: bool, speed_pct: int = 100, rot_pct: int = 100):
        """Indicator: coalesced (latest-wins), REMOTE mode sent once per session."""
        if self._bridge is None:
            self._display_panel.transfer_complete(False, "Not connected")
            return
        if not self._remote_mode_sent:
            self._call(C.ui_mode(remote=True))
            self._remote_mode_sent = True
        self._bridge.latest("indicator", C.disp_indicator(angle, rotation_dir, has_translation, speed_pct, rot_pct))
        self._display_panel.transfer_complete(True, "Indicator sent")

    # --- Flash operation handlers ---

    def _flash_call(self, req: C.Request, tag: str):
        if self._bridge is None:
            self._display_panel.transfer_complete(False, "Not connected")
            return
        self._call(req, tag)

    @Slot()
    def _on_flash_info_requested(self):
        self._flash_call(C.flash_info(), "FLASH_INFO")

    @Slot(int)
    def _on_flash_show_requested(self, slot: int):
        self._flash_call(C.flash_show(slot), "FLASH_SHOW")

    @Slot()
    def _on_flash_diag_requested(self):
        self._flash_call(C.flash_test(), "FLASH_DIAG")

    @Slot()
    def _on_flash_erase_requested(self):
        if self._bridge is not None:
            self._display_panel._set_status("Erasing flash...", "blue")
        self._flash_call(C.flash_erase_all(), "FLASH_ERASE")

    @Slot(str, str, object)
    def _on_response_received(self, tag: str, command: str, response):
        """Route worker responses to appropriate UI handlers."""
        if response is None:
            self._status_bar.showMessage(f"Error: no response to {command}", 5000)
            if tag == "BITMAP_TRANSFER":
                self._display_panel.transfer_complete(False, "No response")
            if tag in ("FLASH_UPLOAD_SLOT",
                       "FLASH_SHOW",
                       "FLASH_ERASE"):
                self._display_panel.transfer_complete(False, "No response")
            if tag == "FLASH_DIAG":
                self._display_panel.show_diag_result(
                    False, "No response from FLASH_TEST")
            return

        if not response.success:
            self._status_bar.showMessage(
                f"Error: {response.error_message}", 5000)
            self._telemetry_panel.show_raw_response(
                command, f"ERROR: {response.error_message}")
            # Handle encoder error specifically
            if tag == "REFRESH_ENC":
                self._telemetry_panel.update_encoder_data(
                    None, response.error_message)
            # Show CLEAR_FAULT failure prominently on motor panel
            if tag == "CLEAR_FAULT":
                self._motor_panel.show_fault_error(
                    f"Cannot clear: {response.error_message}")
            # Mode change failure — revert combo
            if tag == "SET_CTRL_MODE":
                self._motion_panel.update_mode_response(False, {})
            # Step mode refused (MOTOR_BUSY) or not verified: show what the chip runs
            if tag == "APPLY_STEPMODE":
                self._status_bar.showMessage(
                    f"Step mode not applied: {response.error_code or ''} "
                    f"{response.error_message}", 8000)
                self._call(C.step_mode_query(), "REFRESH_DRV_CONFIG")
            # Flash operation failures
            if tag in ("FLASH_UPLOAD_SLOT",
                       "FLASH_SHOW",
                       "FLASH_ERASE",
                       "FLASH_INFO"):
                self._display_panel.transfer_complete(
                    False, f"Flash error: {response.error_message}")
            if tag == "FLASH_DIAG":
                self._display_panel.show_diag_result(
                    False, f"FLASH_TEST FAILED\n\n{response.error_message}")
            return

        if tag == "REFRESH_MOTOR_DEBUG":
            dbg = V.motor_debug(response)
            self._driver_panel.update_driver_params(dbg)
            self._telemetry_panel.show_raw_response("MOTOR_DEBUG", dbg["text"])
            self._status_bar.showMessage("Parameters refreshed", 2000)

        elif tag == "POLL_UI_MODE":
            mode = str((response.data or {}).get("message", "")).upper()
            if mode == "LOCAL" and self._remote_mode_sent:
                self._remote_mode_sent = False
                self._status_bar.showMessage("The board is back on its menu (joystick); "
                                             "the next indicator or drawing takes the LCD again", 6000)
        elif tag == "EXPORT_PROFILE":
            self._status_bar.showMessage("Profile saved: %s" % response, 8000)
            self._profile_panel.reload()
        elif tag == "REFRESH_ENC":
            self._telemetry_panel.update_encoder_data(V.encoder(response))

        elif tag == "REFRESH_MCONFIG":
            cfg = V.motor_config(response)
            self._telemetry_panel.update_motor_config(cfg)
            self._protection_panel.update_protection_params(cfg)
            self._stepmode_panel.update_step_mode(cfg)
            self._motion_panel.update_mconfig(cfg)
            self._driver_panel.update_motor_config(cfg)

        elif tag == "SET_FORMAT":
            self._status_bar.showMessage("Format set", 2000)

        elif tag == "APPLY_KVAL":
            self._status_bar.showMessage("KVAL applied", 2000)
            self._queue_refresh()

        elif tag == "APPLY_PARAMS":
            self._status_bar.showMessage("Parameters applied", 2000)
            if getattr(self, "_pending_rating", None):
                self._driver_panel.set_rated_current(self._pending_rating)
                self._pending_rating = None
            self._queue_refresh()

        elif tag == "APPLY_STEPMODE":
            self._status_bar.showMessage(
                f"Step mode applied: {response.data.get('message', '')}", 4000)
            self._stepmode_panel._dirty = False
            self._call(C.step_mode_query(), "REFRESH_DRV_CONFIG")   # the chip, not the config
            self._queue_refresh()

        elif tag == "APPLY_PROTECTION":
            self._status_bar.showMessage("Protection config applied", 2000)
            # Clear dirty flag so the upcoming refresh updates the panel
            self._protection_panel._dirty = False
            self._queue_refresh()

        elif tag == "CLEAR_FAULT":
            self._status_bar.showMessage("Fault cleared", 2000)
            self._queue_refresh()

        elif tag == "BITMAP_TRANSFER":
            if response.success:
                self._display_panel.transfer_complete(True, "Transfer complete")
                self._status_bar.showMessage("Bitmap sent to LCD", 3000)
            else:
                self._display_panel.transfer_complete(
                    False, f"Transfer failed: {response.error_message}")

        elif tag == "FLASH_INFO":
            info = response.data if response.data else {}
            cap_kb = info.get("capacity_kb", "?")
            max_slots = info.get("max_slots", "?")
            img_kb = LCD_WIDTH * LCD_HEIGHT * 2 / 1024  # 150.0
            self._display_panel.update_flash_info(
                f"Flash: {cap_kb} KB \u2014 {max_slots} full-screen images "
                f"({LCD_WIDTH}\u00d7{LCD_HEIGHT} RGB565, {img_kb:.0f} KB each)")
            if isinstance(max_slots, int):
                self._display_panel.update_max_slots(max_slots)
            self._status_bar.showMessage("Flash info received", 3000)

        elif tag == "FLASH_UPLOAD_SLOT":
            msg = response.data.get("message", "")
            log.info("Flash upload response: %s (raw: %s)", msg, response.raw)
            self._display_panel.transfer_complete(
                True, f"Uploaded: {msg}" if msg else "Image uploaded to flash")
            self._status_bar.showMessage(
                f"Flash upload: {msg}" if msg else "Flash upload complete", 5000)

        elif tag == "FLASH_SHOW":
            self._display_panel.transfer_complete(True, "Displayed from flash")
            self._status_bar.showMessage("Image shown from flash", 3000)

        elif tag == "FLASH_ERASE":
            self._display_panel.transfer_complete(True, "Flash erased")
            self._status_bar.showMessage("Flash erased", 3000)

        elif tag == "FLASH_DIAG":
            msg = response.data.get("message", response.raw) if response.data else response.raw
            self._display_panel.show_diag_result(
                True, f"FLASH_TEST PASSED\n\n{msg}")
            self._status_bar.showMessage("Flash diagnostics passed", 5000)

        elif tag == "REFRESH_DRV_CONFIG":
            if response.data:
                self._motion_panel.update_drv_config(response.data)
                self._motor_panel.update_drv_config(response.data)
                self._stepmode_panel.update_chip_step_mode(response.data)
                self._graph_panel.update_drv_config(response.data)
                self._driver_panel.update_drv_config(response.data)
                self._telemetry_panel.update_drv_config(response.data)
                self._status_strip.set_geometry(response.data.get("full_steps_per_rev"),
                                                response.data.get("encoder_ppr"))

        elif tag == "REFRESH_TRIM_CONFIG":
            if response.data:
                self._motion_panel.update_trim_config(response.data)

        elif tag == "SET_CTRL_MODE":
            if response.success:
                self._status_bar.showMessage("Control mode set", 2000)
                self._motion_panel.update_mode_response(True, response.data or {})
            else:
                self._motion_panel.update_mode_response(False, {})

        elif tag == "APPLY_TRIM":
            self._status_bar.showMessage("Trim config updated", 2000)

        elif tag == "SYSID_ABORT":
            self._status_bar.showMessage("SYSID aborted", 2000)

        elif tag == "REFRESH_ENC_FILTER":
            if response.data:
                self._telemetry_panel.update_filter_config(response.data)

        elif tag == "APPLY_ENC_FILTER":
            self._status_bar.showMessage("Encoder filter updated", 2000)

        else:  # GENERIC
            self._status_bar.showMessage(f"OK: {command}", 2000)
            self._telemetry_panel.show_raw_response(command, response.raw)

    @Slot(dict)
    def _on_telemetry_updated(self, data: dict):
        """Handle telemetry data pushed by the board."""
        self._last_status = data
        self._status_strip.update_status(data)
        self._telemetry_panel.update_data(data)
        self._graph_panel.update_data(data)
        self._motion_panel.update_telemetry(data)
        self.status_updated.emit(data)

        # Update motor panel state from telemetry
        state = data.get("state")
        if state:
            self._motor_panel.update_state(state)

        # Update motor panel Hi-Z state (gates motion buttons)
        motor = data.get("motor", {})
        if isinstance(motor, dict):
            hiz = motor.get("hi_z", motor.get("hiz"))
            if hiz is not None:
                self._motor_panel.update_hiz(hiz)

    @Slot(str)
    def _on_connection_lost(self, error: str):
        """Handle unexpected connection loss."""
        self._status_bar.showMessage(f"Connection lost: {error}", 5000)
        self._on_disconnect_clicked()

    def _show_about(self):
        """Show about dialog."""
        QMessageBox.about(
            self,
            "About STM32 Motor Controller",
            "<h3>STM32 Motor Controller Client</h3>"
            "<p>Version 0.1.0</p>"
            "<p>Cross-platform client for controlling STM32F401RE "
            "stepper motor controller.</p>"
            "<p>Transport: VCP (Serial) / RTT</p>"
            "<p>Protocol: JSON (protocol 2)</p>",
        )

    # -------------------------------------------------------------------------
    # Debug menu handlers
    # -------------------------------------------------------------------------

    _LOG_FILE = "gui_debug.log"

    def _toggle_debug(self, checked: bool):
        """Toggle verbose debug logging."""
        root = logging.getLogger()
        if checked:
            root.setLevel(logging.DEBUG)
            # Add file handler if not already present
            if not any(isinstance(h, logging.FileHandler) for h in root.handlers):
                fh = logging.FileHandler(self._LOG_FILE, mode="w")
                fh.setFormatter(logging.Formatter(
                    "%(asctime)s.%(msecs)03d [%(name)s] %(levelname)s: %(message)s",
                    datefmt="%H:%M:%S"))
                root.addHandler(fh)
            log.info("Debug logging ENABLED — writing to %s", self._LOG_FILE)
            self._status_bar.showMessage(
                f"Debug logging ON — {self._LOG_FILE}", 3000)
        else:
            root.setLevel(logging.INFO)
            log.info("Debug logging DISABLED")
            self._status_bar.showMessage("Debug logging OFF", 3000)

    @Slot()
    def _on_reinit_motor(self):
        """Re-initialize the powerSTEP01 after a motor power cycle, then refresh."""
        if self._bridge is None:
            return
        self._status_bar.showMessage("Re-initializing motor driver...", 3000)
        self._call(C.driver_reinit())
        self._queue_refresh()

    def _open_log_file(self):
        """Open the debug log file in the system editor."""
        path = os.path.abspath(self._LOG_FILE)
        if not os.path.exists(path):
            QMessageBox.information(
                self, "Log File",
                "No log file yet. Enable 'Verbose Logging' first, "
                "then use the app, then open the log.")
            return
        os.startfile(path)

    @Slot(dict)
    def _on_heartbeat_ack(self, data: dict):
        """Handle heartbeat ACK from the board — update link health display."""
        self._telemetry_panel.update_link_health(data)
        self._motion_panel.update_link_health(data)

    @Slot()
    def _on_heartbeat_timeout(self):
        """Three heartbeats without an ack: the firmware watchdog has latched an ESTOP."""
        self._status_bar.showMessage(
            "HEARTBEAT LOST: firmware watchdog stops the motor. Check the link, then CLEAR.", 0)
        self._telemetry_panel.set_link_alarm("#ff0000", "LOST")
        self._motor_panel.show_fault_error(
            "Heartbeat lost: no ack for 3 heartbeats (comms-timeout ESTOP expected)")

    @Slot(str, dict)
    def _on_async_event(self, event_type: str, data: dict):
        """Handle async event from firmware (FAULT, STALL, MOTION_DONE, etc.).

        NOTE: Events update the status bar and log warnings, but do NOT
        change motor_panel button state.  The pushed telemetry
        (every 100 ms) is the sole authority for button enable/disable.
        This prevents transient events (OCD on ENABLE, false stall during
        acceleration) from flickering buttons and blocking jog input.
        """
        status_hex = data.get("status", "?")

        if event_type == "FAULT":
            detail = data.get("detail", "unknown")
            self._status_bar.showMessage(
                f"FAULT: {detail} (status=0x{status_hex})", 10000)
            log.warning("[EVENT] Fault: %s (status=0x%s)", detail, status_hex)

        elif event_type == "FAULT_CLEAR":
            self._status_bar.showMessage("Fault cleared", 3000)
            log.info("[EVENT] Fault cleared (status=0x%s)", status_hex)

        elif event_type == "STALL":
            detail = data.get("detail", "?")
            self._status_bar.showMessage(
                f"STALL: bridge {detail} (status=0x{status_hex})", 10000)
            log.warning("[EVENT] Stall: bridge %s (status=0x%s)",
                        detail, status_hex)

        elif event_type == "STALL_CLEAR":
            self._status_bar.showMessage("Stall cleared", 3000)
            log.info("[EVENT] Stall cleared (status=0x%s)", status_hex)

        elif event_type == "MOTION_DONE":
            self._status_bar.showMessage("Motion complete", 3000)
            log.info("[EVENT] Motion complete (status=0x%s)", status_hex)

        else:
            log.info("[EVENT] Unknown event: %s data=%s", event_type, data)

        # Show in raw data panel for visibility
        self._telemetry_panel.show_raw_response(
            f"EVENT:{event_type}",
            f"!{event_type} " + " ".join(f"{k}={v}" for k, v in data.items()))

    def closeEvent(self, event: QCloseEvent):
        """Stop the motor through the fast path, then close. The watchdog stays armed:
        once the port closes the firmware times out and stays stopped."""
        if self._board is not None and self._board.connected:
            try:
                self._board.close(estop=True)
            except Exception as e:  # noqa: BLE001
                log.warning("ESTOP on close failed: %s", e)
            self._board = None
        self._fleet_panel.shutdown()
        self._on_disconnect_clicked()
        event.accept()
