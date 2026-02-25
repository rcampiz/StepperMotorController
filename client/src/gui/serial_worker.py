"""
Background serial worker thread.

Handles all serial communication off the main GUI thread to prevent blocking.
"""

from PySide6.QtCore import QObject, QThread, Signal, Slot
from typing import Optional
from dataclasses import dataclass
from enum import Enum, auto
import json
import logging
import queue
import time

log = logging.getLogger("serial_worker")


class CommandTag(Enum):
    """Tags for routing responses to correct handlers."""
    GENERIC = auto()
    SET_FORMAT = auto()
    REFRESH_MOTOR_DEBUG = auto()
    REFRESH_ENC = auto()
    REFRESH_MCONFIG = auto()
    HEARTBEAT = auto()
    APPLY_KVAL = auto()
    APPLY_PARAMS = auto()
    APPLY_PROTECTION = auto()
    CLEAR_FAULT = auto()
    BITMAP_TRANSFER = auto()
    FLASH_INFO = auto()
    FLASH_UPLOAD_SLOT = auto()
    FLASH_SHOW = auto()
    FLASH_ERASE = auto()
    FLASH_DIAG = auto()
    REFRESH_DRV_CONFIG = auto()
    REFRESH_PID_CONFIG = auto()
    APPLY_PID = auto()
    SET_CTRL_MODE = auto()
    SYSID_START = auto()
    SYSID_STATUS = auto()
    SYSID_DATA = auto()
    SYSID_ABORT = auto()
    REFRESH_ENC_FILTER = auto()
    APPLY_ENC_FILTER = auto()


@dataclass
class QueuedCommand:
    """Command descriptor for the worker queue."""
    command: str
    tag: CommandTag = CommandTag.GENERIC
    multiline: bool = False
    max_lines: int = 20
    callable: object = None


