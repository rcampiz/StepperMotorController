"""
Main application window.

Provides the primary GUI layout with connection, control, and telemetry panels.
"""

from PySide6.QtWidgets import (
    QMainWindow,
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QSplitter,
    QStatusBar,
    QMessageBox,
    QTabWidget,
)
from PySide6.QtCore import Qt, Signal, Slot
from PySide6.QtGui import QAction, QCloseEvent
from typing import Optional
import logging
import os
import time

log = logging.getLogger("main_window")

from gui.connection_panel import ConnectionPanel
from gui.motor_panel import MotorControlPanel
from gui.telemetry_panel import TelemetryPanel
from gui.driver_panel import DriverPanel
from gui.protection_panel import ProtectionPanel
from gui.stepmode_panel import StepModePanel
from gui.graph_panel import GraphPanel
from gui.display_panel import DisplayPanel
from gui.motion_panel import MotionPanel
from gui.sysid_panel import SysIdPanel
from gui.rle_codec import encode_rle_rgb565
from gui.serial_worker import SerialThread, QueuedCommand, CommandTag
from gui.image_loader import LCD_WIDTH, LCD_HEIGHT
from protocol import MotorClient, ResponseFormat
from transport import VcpTransport


class MainWindow(QMainWindow):
    """Main application window."""

    # Signals
    connected = Signal()
    disconnected = Signal()
    status_updated = Signal(dict)

    def __init__(self):
        super().__init__()

        self._client: Optional[MotorClient] = None
        self._transport: Optional[VcpTransport] = None
        self._serial_thread: Optional[SerialThread] = None
        self._remote_mode_sent = False

        self._setup_ui()
        self._setup_menu()
        self._setup_connections()

    def _setup_ui(self):
        """Set up the user interface."""
        self.setWindowTitle("STM32 Motor Controller")
        self.setMinimumSize(800, 600)

        # Central widget
        central = QWidget()
        self.setCentralWidget(central)

        # Main layout
        main_layout = QVBoxLayout(central)
        main_layout.setContentsMargins(8, 8, 8, 8)
        main_layout.setSpacing(8)

        # Connection panel at top
        self._connection_panel = ConnectionPanel()
        main_layout.addWidget(self._connection_panel)

        # Splitter for control and telemetry/graphs
        splitter = QSplitter(Qt.Horizontal)

        # Motor control panel (left)
        self._motor_panel = MotorControlPanel()
        self._motor_panel.setEnabled(False)
        splitter.addWidget(self._motor_panel)

        # Tabbed right panel (Dashboard + Graphs)
        self._right_tabs = QTabWidget()

        self._telemetry_panel = TelemetryPanel()
        self._right_tabs.addTab(self._telemetry_panel, "Dashboard")

        self._driver_panel = DriverPanel()
        self._right_tabs.addTab(self._driver_panel, "Driver")

        self._protection_panel = ProtectionPanel()
        self._right_tabs.addTab(self._protection_panel, "Protection")

        self._stepmode_panel = StepModePanel()
        self._right_tabs.addTab(self._stepmode_panel, "Step Mode")

        self._graph_panel = GraphPanel()
        self._right_tabs.addTab(self._graph_panel, "Telemetry Graphs")

        self._motion_panel = MotionPanel()
        self._right_tabs.addTab(self._motion_panel, "Motion")

        self._sysid_panel = SysIdPanel()
        self._right_tabs.addTab(self._sysid_panel, "System ID")

        self._display_panel = DisplayPanel()
        self._right_tabs.addTab(self._display_panel, "Display")

        splitter.addWidget(self._right_tabs)

        # Set initial splitter sizes (50% control, 50% tabs)
        splitter.setSizes([480, 400])

        main_layout.addWidget(splitter, 1)

        # Status bar
        self._status_bar = QStatusBar()
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

        # Format menu
        format_menu = menubar.addMenu("F&ormat")

        self._ascii_action = QAction("&ASCII", self)
        self._ascii_action.setCheckable(True)
        self._ascii_action.setChecked(True)
        self._ascii_action.triggered.connect(lambda: self._set_format(ResponseFormat.ASCII))
        format_menu.addAction(self._ascii_action)

        self._json_action = QAction("&JSON", self)
        self._json_action.setCheckable(True)
        self._json_action.triggered.connect(lambda: self._set_format(ResponseFormat.JSON))
        format_menu.addAction(self._json_action)

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

    def _setup_connections(self):
        """Set up signal/slot connections."""
        # Connection panel
        self._connection_panel.connect_clicked.connect(self._on_connect_clicked)
        self._connection_panel.disconnect_clicked.connect(self._on_disconnect_clicked)

        # Motor panel commands
        self._motor_panel.command_requested.connect(self._send_command)
        self._motor_panel.live_command_requested.connect(self._send_live_command)

        # Driver panel refresh and parameter apply
        self._driver_panel.refresh_requested.connect(self._on_refresh_params)
        self._driver_panel.kval_apply_requested.connect(self._on_kval_apply)
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
        self._sysid_panel.tagged_command_requested.connect(
            self._on_sysid_tagged_command)
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
            lambda: self._send_command("SYST:FAULT:CLEAR"))

        # Window signals
        self.connected.connect(self._on_connected)
        self.disconnected.connect(self._on_disconnected)

    @Slot()
    def _on_connect_clicked(self):
        """Handle connect button click."""
        port = self._connection_panel.selected_port
        baudrate = self._connection_panel.baudrate

        if not port:
            QMessageBox.warning(self, "Connection Error", "Please select a port.")
            return

        try:
            self._transport = VcpTransport(port=port, baudrate=baudrate)
            self._client = MotorClient(self._transport)

            self._status_bar.showMessage(f"Connecting to {port}...")

            if not self._client.connect():
                raise Exception("Connection failed")

            log.info("Port opened, waiting for firmware banner...")

            # Read firmware banner to verify comms link is alive
            time.sleep(0.5)
            banner_ok = False
            for _ in range(10):
                line = self._transport.recv_line(timeout_ms=200)
                if line is None:
                    break
                log.info("FW banner: %s", line)
                banner_ok = True

            if not banner_ok:
                # No banner — firmware may already be past boot.
                # Probe with PING (empty lines produce no response
                # because the firmware skips CR/LF when buffer is empty).
                log.warning("No banner received — probing with PING...")
                self._transport.flush_input()
                self._transport.send_line("DIAG:PING 0")
                # Read echo + PONG response (up to 2 lines)
                probe = self._transport.recv_line(timeout_ms=2000)
                if probe is not None:
                    log.info("Probe response: %r", probe)
                    # Read PONG if echo came first
                    if probe.startswith("DIAG:PING"):
                        pong = self._transport.recv_line(timeout_ms=1000)
                        if pong:
                            log.info("Probe PONG: %r", pong)
                else:
                    log.error("Firmware not responding at all. "
                              "Try resetting the board.")
                    raise Exception(
                        "Firmware not responding — try resetting the board "
                        "(press black RESET button or power-cycle).")

            # Drain any remaining banner/probe bytes
            self._transport.flush_input()

            # Set JSON format with retry
            for attempt in range(3):
                try:
                    log.debug("SET_FORMAT attempt %d", attempt + 1)
                    self._client.set_format(ResponseFormat.JSON)
                    break
                except Exception as e:
                    log.debug("SET_FORMAT failed: %s", e)
                    time.sleep(0.3)
                    self._transport.flush_input()
            else:
                raise Exception("Failed to set JSON format after retries")

            self._json_action.setChecked(True)
            self._ascii_action.setChecked(False)

            # Baud auto-negotiation disabled — ST-LINK V2-1 VCP drops bytes
            # above 115200 during bulk binary transfers.  The SET_BAUD command
            # is still available for manual use if a different adapter is used.
            self._status_bar.showMessage(f"Connected to {port}")

            # Create and start worker thread — after this point, ALL serial
            # communication must go through the worker's command queue
            self._serial_thread = SerialThread(self._client)
            worker = self._serial_thread.worker
            worker.response_received.connect(self._on_response_received)
            worker.telemetry_updated.connect(self._on_telemetry_updated)
            worker.connection_error.connect(self._on_connection_lost)
            worker.heartbeat_ack_received.connect(self._on_heartbeat_ack)
            worker.heartbeat_timeout.connect(self._on_heartbeat_timeout)
            worker.event_received.connect(self._on_async_event)
            worker.bitmap_progress.connect(self._display_panel.update_progress)

            # Start heartbeat producer BEFORE starting the thread so the
            # flag is set when run() begins its first loop iteration.
            if self._connection_panel.heartbeat_enabled:
                period_ms = self._connection_panel.heartbeat_period_ms
                self._serial_thread.start_heartbeat(period_ms / 1000.0)

            self._serial_thread.start()
            self._serial_thread.start_polling()

            self.connected.emit()

        except Exception as e:
            QMessageBox.critical(self, "Connection Error", str(e))
            self._status_bar.showMessage("Connection failed")
            self._serial_thread = None
            self._client = None
            self._transport = None

    @Slot()
    def _on_disconnect_clicked(self):
        """Handle disconnect button click."""
        # Stop worker thread FIRST so main thread can safely use the port
        if self._serial_thread:
            self._serial_thread.stop_heartbeat()
            self._serial_thread.stop()
            self._serial_thread = None
            self._remote_mode_sent = False

        # Close the serial port but do NOT disarm the MCU heartbeat watchdog.
        # If heartbeat was enabled, the MCU will notice the missing heartbeats
        # and enter safe state (stop motor) after the timeout expires.
        if self._client and self._client.is_connected():
            try:
                self._client.disconnect()
            except Exception:
                pass

        self._client = None
        self._transport = None
        self.disconnected.emit()

    @Slot()
    def _on_connected(self):
        """Handle successful connection."""
        port = self._connection_panel.selected_port
        self._status_bar.showMessage(f"Connected to {port}")

        # Update UI state
        self._connection_panel.set_connected(True)
        self._motor_panel.setEnabled(True)
        self._connect_action.setEnabled(False)
        self._disconnect_action.setEnabled(True)
        self._reinit_motor_action.setEnabled(True)

        if self._serial_thread:
            # Clear any latched faults (e.g. comms timeout from previous
            # session where heartbeat watchdog fired after USB disconnect)
            self._serial_thread.worker.queue_command(
                QueuedCommand("SYST:FAULT:CLEAR", CommandTag.CLEAR_FAULT))

            # Request initial parameter refresh via worker
            self._serial_thread.queue_refresh()

            # Queue SET_HEARTBEAT AFTER refresh commands so the MCU
            # watchdog only starts once heartbeats are already flowing.
            # (The worker sends heartbeats between command processing
            # cycles, so the watchdog gets fed while refresh runs.)
            if self._connection_panel.heartbeat_enabled:
                period_ms = self._connection_panel.heartbeat_period_ms
                timeout_ms = max(2000, period_ms * 6)
                log.info("Queueing heartbeat watchdog: period=%dms, "
                         "MCU timeout=%dms", period_ms, timeout_ms)
                self._serial_thread.worker.queue_command(
                    QueuedCommand(
                        f"SYST:HB:TIMEOUT {timeout_ms}",
                        CommandTag.GENERIC))

            # Enable async events (all types) after setup is complete
            self._serial_thread.worker.queue_command(
                QueuedCommand("SYST:EVT:EN", CommandTag.GENERIC))

            # Query motor/encoder config for Motion panel unit conversion
            for cmd in ("DRV:STEP_MODE?", "DRV:FULL_STEPS?",
                        "DRV:ENC_PPR?", "SYST:DELAY?"):
                self._serial_thread.worker.queue_command(
                    QueuedCommand(cmd, CommandTag.REFRESH_DRV_CONFIG))

            # Query trim gains for Motion panel tuning controls
            self._serial_thread.worker.queue_command(
                QueuedCommand("CTRL:TRIM?", CommandTag.REFRESH_TRIM_CONFIG))

            # Query encoder filter config to populate Dashboard controls
            self._serial_thread.worker.queue_command(
                QueuedCommand("CTRL:ENC:FILT?", CommandTag.REFRESH_ENC_FILTER))

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
        if not self._serial_thread:
            return
        self._serial_thread.worker.queue_command(
            QueuedCommand("CTRL:TRIM?", CommandTag.REFRESH_TRIM_CONFIG))

    @Slot(str)
    def _send_command(self, command: str):
        """Send a command to the device via worker thread."""
        if not self._serial_thread:
            return

        # Route known commands through tagged path for response handling
        if command == "SYST:FAULT:CLEAR":
            self._serial_thread.worker.queue_command(
                QueuedCommand("SYST:FAULT:CLEAR", CommandTag.CLEAR_FAULT))
        elif command == "SYST:FAULT:FORCE":
            self._serial_thread.worker.queue_command(
                QueuedCommand("SYST:FAULT:FORCE", CommandTag.CLEAR_FAULT))
        elif command.startswith("CTRL:MODE"):
            self._serial_thread.worker.queue_command(
                QueuedCommand(command, CommandTag.SET_CTRL_MODE))
        elif command.startswith("CTRL:TRIM:"):
            self._serial_thread.worker.queue_command(
                QueuedCommand(command, CommandTag.APPLY_TRIM))
        else:
            self._serial_thread.send_command(command)

    @Slot(str)
    def _send_live_command(self, command: str):
        """Send a coalesced live command (latest-wins, highest priority)."""
        if self._serial_thread:
            self._serial_thread.set_live_command(command)

    def _on_sysid_tagged_command(self, command: str, tag_name: str):
        """Send a tagged command from the SysIdPanel."""
        if not self._serial_thread:
            return
        tag = CommandTag[tag_name]
        multiline = (tag_name == "SYSID_DATA")
        max_lines = 50 if multiline else 20
        self._serial_thread.worker.queue_command(
            QueuedCommand(command, tag, multiline=multiline,
                          max_lines=max_lines))

    @Slot()
    def _on_refresh_params(self):
        """Refresh driver parameters and encoder status via worker."""
        if self._serial_thread:
            self._serial_thread.queue_refresh()

    @Slot(str)
    def _on_kval_apply(self, cmd_str: str):
        """Send MCONFIG_KVAL + MCONFIG_APPLY to update config and push to hardware."""
        if self._serial_thread:
            self._serial_thread.worker.queue_command(
                QueuedCommand(cmd_str, CommandTag.APPLY_KVAL))
            self._serial_thread.worker.queue_command(
                QueuedCommand("DRV:CFG:APPLY", CommandTag.APPLY_KVAL))

    @Slot(list)
    def _on_param_apply(self, commands: list):
        """Send multiple parameter commands (ACCEL, DECEL, MAXSPD) via worker."""
        if not self._serial_thread:
            return
        for cmd in commands:
            self._serial_thread.worker.queue_command(
                QueuedCommand(cmd, CommandTag.APPLY_PARAMS))

    @Slot(list)
    def _on_protection_apply(self, commands: list):
        """Send protection parameter commands (OCD, STALL, FAULT, APPLY) via worker.

        Intermediate commands are tagged GENERIC so they don't trigger
        auto-refresh. Only the final MCONFIG_APPLY is tagged
        APPLY_PROTECTION, which clears the dirty flag and refreshes.
        """
        if not self._serial_thread:
            return
        for cmd in commands[:-1]:
            self._serial_thread.worker.queue_command(
                QueuedCommand(cmd, CommandTag.GENERIC))
        if commands:
            self._serial_thread.worker.queue_command(
                QueuedCommand(commands[-1], CommandTag.APPLY_PROTECTION))

    @Slot(str)
    def _on_stepmode_apply(self, cmd_str: str):
        """Send MCONFIG_STEPMODE + MCONFIG_APPLY to update step mode."""
        if self._serial_thread:
            self._serial_thread.worker.queue_command(
                QueuedCommand(cmd_str, CommandTag.APPLY_PARAMS))
            self._serial_thread.worker.queue_command(
                QueuedCommand("DRV:CFG:APPLY", CommandTag.APPLY_PARAMS))

    @Slot(str)
    def _on_enc_filter_scpi(self, command: str):
        """Send encoder filter SCPI command from telemetry panel."""
        if self._serial_thread:
            self._serial_thread.worker.queue_command(
                QueuedCommand(command, CommandTag.APPLY_ENC_FILTER))

    # Rows per band for chunked bitmap transfer.  Each band is a separate
    # DISP_BITMAP_RLE command so heartbeats can flow between bands.
    # 20 rows × 240px × 2 bytes = 9600 raw bytes per band.
    BITMAP_BAND_ROWS = 20

    @Slot(bytes, bytes)
    def _on_image_send_requested(self, rgb565_data, rle_data):
        """Handle image send request from display panel.

        Splits the image into horizontal bands, RLE-compresses each band
        independently, and queues as DISP_BITMAP_RLE commands so the serial
        worker can send heartbeats between bands.
        """
        if not self._serial_thread:
            self._display_panel.transfer_complete(False, "Not connected")
            return

        worker = self._serial_thread.worker
        w, h = LCD_WIDTH, LCD_HEIGHT
        total_bytes = len(rgb565_data)
        band_h = self.BITMAP_BAND_ROWS
        bytes_per_row = w * 2

        # Switch to REMOTE mode first
        worker.queue_command(
            QueuedCommand("UI:MODE REMOTE", CommandTag.GENERIC))

        # Queue one DISP_BITMAP_RLE per band
        row = 0
        while row < h:
            rows_this_band = min(band_h, h - row)
            data_offset = row * bytes_per_row
            band_raw = rgb565_data[
                data_offset:data_offset + rows_this_band * bytes_per_row]
            band_rle = encode_rle_rgb565(band_raw)

            def make_transfer(bx, by, bw, bh, rle_bytes, accumulated_offset,
                              raw_len):
                """Create a closure that sends one RLE band."""
                def do_transfer(client):
                    resp = client.disp_bitmap_rle(
                        bx, by, bw, bh, rle_bytes, use_crc=True)
                    worker.bitmap_progress.emit(
                        accumulated_offset + raw_len, total_bytes)
                    return resp
                return do_transfer

            accumulated = data_offset
            is_last = (row + rows_this_band >= h)
            tag = CommandTag.BITMAP_TRANSFER if is_last else CommandTag.GENERIC

            worker.queue_command(
                QueuedCommand(
                    command=f"UI:DISP:BITMAP:RLE 0 {row} {w} {rows_this_band} "
                            f"{len(band_rle)}",
                    tag=tag,
                    callable=make_transfer(0, row, w, rows_this_band,
                                           band_rle, accumulated,
                                           len(band_raw)),
                )
            )
            row += rows_this_band

    @Slot(int, bytes, bytes)
    def _on_image_flash_upload_requested(self, slot, rgb565_data, rle_data):
        """Handle image flash upload request from display panel.

        Uploads raw RGB565 image data to a flash slot via FLASH_UPLOAD
        with CRC integrity verification.  Temporarily disables heartbeats
        during the blocking upload to prevent MCU watchdog timeout.
        After successful upload, auto-issues FLASH_SHOW to display the
        stored image on the LCD.
        """
        if not self._serial_thread:
            self._display_panel.transfer_complete(False, "Not connected")
            return

        worker = self._serial_thread.worker

        # No UI_MODE REMOTE here — flash upload writes to flash, not LCD.
        # FLASH_SHOW (queued after upload success) sets REMOTE mode internally.

        # Capture heartbeat timeout for re-enable after upload
        hb_timeout_ms = max(2000, int(worker._heartbeat_interval * 6000))

        def make_upload(s, data, w, hb_ms):
            def do_upload(client):
                # Disable heartbeat watchdog for the duration of the upload
                # (the callable blocks the worker thread, so no heartbeats
                # can be sent during the upload)
                client.send_command("SYST:HB:TIMEOUT 0")
                try:
                    resp = client.flash_upload(
                        s, data, use_crc=True,
                        progress_cb=lambda sent, total:
                            w.bitmap_progress.emit(sent, total))
                finally:
                    # Re-enable heartbeat watchdog
                    client.send_command(f"SYST:HB:TIMEOUT {hb_ms}")
                return resp
            return do_upload

        worker.queue_command(
            QueuedCommand(
                command=f"UI:FLASH:UPLOAD {slot}",
                tag=CommandTag.FLASH_UPLOAD_SLOT,
                callable=make_upload(slot, rgb565_data, worker, hb_timeout_ms),
            )
        )

    # --- Indicator handler ---

    @Slot(int, int, bool)
    def _on_indicator_send_requested(self, angle: int, rotation_dir: int,
                                     has_translation: bool):
        """Handle indicator send request from display panel."""
        if not self._serial_thread:
            self._display_panel.transfer_complete(False, "Not connected")
            return

        worker = self._serial_thread.worker

        # Ensure REMOTE mode (send only once per session)
        if not self._remote_mode_sent:
            worker.queue_command(
                QueuedCommand("UI:MODE REMOTE", CommandTag.GENERIC))
            self._remote_mode_sent = True

        # Use coalesced indicator path (latest-wins, no queue backlog)
        trans_int = 1 if has_translation else 0
        worker.set_indicator_command(
            f"UI:DISP:INDICATOR {angle} {rotation_dir} {trans_int}")

        self._display_panel.transfer_complete(True, "Indicator sent")

    # --- Flash operation handlers ---

    @Slot()
    def _on_flash_info_requested(self):
        """Handle Flash Info button click."""
        if not self._serial_thread:
            self._display_panel.transfer_complete(False, "Not connected")
            return
        self._serial_thread.worker.queue_command(
            QueuedCommand("UI:FLASH:INFO", CommandTag.FLASH_INFO))

    @Slot(int)
    def _on_flash_show_requested(self, slot: int):
        """Handle Show from Flash button click."""
        if not self._serial_thread:
            self._display_panel.transfer_complete(False, "Not connected")
            return
        self._serial_thread.worker.queue_command(
            QueuedCommand(f"UI:FLASH:SHOW {slot}", CommandTag.FLASH_SHOW))

    @Slot()
    def _on_flash_diag_requested(self):
        """Handle Flash Diagnostics button click — sends FLASH_TEST."""
        if not self._serial_thread:
            self._display_panel.transfer_complete(False, "Not connected")
            return
        self._serial_thread.worker.queue_command(
            QueuedCommand("UI:FLASH:TEST", CommandTag.FLASH_DIAG))

    @Slot()
    def _on_flash_erase_requested(self):
        """Handle Erase Flash button click."""
        if not self._serial_thread:
            self._display_panel.transfer_complete(False, "Not connected")
            return
        self._display_panel._set_status("Erasing flash...", "blue")

        def do_erase(client):
            return client.flash_erase_all(timeout_ms=60000)

        self._serial_thread.worker.queue_command(
            QueuedCommand(
                command="UI:FLASH:ERASE_ALL",
                tag=CommandTag.FLASH_ERASE,
                callable=do_erase,
            )
        )

    @Slot(str, str, object)
    def _on_response_received(self, tag: str, command: str, response):
        """Route worker responses to appropriate UI handlers."""
        if response is None:
            self._status_bar.showMessage(f"Error: no response to {command}", 5000)
            if tag == CommandTag.BITMAP_TRANSFER.name:
                self._display_panel.transfer_complete(False, "No response")
            if tag in (CommandTag.FLASH_UPLOAD_SLOT.name,
                       CommandTag.FLASH_SHOW.name,
                       CommandTag.FLASH_ERASE.name):
                self._display_panel.transfer_complete(False, "No response")
            if tag == CommandTag.FLASH_DIAG.name:
                self._display_panel.show_diag_result(
                    False, "No response from FLASH_TEST")
            return

        if not response.success:
            self._status_bar.showMessage(
                f"Error: {response.error_message}", 5000)
            self._telemetry_panel.show_raw_response(
                command, f"ERROR: {response.error_message}")
            # Handle encoder error specifically
            if tag == CommandTag.REFRESH_ENC.name:
                self._telemetry_panel.update_encoder_data(
                    None, response.error_message)
            # Show CLEAR_FAULT failure prominently on motor panel
            if tag == CommandTag.CLEAR_FAULT.name:
                self._motor_panel.show_fault_error(
                    f"Cannot clear: {response.error_message}")
            # Mode change failure — revert combo
            if tag == CommandTag.SET_CTRL_MODE.name:
                self._motion_panel.update_mode_response(False, {})
            # Flash operation failures
            if tag in (CommandTag.FLASH_UPLOAD_SLOT.name,
                       CommandTag.FLASH_SHOW.name,
                       CommandTag.FLASH_ERASE.name,
                       CommandTag.FLASH_INFO.name):
                self._display_panel.transfer_complete(
                    False, f"Flash error: {response.error_message}")
            if tag == CommandTag.FLASH_DIAG.name:
                self._display_panel.show_diag_result(
                    False, f"FLASH_TEST FAILED\n\n{response.error_message}")
            return

        if tag == CommandTag.REFRESH_MOTOR_DEBUG.name:
            self._driver_panel.update_driver_params(response.raw)
            self._telemetry_panel.show_raw_response("MOTOR_DEBUG", response.raw)
            self._status_bar.showMessage("Parameters refreshed", 2000)

        elif tag == CommandTag.REFRESH_ENC.name:
            self._telemetry_panel.update_encoder_data(response.raw)

        elif tag == CommandTag.REFRESH_MCONFIG.name:
            self._telemetry_panel.update_motor_config(response.raw)
            self._protection_panel.update_protection_params(response.raw)
            self._stepmode_panel.update_step_mode(response.raw)
            self._motion_panel.update_mconfig(response.raw)

        elif tag == CommandTag.SET_FORMAT.name:
            self._status_bar.showMessage("Format set", 2000)

        elif tag == CommandTag.APPLY_KVAL.name:
            self._status_bar.showMessage("KVAL applied", 2000)
            if self._serial_thread:
                self._serial_thread.queue_refresh()

        elif tag == CommandTag.APPLY_PARAMS.name:
            self._status_bar.showMessage("Parameters applied", 2000)
            if self._serial_thread:
                self._serial_thread.queue_refresh()

        elif tag == CommandTag.APPLY_PROTECTION.name:
            self._status_bar.showMessage("Protection config applied", 2000)
            # Clear dirty flag so the upcoming refresh updates the panel
            self._protection_panel._dirty = False
            if self._serial_thread:
                self._serial_thread.queue_refresh()

        elif tag == CommandTag.CLEAR_FAULT.name:
            self._status_bar.showMessage("Fault cleared", 2000)
            if self._serial_thread:
                self._serial_thread.queue_refresh()

        elif tag == CommandTag.BITMAP_TRANSFER.name:
            if response.success:
                self._display_panel.transfer_complete(True, "Transfer complete")
                self._status_bar.showMessage("Bitmap sent to LCD", 3000)
            else:
                self._display_panel.transfer_complete(
                    False, f"Transfer failed: {response.error_message}")

        elif tag == CommandTag.FLASH_INFO.name:
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

        elif tag == CommandTag.FLASH_UPLOAD_SLOT.name:
            msg = response.data.get("message", "")
            log.info("Flash upload response: %s (raw: %s)", msg, response.raw)
            self._display_panel.transfer_complete(
                True, f"Uploaded: {msg}" if msg else "Image uploaded to flash")
            self._status_bar.showMessage(
                f"Flash upload: {msg}" if msg else "Flash upload complete", 5000)
            # Auto-show the uploaded image on the LCD
            slot_str = command.split()[1] if len(command.split()) > 1 else "0"
            if self._serial_thread:
                self._serial_thread.worker.queue_command(
                    QueuedCommand(
                        f"UI:FLASH:SHOW {slot_str}", CommandTag.FLASH_SHOW))

        elif tag == CommandTag.FLASH_SHOW.name:
            self._display_panel.transfer_complete(True, "Displayed from flash")
            self._status_bar.showMessage("Image shown from flash", 3000)

        elif tag == CommandTag.FLASH_ERASE.name:
            self._display_panel.transfer_complete(True, "Flash erased")
            self._status_bar.showMessage("Flash erased", 3000)

        elif tag == CommandTag.FLASH_DIAG.name:
            msg = response.data.get("message", response.raw) if response.data else response.raw
            self._display_panel.show_diag_result(
                True, f"FLASH_TEST PASSED\n\n{msg}")
            self._status_bar.showMessage("Flash diagnostics passed", 5000)

        elif tag == CommandTag.REFRESH_DRV_CONFIG.name:
            if response.data:
                self._motion_panel.update_drv_config(response.data)
                self._motor_panel.update_drv_config(response.data)
                self._graph_panel.update_drv_config(response.data)
                self._driver_panel.update_drv_config(response.data)
                self._telemetry_panel.update_drv_config(response.data)

        elif tag == CommandTag.REFRESH_TRIM_CONFIG.name:
            if response.data:
                self._motion_panel.update_trim_config(response.data)

        elif tag == CommandTag.SET_CTRL_MODE.name:
            if response.success:
                self._status_bar.showMessage("Control mode set", 2000)
                self._motion_panel.update_mode_response(True, response.data or {})
            else:
                self._motion_panel.update_mode_response(False, {})

        elif tag == CommandTag.APPLY_TRIM.name:
            self._status_bar.showMessage("Trim config updated", 2000)

        elif tag == CommandTag.SYSID_START.name:
            self._sysid_panel.on_sysid_start_response(
                response.success, response.data)

        elif tag == CommandTag.SYSID_STATUS.name:
            self._sysid_panel.on_sysid_status_response(
                response.success, response.data or response.raw)

        elif tag == CommandTag.SYSID_DATA.name:
            self._sysid_panel.on_sysid_data_response(
                response.success, response.raw)

        elif tag == CommandTag.SYSID_ABORT.name:
            self._status_bar.showMessage("SYSID aborted", 2000)

        elif tag == CommandTag.REFRESH_ENC_FILTER.name:
            if response.data:
                self._telemetry_panel.update_filter_config(response.data)

        elif tag == CommandTag.APPLY_ENC_FILTER.name:
            self._status_bar.showMessage("Encoder filter updated", 2000)

        else:  # GENERIC
            self._status_bar.showMessage(f"OK: {command}", 2000)
            self._telemetry_panel.show_raw_response(command, response.raw)

    @Slot(dict)
    def _on_telemetry_updated(self, data: dict):
        """Handle telemetry data from worker polling."""
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

    def _set_format(self, fmt: ResponseFormat):
        """Set response format."""
        if not self._serial_thread:
            # Just update UI state
            self._ascii_action.setChecked(fmt == ResponseFormat.ASCII)
            self._json_action.setChecked(fmt == ResponseFormat.JSON)
            return

        # Route through worker
        self._serial_thread.worker.queue_command(
            QueuedCommand(f"FMT {fmt.value}", CommandTag.SET_FORMAT)
        )
        # Update UI checkboxes optimistically
        self._ascii_action.setChecked(fmt == ResponseFormat.ASCII)
        self._json_action.setChecked(fmt == ResponseFormat.JSON)

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
            "<p>Protocol: ASCII / JSON</p>",
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
        """Re-initialize the powerSTEP01 after a motor power cycle.

        Sends MOTOR_REINIT which re-runs the driver init sequence and
        re-applies the saved motor config, then refreshes all panels.
        """
        if not self._serial_thread:
            return
        self._status_bar.showMessage("Re-initializing motor driver...", 3000)
        self._serial_thread.worker.queue_command(
            QueuedCommand("DRV:REINIT", CommandTag.GENERIC))
        # Refresh all panels to pick up the restored config
        self._serial_thread.queue_refresh()

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
        """Handle heartbeat ACK from worker — update link health display."""
        self._telemetry_panel.update_link_health(data)
        self._motion_panel.update_link_health(data)

    @Slot()
    def _on_heartbeat_timeout(self):
        """Handle heartbeat timeout — MCU has entered safe state."""
        self._status_bar.showMessage(
            "COMMS TIMEOUT: Motor stopped. Send CLEAR_FAULT to recover.")

    @Slot(str, dict)
    def _on_async_event(self, event_type: str, data: dict):
        """Handle async event from firmware (FAULT, STALL, MOTION_DONE, etc.).

        NOTE: Events update the status bar and log warnings, but do NOT
        change motor_panel button state.  The GET_STATUS telemetry poll
        (every 100ms) is the sole authority for button enable/disable.
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
        """Handle window close - queue cleanup through worker, then disconnect."""
        if self._serial_thread:
            # Queue cleanup commands through the worker (don't bypass it)
            self._serial_thread.send_command("SYST:EVT:DIS")
            self._serial_thread.send_command("SYST:HB:TIMEOUT 0")
            self._serial_thread.send_command("SYST:ESTOP")
            time.sleep(0.2)  # Brief pause to let commands drain
        self._on_disconnect_clicked()
        event.accept()
