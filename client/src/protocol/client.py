"""
Motor controller protocol client.

Provides high-level command interface with JSON/ASCII response parsing.
"""

import json
from enum import Enum
from typing import Optional, Any, Dict
from dataclasses import dataclass, field

from transport.interface import ITransport, TransportError


class FrameType(Enum):
    """Classification of a received line from the device."""
    RESPONSE = "RESPONSE"      # OK, ERROR, PONG, STATUS, JSON responses
    EVENT = "EVENT"            # Async event (!FAULT, {"kind":"event",...})
    ECHO = "ECHO"              # Command echo
    TELEMETRY = "TELEMETRY"    # TELEM: or EVENT JOY lines


class ResponseFormat(Enum):
    """Response format mode."""
    ASCII = "ASCII"
    JSON = "JSON"


class ProtocolError(Exception):
    """Protocol-level error."""
    pass


class CommandError(ProtocolError):
    """Command returned an error response."""

    def __init__(self, code: str, message: str, command: str = ""):
        self.code = code
        self.message = message
        self.command = command
        super().__init__(f"{code}: {message}")


@dataclass
class Response:
    """Parsed response from the device."""
    success: bool
    command: str
    data: Dict[str, Any] = field(default_factory=dict)
    error_code: Optional[str] = None
    error_message: Optional[str] = None
    raw: str = ""

    @property
    def is_error(self) -> bool:
        return not self.success