class SerialWorker(QObject):
    """Worker that handles serial communication in a background thread."""

    # Signals emitted to GUI thread
    connected = Signal()
    disconnected = Signal()
    connection_error = Signal(str)
    response_received = Signal(str, str, object)  # tag_name, command, response
    telemetry_updated = Signal(dict)
    heartbeat_ack_received = Signal(dict)
    heartbeat_timeout = Signal()
    event_received = Signal(str, dict)  # (event_type, data_dict)
    bitmap_progress = Signal(int, int)  # (bytes_sent, total_bytes)
    flash_upload_progress = Signal(int, int)  # (slot_done, total_slots)

    def __init__(self, client):
        super().__init__()
        self._client = client
        self._running = False
        self._command_queue = queue.Queue()
        self._poll_telemetry = False
        self._poll_interval = 0.1  # seconds (100ms = 10 Hz graph updates)

        # Coalesced indicator: only latest value is sent (replaces queue backlog)
        self._pending_indicator = None  # str or None

        # Heartbeat state
        self._heartbeat_enabled = False
        self._heartbeat_interval = 0.050  # seconds (50ms default)
        self._heartbeat_seq = 0
        self._last_heartbeat_send_time = 0.0
        self._heartbeat_stats = {
            'sent': 0,
            'acked': 0,
            'missed': 0,
            'last_rtt_ms': 0.0,
            'avg_rtt_ms': 0.0,
        }

    @Slot()
    def start_polling(self):
        """Start telemetry polling."""
        self._poll_telemetry = True

    @Slot()
    def stop_polling(self):
        """Stop telemetry polling."""
        self._poll_telemetry = False

    @Slot()
    def start_heartbeat(self):
        """Start sending heartbeats."""
        self._heartbeat_enabled = True
        self._heartbeat_seq = 0
        self._last_heartbeat_send_time = 0.0
        self._heartbeat_stats = {
            'sent': 0, 'acked': 0, 'missed': 0,
            'last_rtt_ms': 0.0, 'avg_rtt_ms': 0.0,
        }

    @Slot()
    def stop_heartbeat(self):
        """Stop sending heartbeats."""
        self._heartbeat_enabled = False

    @Slot(float)
    def set_heartbeat_interval(self, interval_sec: float):
        """Set heartbeat interval (thread-safe)."""
        self._heartbeat_interval = max(0.010, min(0.200, interval_sec))

    def queue_command(self, command):
        """Queue a command to be sent (thread-safe).

        Args:
            command: str or QueuedCommand
        """
        if isinstance(command, str):
            command = QueuedCommand(command=command)
        self._command_queue.put(command)

    def set_indicator_command(self, cmd_str: str):
        """Set the latest indicator command (coalesced, thread-safe).

        Only the most recent value is kept. The worker sends it on the next
        loop iteration, skipping any stale intermediate values.
        """
        self._pending_indicator = cmd_str

    @Slot()
    def run(self):
        """Main worker loop - runs in background thread."""
        self._running = True
        last_poll = 0

        while self._running:
            # Process ONE queued command per iteration (not all at once)
            # so heartbeats can interleave between long command sequences
            try:
                cmd = self._command_queue.get_nowait()
                self._send_command(cmd)
            except queue.Empty:
                pass

            # Send coalesced indicator command (latest-wins, no queue backlog)
            indicator_cmd = self._pending_indicator
            if indicator_cmd is not None:
                self._pending_indicator = None
                self._send_command(QueuedCommand(indicator_cmd, CommandTag.GENERIC))

            # Send heartbeat if enabled and due
            if self._heartbeat_enabled and self._client and self._client.is_connected():
                now = time.time()
                if now - self._last_heartbeat_send_time >= self._heartbeat_interval:
                    self._last_heartbeat_send_time = now
                    self._send_heartbeat()

            # Drain any buffered async events from protocol layer
            self._drain_events()

            # Poll telemetry if enabled and due
            if self._poll_telemetry and self._client and self._client.is_connected():
                now = time.time()
                if now - last_poll >= self._poll_interval:
                    last_poll = now
                    self._poll()

            # Small sleep to prevent busy-waiting
            time.sleep(0.01)

    def _send_command(self, queued: QueuedCommand):
        """Send a command and emit response."""
        if not self._client or not self._client.is_connected():
            log.warning("Cannot send %s: not connected", queued.command)
            return

        try:
            log.debug("Sending [%s]: %s", queued.tag.name, queued.command)
            if queued.callable is not None:
                resp = queued.callable(self._client)
            elif queued.multiline:
                resp = self._client.send_command_multiline(
                    queued.command, max_lines=queued.max_lines
                )
            else:
                resp = self._client.send_command(queued.command)
            log.debug("Response [%s] %s: success=%s",
                      queued.tag.name, queued.command, resp.success)
            self.response_received.emit(queued.tag.name, queued.command, resp)
        except Exception as e:
            log.warning("Command failed [%s] %s: %s",
                        queued.tag.name, queued.command, e)
            self.response_received.emit(queued.tag.name, queued.command, None)

    def _send_heartbeat(self):
        """Send a heartbeat and process ACK."""
        if not self._client or not self._client.is_connected():
            return

        self._heartbeat_seq += 1
        seq = self._heartbeat_seq
        send_time = time.time()

        try:
            resp = self._client.heartbeat(seq)
            rtt_ms = (time.time() - send_time) * 1000.0

            self._heartbeat_stats['sent'] += 1

            if resp and resp.success:
                self._heartbeat_stats['acked'] += 1
                self._heartbeat_stats['last_rtt_ms'] = rtt_ms

                # Exponential moving average for RTT
                alpha = 0.3
                prev = self._heartbeat_stats['avg_rtt_ms']
                if prev == 0:
                    self._heartbeat_stats['avg_rtt_ms'] = rtt_ms
                else:
                    self._heartbeat_stats['avg_rtt_ms'] = (
                        alpha * rtt_ms + (1 - alpha) * prev
                    )

                ack_data = {
                    'seq': resp.data.get('seq', seq),
                    'mcu_tick': resp.data.get('mcu_tick', 0),
                    'remaining_ms': resp.data.get('remaining_ms', 0),
                    'rtt_ms': rtt_ms,
                    'sent': self._heartbeat_stats['sent'],
                    'acked': self._heartbeat_stats['acked'],
                    'missed': self._heartbeat_stats['missed'],
                    'avg_rtt_ms': self._heartbeat_stats['avg_rtt_ms'],
                    'interval_ms': self._heartbeat_interval * 1000.0,
                }
                self.heartbeat_ack_received.emit(ack_data)
            else:
                self._heartbeat_stats['missed'] += 1

        except Exception as e:
            log.debug("Heartbeat %d failed: %s", seq, e)
            self._heartbeat_stats['missed'] += 1

    def _poll(self):
        """Poll for telemetry data."""
        if not self._client or not self._client.is_connected():
            return

        try:
            resp = self._client.get_status()
            if resp and resp.success and resp.data:
                self.telemetry_updated.emit(resp.data)
            elif resp and not resp.success:
                log.warning("GET_STATUS error: %s", resp.error_message)
        except Exception as e:
            log.warning("GET_STATUS exception: %s", e)
            if not self._client.is_connected():
                self.connection_error.emit(str(e))
                self._running = False

    def _drain_events(self):
        """Drain buffered async events from MotorClient and emit signals."""
        if not self._client or not self._client.is_connected():
            return
        for raw_line in self._client.pop_events():
            evt_type, evt_data = self._parse_event(raw_line)
            if evt_type:
                self.event_received.emit(evt_type, evt_data)

    @staticmethod
    def _parse_event(line: str) -> tuple:
        """
        Parse an event line (JSON or ASCII) into (type, data) tuple.

        JSON: {"kind":"event","type":"FAULT","seq":1,"ts_ms":123,"data":{...}}
        ASCII: !FAULT status=0x1234 detail=OCD

        Returns:
            (event_type, data_dict) or (None, None) on parse failure.
        """
        if line.startswith("{"):
            try:
                obj = json.loads(line)
                return (obj.get("type", ""), obj.get("data", {}))
            except json.JSONDecodeError:
                return (None, None)
        elif line.startswith("!"):
            parts = line[1:].split(None, 1)
            evt_type = parts[0] if parts else ""
            data = {}
            if len(parts) > 1:
                for kv in parts[1].split():
                    if "=" in kv:
                        k, v = kv.split("=", 1)
                        data[k] = v
            return (evt_type, data)
        return (None, None)

    @Slot()
    def stop(self):
        """Stop the worker loop."""
        self._running = False
        self._poll_telemetry = False
        self._heartbeat_enabled = False