class MotorClient:
    """
    High-level client for the motor controller protocol.

    Handles command sending, response parsing, and format switching.
    """

    DEFAULT_TIMEOUT_MS = 2000
    DEFAULT_STREAM_CHUNK = 256

    def __init__(self, transport: ITransport):
        """
        Initialize the motor client.

        Args:
            transport: Transport instance for communication.
        """
        self._transport = transport
        self._format = ResponseFormat.ASCII
        self._timeout_ms = self.DEFAULT_TIMEOUT_MS
        self._stream_chunk = self.DEFAULT_STREAM_CHUNK
        self._event_buffer: list[str] = []

    @property
    def transport(self) -> ITransport:
        """Get the underlying transport."""
        return self._transport

    @property
    def format(self) -> ResponseFormat:
        """Get current response format."""
        return self._format

    def connect(self) -> bool:
        """Connect to the device."""
        return self._transport.open()

    def disconnect(self) -> None:
        """Disconnect from the device."""
        self._transport.close()

    def is_connected(self) -> bool:
        """Check if connected."""
        return self._transport.is_connected()

    # -------------------------------------------------------------------------
    # Frame classification and event-aware response reading
    # -------------------------------------------------------------------------

    @staticmethod
    def _classify_line(line: str, sent_command: str = "") -> FrameType:
        """
        Single point of line classification (per protocol contract).

        Classification order:
          1. ECHO — if it exactly matches the command just sent
          2. EVENT — if it starts with '!' (ASCII) or is JSON with "kind":"event"
          3. TELEMETRY — if it starts with 'TELEM:' or 'EVENT JOY'
          4. RESPONSE — everything else
        """
        stripped = line.strip()
        # Echo detection
        if sent_command and stripped == sent_command.strip():
            return FrameType.ECHO
        # Event detection
        if stripped.startswith("!"):
            return FrameType.EVENT
        if stripped.startswith("{") and '"kind":"event"' in stripped:
            return FrameType.EVENT
        # Telemetry detection
        if stripped.startswith("TELEM:") or stripped.startswith("EVENT JOY"):
            return FrameType.TELEMETRY
        # Everything else is a response
        return FrameType.RESPONSE

    def _recv_response(self, command: str, timeout_ms: int) -> Optional[str]:
        """
        Read lines until we get a RESPONSE, buffering events along the way.

        Skips ECHO and TELEMETRY frames silently.
        Buffers EVENT frames into _event_buffer.

        Returns:
            The first RESPONSE line, or None on timeout.
        """
        max_skip = 16  # Safety: don't loop forever on non-response lines
        for _ in range(max_skip):
            line = self._transport.recv_line(timeout_ms=timeout_ms)
            if line is None:
                return None
            frame_type = self._classify_line(line, command)
            if frame_type == FrameType.RESPONSE:
                return line
            elif frame_type == FrameType.EVENT:
                self._event_buffer.append(line.strip())
            # ECHO and TELEMETRY are silently skipped
        return None  # Too many non-response lines

    def pop_events(self) -> list[str]:
        """Return and clear buffered event lines."""
        events = self._event_buffer[:]
        self._event_buffer.clear()
        return events

    def _drain_pending(self) -> None:
        """
        Non-blocking drain: read all pending lines, buffer events, discard rest.

        Replaces destructive flush_input() at the protocol layer so that
        async event frames arriving between commands are preserved.
        Transport layer remains unchanged (flush_input still exists for
        connection-time use).
        """
        while self._transport.available() > 0:
            line = self._transport.recv_line(timeout_ms=10)
            if line is None:
                break
            if self._classify_line(line) == FrameType.EVENT:
                self._event_buffer.append(line.strip())

    def set_format(self, fmt: ResponseFormat) -> Response:
        """
        Set the response format.

        Args:
            fmt: Desired response format (ASCII or JSON).

        Returns:
            Response from the device.
        """
        resp = self.send_command(f"SET_FORMAT {fmt.value}")
        if resp.success:
            self._format = fmt
        return resp

    def get_format(self) -> Response:
        """Query current response format from device."""
        return self.send_command("GET_FORMAT")

    def send_command(
        self,
        command: str,
        timeout_ms: Optional[int] = None
    ) -> Response:
        """
        Send a command and wait for response.

        Args:
            command: Command string to send.
            timeout_ms: Response timeout (uses default if None).

        Returns:
            Parsed Response object.

        Raises:
            TransportError: If communication fails.
            ProtocolError: If response cannot be parsed.
        """
        if not self.is_connected():
            raise TransportError("Not connected")

        timeout = timeout_ms or self._timeout_ms

        # Drain pending input, preserving any buffered events
        self._drain_pending()

        # Send command
        self._transport.send_line(command)

        # Read response (skips echoes, telemetry; buffers events)
        line = self._recv_response(command, timeout)
        if line is None:
            raise ProtocolError(f"No response to command: {command}")

        # Parse based on current format
        return self._parse_response(line, command)

    def send_command_multiline(
        self,
        command: str,
        timeout_ms: Optional[int] = None,
        max_lines: int = 20
    ) -> Response:
        """
        Send a command and collect multi-line response.

        Reads lines until timeout or max_lines reached.
        Use for commands like MOTOR_DEBUG that output multiple lines.

        Args:
            command: Command string to send.
            timeout_ms: Response timeout per line (uses default if None).
            max_lines: Maximum lines to read.

        Returns:
            Response with all lines joined in raw field.
        """
        if not self.is_connected():
            raise TransportError("Not connected")

        timeout = timeout_ms or self._timeout_ms

        # Drain pending input, preserving any buffered events
        self._drain_pending()

        # Send command
        self._transport.send_line(command)

        # Collect all response lines (skip echoes, telemetry; buffer events)
        lines = []
        short_timeout = min(250, timeout)  # Timeout for subsequent lines

        for i in range(max_lines):
            # Use full timeout for first line, shorter timeout for subsequent
            line_timeout = timeout if len(lines) == 0 else short_timeout
            line = self._transport.recv_line(timeout_ms=line_timeout)
            if line is None:
                break
            frame_type = self._classify_line(line, command)
            if frame_type == FrameType.ECHO or frame_type == FrameType.TELEMETRY:
                continue
            if frame_type == FrameType.EVENT:
                self._event_buffer.append(line.strip())
                continue
            lines.append(line.strip())

        if not lines:
            raise ProtocolError(f"No response to command: {command}")

        # Join all lines
        full_response = "\n".join(lines)

        # Check for error in first line
        first_line = lines[0]
        if first_line.startswith("ERROR"):
            message = first_line[5:].strip() if len(first_line) > 5 else "Unknown error"
            return Response(
                success=False,
                command=command,
                error_code="ERROR",
                error_message=message,
                raw=full_response,
            )

        # Success with all data
        return Response(
            success=True,
            command=command,
            data={"lines": lines},
            raw=full_response,
        )

    def _parse_response(self, line: str, command: str) -> Response:
        """Parse a response line based on current format."""
        line = line.strip()

        # Try JSON parsing first (handles both modes gracefully)
        if line.startswith("{"):
            return self._parse_json_response(line, command)

        # ASCII format parsing
        return self._parse_ascii_response(line, command)

    def _parse_json_response(self, line: str, command: str) -> Response:
        """Parse a JSON response."""
        try:
            data = json.loads(line)
        except json.JSONDecodeError as e:
            raise ProtocolError(f"Invalid JSON response: {e}")

        status = data.get("status", "")
        cmd = data.get("command", command)

        if status == "ok":
            return Response(
                success=True,
                command=cmd,
                data=data.get("data", {}),
                raw=line,
            )
        elif status == "error":
            return Response(
                success=False,
                command=cmd,
                error_code=data.get("code", "ERROR"),
                error_message=data.get("message", "Unknown error"),
                raw=line,
            )
        else:
            raise ProtocolError(f"Unknown status in JSON response: {status}")

    def _parse_ascii_response(self, line: str, command: str) -> Response:
        """Parse an ASCII response."""
        # Handle common ASCII response formats
        if line.startswith("OK"):
            # "OK" or "OK message"
            message = line[2:].strip() if len(line) > 2 else ""
            data = {}
            if message.startswith("READY"):
                # "OK READY <bytes>"
                parts = message.split()
                if len(parts) == 2 and parts[0] == "READY":
                    try:
                        data["ready_bytes"] = int(parts[1])
                    except ValueError:
                        data["message"] = message
                else:
                    data["message"] = message
            elif message:
                data["message"] = message
            return Response(
                success=True,
                command=command,
                data=data,
                raw=line,
            )
        elif line.startswith("ERROR"):
            # "ERROR message"
            message = line[5:].strip() if len(line) > 5 else "Unknown error"
            return Response(
                success=False,
                command=command,
                error_code="ERROR",
                error_message=message,
                raw=line,
            )
        elif line.startswith("PONG"):
            # "PONG seq rx_tick tx_tick state"
            parts = line.split()
            data = {}
            if len(parts) >= 5:
                data = {
                    "seq": int(parts[1]),
                    "rx_tick": int(parts[2]),
                    "tx_tick": int(parts[3]),
                    "state": parts[4],
                }
            return Response(success=True, command=command, data=data, raw=line)
        elif line.startswith("HEARTBEAT_ACK"):
            # "HEARTBEAT_ACK seq mcu_tick remaining_ms"
            parts = line.split()
            data = {}
            if len(parts) >= 4:
                data = {
                    "seq": int(parts[1]),
                    "mcu_tick": int(parts[2]),
                    "remaining_ms": int(parts[3]),
                }
            return Response(success=True, command=command, data=data, raw=line)
        elif line.startswith("HEARTBEAT_STATUS"):
            # "HEARTBEAT_STATUS ENABLED/DISABLED timeout_ms last_seq remaining_ms OK/TIMED_OUT"
            parts = line.split()
            data = {}
            if len(parts) >= 6:
                data = {
                    "enabled": parts[1] == "ENABLED",
                    "timeout_ms": int(parts[2]),
                    "last_seq": int(parts[3]),
                    "remaining_ms": int(parts[4]),
                    "timed_out": parts[5] == "TIMED_OUT",
                }
            return Response(success=True, command=command, data=data, raw=line)
        elif line.startswith("STATUS"):
            # "STATUS state tick queue_depth mode enc_status pos speed"
            parts = line.split()
            data = {}
            if len(parts) >= 8:
                data = {
                    "state": parts[1],
                    "tick": int(parts[2]),
                    "queue_depth": int(parts[3]),
                    "mode": parts[4],
                    "encoder_status": parts[5],
                    "position": int(parts[6]),
                    "speed": int(parts[7]),
                }
            return Response(success=True, command=command, data=data, raw=line)
        else:
            # Unknown format - treat as success with raw data
            return Response(
                success=True,
                command=command,
                data={"raw": line},
                raw=line,
            )

    # -------------------------------------------------------------------------
    # Convenience command methods
    # -------------------------------------------------------------------------

    def ping(self, seq: int = 0) -> Response:
        """Send PING command for latency measurement."""
        return self.send_command(f"PING {seq}")

    def get_tick(self) -> Response:
        """Get current MCU tick counter."""
        return self.send_command("GET_TICK")

    def get_status(self) -> Response:
        """Get full device status."""
        return self.send_command("GET_STATUS")

    def move(self, steps: int, direction: int) -> Response:
        """
        Relative move command.

        Args:
            steps: Number of steps (0-2097151).
            direction: 0 for reverse, 1 for forward.
        """
        return self.send_command(f"MOVE {steps} {direction}")

    def goto(self, position: int) -> Response:
        """
        Absolute move command.

        Args:
            position: Target position (-2097152 to 2097151).
        """
        return self.send_command(f"GOTO {position}")

    def run(self, speed: int, direction: int) -> Response:
        """
        Continuous run command.

        Args:
            speed: Speed in steps/sec (0-15625).
            direction: 0 for reverse, 1 for forward.
        """
        return self.send_command(f"RUN {speed} {direction}")

    def stop(self, hard: bool = False) -> Response:
        """Stop motion."""
        cmd = "STOP hard" if hard else "STOP"
        return self.send_command(cmd)

    def estop(self) -> Response:
        """Emergency stop."""
        return self.send_command("ESTOP")

    def home(self) -> Response:
        """Go to home position."""
        return self.send_command("HOME")

    def zero(self) -> Response:
        """Set current position as zero."""
        return self.send_command("ZERO")

    def enable(self) -> Response:
        """Enable motor outputs."""
        return self.send_command("ENABLE")

    def disable(self) -> Response:
        """Disable motor outputs (Hi-Z)."""
        return self.send_command("DISABLE")

    def set_accel(self, value: int) -> Response:
        """Set acceleration (1-4095)."""
        return self.send_command(f"ACCEL {value}")

    def set_decel(self, value: int) -> Response:
        """Set deceleration (1-4095)."""
        return self.send_command(f"DECEL {value}")

    def set_max_speed(self, value: int) -> Response:
        """Set maximum speed (1-1023)."""
        return self.send_command(f"MAXSPD {value}")

    def get_encoder(self) -> Response:
        """Get encoder state."""
        return self.send_command("ENCODER")

    def get_version(self) -> Response:
        """Get firmware version."""
        return self.send_command("VER")

    def get_help(self) -> Response:
        """Get help text."""
        return self.send_command("HELP", timeout_ms=5000)

    # -------------------------------------------------------------------------
    # Heartbeat commands
    # -------------------------------------------------------------------------

    def heartbeat(self, seq: int = 0) -> Response:
        """Send HEARTBEAT command to reset watchdog timer."""
        return self.send_command(f"HEARTBEAT {seq}")

    def set_heartbeat(self, timeout_ms: int) -> Response:
        """Configure heartbeat watchdog timeout (0=disable, clamped to [100,5000])."""
        return self.send_command(f"SET_HEARTBEAT {timeout_ms}")

    def get_heartbeat_status(self) -> Response:
        """Query heartbeat watchdog status."""
        return self.send_command("GET_HEARTBEAT_STATUS")

    # -------------------------------------------------------------------------
    # Display / Bitmap Streaming
    # -------------------------------------------------------------------------

    def disp_bitmap(self, x: int, y: int, w: int, h: int, data: bytes,
                    timeout_ms: Optional[int] = None) -> Response:
        """
        Stream raw RGB565 bitmap data to the device.

        Protocol:
          1) Send DISP_BITMAP <x> <y> <w> <h>
          2) Expect "OK READY <bytes>" (ASCII)
          3) Send exactly <bytes> raw RGB565 bytes
          4) Receive final OK/ERROR response
        """
        if not self.is_connected():
            raise TransportError("Not connected")

        timeout = timeout_ms or self._timeout_ms
        self._drain_pending()

        # Step 1: request streaming
        self._transport.send_line(f"DISP_BITMAP {x} {y} {w} {h}")

        # Step 2: read READY response (ASCII even if JSON mode is enabled)
        line = self._transport.recv_line(timeout_ms=timeout)
        if line is None:
            raise ProtocolError("No READY response for DISP_BITMAP")

        if line.startswith("{"):
            # JSON error or OK; treat as standard response
            resp = self._parse_json_response(line, "DISP_BITMAP")
            if resp.success:
                # JSON OK without READY is unexpected for streaming
                raise ProtocolError("Expected READY for DISP_BITMAP, got JSON OK")
            return resp

        resp = self._parse_ascii_response(line, "DISP_BITMAP")
        ready_bytes = resp.data.get("ready_bytes")
        if ready_bytes is None:
            raise ProtocolError(f"Expected READY response, got: {line}")

        if len(data) != ready_bytes:
            raise ProtocolError(
                f"Bitmap size mismatch: expected {ready_bytes}, got {len(data)}"
            )

        # Step 3: stream raw bytes in chunks
        offset = 0
        while offset < ready_bytes:
            chunk = data[offset:offset + self._stream_chunk]
            self._transport.send(chunk)
            offset += len(chunk)

        # Step 4: final OK/ERROR
        final_line = self._transport.recv_line(timeout_ms=timeout)
        if final_line is None:
            raise ProtocolError("No final response for DISP_BITMAP")
        return self._parse_response(final_line, "DISP_BITMAP")