class SerialThread:
    """Manages the serial worker thread."""

    def __init__(self, client):
        self._thread = QThread()
        self._worker = SerialWorker(client)
        self._worker.moveToThread(self._thread)

        # Connect thread lifecycle
        self._thread.started.connect(self._worker.run)

    @property
    def worker(self) -> SerialWorker:
        return self._worker

    def start(self):
        """Start the background thread."""
        if not self._thread.isRunning():
            self._thread.start()

    def stop(self):
        """Stop the background thread."""
        self._worker.stop()
        self._thread.quit()
        self._thread.wait(2000)  # Wait up to 2 seconds

    def send_command(self, command: str):
        """Queue a command to be sent."""
        self._worker.queue_command(command)

    def queue_refresh(self):
        """Queue the three refresh commands (MOTOR_DEBUG, ENC, MCONFIG)."""
        self._worker.queue_command(QueuedCommand(
            "MOTOR_DEBUG", CommandTag.REFRESH_MOTOR_DEBUG, multiline=True))
        self._worker.queue_command(QueuedCommand(
            "ENC", CommandTag.REFRESH_ENC, multiline=True, max_lines=5))
        self._worker.queue_command(QueuedCommand(
            "MCONFIG", CommandTag.REFRESH_MCONFIG, multiline=True, max_lines=10))

    def start_polling(self):
        """Start telemetry polling."""
        self._worker.start_polling()

    def stop_polling(self):
        """Stop telemetry polling."""
        self._worker.stop_polling()

    def start_heartbeat(self, interval_sec: float = 0.050):
        """Start heartbeat producer."""
        self._worker.set_heartbeat_interval(interval_sec)
        self._worker.start_heartbeat()

    def stop_heartbeat(self):
        """Stop heartbeat producer."""
        self._worker.stop_heartbeat()
