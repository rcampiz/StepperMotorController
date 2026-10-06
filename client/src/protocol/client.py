"""
Motor controller protocol client.

Provides high-level command interface with JSON/ASCII response parsing.
"""

import re
import struct
import threading
import time
import zlib
from enum import Enum
from typing import Optional, Any, Dict

from transport.interface import ITransport, TransportError
# Framing and parsing live in the shared core (client refactor step 1).
from smc_core.framing import FrameKind, classify
from smc_core.codec import ProtocolError, Response, parse_response


# Protocol 2 (F12): an ASCII multi-line reply ends with `OK <n>` (n = lines sent)
MULTILINE_TERMINATOR = re.compile(r"^OK \d+$")

# Frames a command reader skips (buffered as events: EVENT and RESET)
_SKIP = (FrameKind.ECHO, FrameKind.TELEMETRY, FrameKind.BANNER, FrameKind.JOYSTICK,
         FrameKind.EMPTY)
_KEEP_AS_EVENT = (FrameKind.EVENT, FrameKind.RESET)


class ResponseFormat(Enum):
    """Response format mode."""
    ASCII = "ASCII"
    JSON = "JSON"


class CommandError(ProtocolError):
    """Command returned an error response."""

    def __init__(self, code: str, message: str, command: str = ""):
        self.code = code
        self.message = message
        self.command = command
        super().__init__(f"{code}: {message}")


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
        # Set by the ESTOP fast path; every streaming loop checks it per chunk.
        self._cancel = threading.Event()

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

    def _recv_response(self, command: str, timeout_ms: int) -> Optional[str]:
        """
        Read lines until a RESPONSE frame arrives or the deadline passes (R11: the
        loop is bounded by time, not by a line count). Events and !RESET lines are
        buffered; echo, telemetry, banner and joystick lines are skipped.
        """
        deadline = time.monotonic() + timeout_ms / 1000.0
        while True:
            left_ms = int((deadline - time.monotonic()) * 1000)
            if left_ms <= 0:
                return None
            line = self._transport.recv_line(timeout_ms=left_ms)
            if line is None:
                return None
            frame = classify(line, command)
            if frame.kind == FrameKind.RESPONSE:
                return line
            if frame.kind in _KEEP_AS_EVENT:
                self._event_buffer.append(frame.text)

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
            frame = classify(line)
            if frame.kind in _KEEP_AS_EVENT:
                self._event_buffer.append(frame.text)

    def set_format(self, fmt: ResponseFormat) -> Response:
        """
        Set the response format.

        Args:
            fmt: Desired response format (ASCII or JSON).

        Returns:
            Response from the device.
        """
        resp = self.send_command(f"FMT {fmt.value}")
        if resp.success:
            self._format = fmt
        return resp

    def get_format(self) -> Response:
        """Query current response format from device."""
        return self.send_command("FMT?")

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

        terminated = False
        for i in range(max_lines):
            # Use full timeout for first line, shorter timeout for subsequent
            line_timeout = timeout if len(lines) == 0 else short_timeout
            line = self._transport.recv_line(timeout_ms=line_timeout)
            if line is None:
                break
            frame = classify(line, command)
            if frame.kind in _SKIP:
                continue
            if frame.kind in _KEEP_AS_EVENT:
                self._event_buffer.append(frame.text)
                continue
            stripped = frame.text
            if stripped.startswith("{"):
                # Protocol 2 (F12): in JSON mode the whole reply is one envelope whose
                # data.lines holds the text; expose it joined in raw for text consumers.
                resp = self._parse_json_response(stripped, command)
                if resp.success and isinstance(resp.data, dict) and "lines" in resp.data:
                    return Response(success=True, command=resp.command, data=resp.data,
                                    raw="\n".join(resp.data["lines"]))
                return resp
            if MULTILINE_TERMINATOR.match(stripped):
                # Protocol 2 (F12): `OK <n>` closes an ASCII multi-line reply
                terminated = True
                break
            lines.append(stripped)

        if terminated and not lines:
            return Response(success=True, command=command, data={"lines": []}, raw="")
        if not lines:
            raise ProtocolError(f"No response to command: {command}")

        # Join all lines
        full_response = "\n".join(lines)

        # Check for error in first line
        first_line = lines[0]
        if first_line.startswith("ERROR"):
            err = parse_response(first_line, command)
            err.raw = full_response
            return err

        # Success with all data
        return Response(
            success=True,
            command=command,
            data={"lines": lines},
            raw=full_response,
        )

    def _parse_response(self, line: str, command: str) -> Response:
        """Parse a reply line (either format) with the core codec."""
        return parse_response(line, command)

    _parse_json_response = _parse_response
    _parse_ascii_response = _parse_response

    # -------------------------------------------------------------------------
    # Convenience command methods
    # -------------------------------------------------------------------------

    def ping(self, seq: int = 0) -> Response:
        """Send PING command for latency measurement."""
        return self.send_command(f"DIAG:PING {seq}")

    def get_tick(self) -> Response:
        """Get current MCU tick counter."""
        return self.send_command("SYST:TICK?")

    def get_status(self) -> Response:
        """Get full device status."""
        return self.send_command("DIAG:STAT?")

    def move(self, steps: int, direction: int) -> Response:
        """
        Relative move command.

        Args:
            steps: Number of steps (0-2097151).
            direction: 0 for reverse, 1 for forward.
        """
        return self.send_command(f"MOT:MOVE {steps} {direction}")

    def goto(self, position: int) -> Response:
        """
        Absolute move command.

        Args:
            position: Target position (-2097152 to 2097151).
        """
        return self.send_command(f"MOT:GOTO {position}")

    def run(self, speed: int, direction: int) -> Response:
        """
        Continuous run command.

        Args:
            speed: Speed in steps/sec (0-15625).
            direction: 0 for reverse, 1 for forward.
        """
        return self.send_command(f"MOT:RUN {speed} {direction}")

    def stop(self, hard: bool = False) -> Response:
        """Stop motion."""
        cmd = "MOT:STOP hard" if hard else "MOT:STOP"
        return self.send_command(cmd)

    def estop(self) -> Response:
        """Emergency stop through the normal command path (queued, with reply)."""
        return self.send_command("SYST:ESTOP")

    # ------------------------------------------------------------------
    # ESTOP fast path
    # ------------------------------------------------------------------

    ESTOP_LINE = b"SYST:ESTOP\r\n"
    ESTOP_OOB_BYTE = b"\x18"          # Phase 4: recognised in the USART2 ISR

    def estop_fast(self, oob: bool = False) -> None:
        """
        Stop NOW, bypassing the command queue and any transfer in progress.

        1. Set the cancel flag so the streaming loops stop after their current chunk.
        2. transport.send_priority(): purge unsent output, then write the stop bytes,
           under the transport write lock (waits for at most one in-flight chunk).
        Call from any thread. Does not wait for a reply; confirm with
        estop_confirmed() on a following DIAG:STAT? reply.

        Remaining limit until Phase 4: the firmware parses the text command only when
        the Comms task is back in its command loop, i.e. after a bitmap/flash handler
        finishes. With oob=True the 0x18 byte goes first and is honoured in the ISR.
        """
        self._cancel.set()
        payload = (self.ESTOP_OOB_BYTE if oob else b"") + self.ESTOP_LINE
        self._transport.send_priority(payload)

    def cancel_transfers(self) -> None:
        """Abort any streaming transfer at its next chunk boundary."""
        self._cancel.set()

    def clear_cancel(self) -> None:
        """Re-arm after the worker has dropped its queue."""
        self._cancel.clear()

    def transfer_cancelled(self) -> bool:
        return self._cancel.is_set()

    def _send_chunk(self, chunk: bytes) -> None:
        """One bounded write: <= stream chunk, then wait for the driver to drain it.

        Guarantees that at most one chunk can sit ahead of a priority write, and
        that a cancelled transfer stops within one chunk.
        """
        if self._cancel.is_set():
            raise ProtocolError("transfer cancelled (ESTOP)")
        self._transport.send(chunk)
        self._transport.drain_output(timeout_ms=500)
        if self._cancel.is_set():
            raise ProtocolError("transfer cancelled (ESTOP)")

    @staticmethod
    def upload_precondition(data: Dict[str, Any]) -> tuple:
        """
        May a blocking flash upload start? (allowed, reason)

        The firmware watchdog is never disarmed, and UI:FLASH:UPLOAD blocks the
        Comms task for ~13 s at 115200 baud, so the stop it will trigger must be
        harmless: the motor has to be idle and in Hi-Z already.
        """
        if not data:
            return False, "no status snapshot yet (wait for telemetry)"
        state = str(data.get("state", "?"))
        motor = data.get("motor", {}) or {}
        hiz = motor.get("hi_z", motor.get("hiz"))
        busy = motor.get("busy", False)
        if state not in ("IDLE", "ESTOP", "FAULT"):
            return False, f"motor must be idle (state={state})"
        if busy:
            return False, "motor is busy"
        if not hiz:
            return False, "motor must be in Hi-Z (send MOT:DIS first)"
        return True, "idle, Hi-Z"

    @staticmethod
    def estop_confirmed(data: Dict[str, Any]) -> tuple:
        """
        Judge a DIAG:STAT? reply after an ESTOP.

        Returns (confirmed, detail). Confirmed means the controller reports the
        ESTOP (or FAULT) latch AND the driver reports Hi-Z (bridges off).
        """
        state = str(data.get("state", "?"))
        motor = data.get("motor", {}) or {}
        hiz = motor.get("hi_z", motor.get("hiz"))
        latched = state in ("ESTOP", "FAULT")
        ok = bool(latched and hiz)
        detail = f"state={state} hiZ={hiz}"
        return ok, detail

    def home(self) -> Response:
        """Go to home position."""
        return self.send_command("MOT:HOME")

    def zero(self) -> Response:
        """Set current position as zero."""
        return self.send_command("MOT:ZERO")

    def enable(self) -> Response:
        """Enable motor outputs."""
        return self.send_command("MOT:EN")

    def disable(self) -> Response:
        """Disable motor outputs (Hi-Z)."""
        return self.send_command("MOT:DIS")

    def get_encoder(self) -> Response:
        """Get encoder state."""
        return self.send_command("CTRL:ENC?")

    def get_version(self) -> Response:
        """Get firmware version."""
        return self.send_command("SYST:VER?")

    def get_help(self) -> Response:
        """Get help text."""
        return self.send_command("SYST:HELP?", timeout_ms=5000)

    # -------------------------------------------------------------------------
    # Heartbeat commands
    # -------------------------------------------------------------------------

    def heartbeat(self, seq: int = 0) -> Response:
        """Send HEARTBEAT command to reset watchdog timer."""
        return self.send_command(f"SYST:HB {seq}")

    def set_heartbeat(self, timeout_ms: int) -> Response:
        """Configure heartbeat watchdog timeout (0=disable, clamped to [100,5000])."""
        return self.send_command(f"SYST:HB:TIMEOUT {timeout_ms}")

    def get_heartbeat_status(self) -> Response:
        """Query heartbeat watchdog status."""
        return self.send_command("SYST:HB:STAT?")

    # -------------------------------------------------------------------------
    # Display / Bitmap Streaming
    # -------------------------------------------------------------------------

    def disp_bitmap(self, x: int, y: int, w: int, h: int, data: bytes,
                    timeout_ms: Optional[int] = None,
                    progress_cb=None,
                    use_crc: bool = False) -> Response:
        """
        Stream raw RGB565 bitmap data to the device.

        Protocol:
          1) Send UI:DISP:BITMAP <x> <y> <w> <h> [CRC]
          2) Expect "OK READY <bytes>" (ASCII)
          3) Send exactly <bytes> raw RGB565 bytes [+ 4-byte CRC32 LE]
          4) Receive final OK/ERROR response
        """
        if not self.is_connected():
            raise TransportError("Not connected")

        timeout = timeout_ms or self._timeout_ms
        self._drain_pending()

        # Step 1: request streaming
        cmd_str = f"UI:DISP:BITMAP {x} {y} {w} {h}"
        if use_crc:
            cmd_str += " CRC"
        self._transport.send_line(cmd_str)

        # Step 2: read READY response, skipping echo/events
        line = self._recv_response(cmd_str, timeout)
        if line is None:
            raise ProtocolError("No READY response for UI:DISP:BITMAP")

        if line.startswith("{"):
            # JSON error or OK; treat as standard response
            resp = self._parse_json_response(line, "UI:DISP:BITMAP")
            if resp.success:
                # JSON OK without READY is unexpected for streaming
                raise ProtocolError("Expected READY for UI:DISP:BITMAP, got JSON OK")
            return resp

        resp = self._parse_ascii_response(line, "UI:DISP:BITMAP")
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
            self._send_chunk(chunk)
            offset += len(chunk)
            if progress_cb is not None:
                progress_cb(offset, ready_bytes)

        if use_crc:
            crc = zlib.crc32(data[:ready_bytes]) & 0xFFFFFFFF
            self._transport.send(struct.pack('<I', crc))

        # Step 4: final OK/ERROR (skip any echo/event lines)
        final_line = self._recv_response(cmd_str, timeout)
        if final_line is None:
            raise ProtocolError("No final response for UI:DISP:BITMAP")
        return self._parse_response(final_line, "UI:DISP:BITMAP")

    # -------------------------------------------------------------------------
    # Baud rate negotiation
    # -------------------------------------------------------------------------

    def set_baud(self, baud: int) -> bool:
        """
        Negotiate a new baud rate with the firmware.

        Sends SET_BAUD, waits for OK, switches pyserial port, verifies
        with PING. If verification fails, reverts to the old baud rate.
        The firmware auto-reverts after 2 seconds if no valid command arrives.

        Returns:
            True if baud rate was successfully changed, False otherwise.
        """
        import time

        old_baud = self._transport.baudrate

        # Send SET_BAUD at current baud rate
        try:
            resp = self.send_command(f"SYST:BAUD {baud}")
        except Exception:
            return False

        if not resp.success:
            return False

        # Give firmware time to switch (it does flush + 10ms delay internally)
        time.sleep(0.05)

        # Switch the serial port to the new baud rate
        self._transport.set_baudrate(baud)

        # Small delay for ST-LINK to reconfigure its UART bridge
        time.sleep(0.05)

        # Verify with PING at new baud rate
        try:
            verify = self.send_command("DIAG:PING 99", timeout_ms=1000)
            if verify.success:
                return True
        except Exception:
            pass

        # Verification failed — revert to old baud rate
        self._transport.set_baudrate(old_baud)
        return False

    # -------------------------------------------------------------------------
    # Flash Image Storage
    # -------------------------------------------------------------------------

    def flash_info(self) -> Response:
        """Query NOR flash info (capacity, slot count)."""
        return self.send_command("UI:FLASH:INFO")

    def flash_show(self, slot: int) -> Response:
        """Display an image stored in flash slot on the LCD."""
        return self.send_command(f"UI:FLASH:SHOW {slot}")

    def flash_erase_all(self, timeout_ms: int = 60000) -> Response:
        """Erase all flash image slots. Can take several seconds."""
        return self.send_command("UI:FLASH:ERASE_ALL", timeout_ms=timeout_ms)

    def flash_upload(self, slot: int, data: bytes,
                     timeout_ms: Optional[int] = None,
                     progress_cb=None,
                     use_crc: bool = False) -> Response:
        """
        Upload raw RGB565 image data to a flash slot.

        Protocol (same pattern as disp_bitmap):
          1) Send UI:FLASH:UPLOAD <slot> [CRC]
          2) Expect "OK READY <bytes>"
          3) Send raw RGB565 bytes [+ 4-byte CRC32 LE]
          4) Receive final OK/ERROR
        """
        if not self.is_connected():
            raise TransportError("Not connected")

        timeout = timeout_ms or max(self._timeout_ms, 30000)
        self._drain_pending()

        cmd_str = f"UI:FLASH:UPLOAD {slot}"
        if use_crc:
            cmd_str += " CRC"
        self._transport.send_line(cmd_str)

        # Wait for READY (flash erase may take a moment)
        line = self._recv_response(cmd_str, timeout)
        if line is None:
            raise ProtocolError("No READY response for UI:FLASH:UPLOAD")

        if line.startswith("{"):
            resp = self._parse_json_response(line, "UI:FLASH:UPLOAD")
            if resp.success:
                raise ProtocolError("Expected READY for UI:FLASH:UPLOAD, got JSON OK")
            return resp

        resp = self._parse_ascii_response(line, "UI:FLASH:UPLOAD")
        ready_bytes = resp.data.get("ready_bytes")
        if ready_bytes is None:
            raise ProtocolError(f"Expected READY response, got: {line}")

        if len(data) < ready_bytes:
            raise ProtocolError(
                f"Data too small: expected {ready_bytes}, got {len(data)}"
            )

        # Stream raw bytes
        offset = 0
        while offset < ready_bytes:
            chunk = data[offset:offset + self._stream_chunk]
            self._send_chunk(chunk)
            offset += len(chunk)
            if progress_cb is not None:
                progress_cb(offset, ready_bytes)

        if use_crc:
            crc = zlib.crc32(data[:ready_bytes]) & 0xFFFFFFFF
            self._transport.send(struct.pack('<I', crc))

        # Final response (programming may take a moment)
        final_line = self._recv_response(cmd_str, timeout)
        if final_line is None:
            raise ProtocolError("No final response for UI:FLASH:UPLOAD")
        return self._parse_response(final_line, "UI:FLASH:UPLOAD")

    # -------------------------------------------------------------------------
    # Display Indicator + RLE Bitmap
    # -------------------------------------------------------------------------

    def disp_indicator(self, angle: int, rotation_dir: int,
                       has_translation: bool) -> Response:
        """
        Draw a motion indicator on the MCU LCD.

        Args:
            angle: Direction angle 0-359 (0=up/north, clockwise).
            rotation_dir: -1 (CCW), 0 (none), 1 (CW).
            has_translation: Whether translational motion is active.
        """
        return self.send_command(
            f"UI:DISP:INDICATOR {angle} {rotation_dir} {int(has_translation)}"
        )

    def disp_bitmap_rle(self, x: int, y: int, w: int, h: int,
                        rle_data: bytes,
                        timeout_ms: Optional[int] = None,
                        progress_cb=None,
                        use_crc: bool = False) -> Response:
        """
        Stream RLE-compressed RGB565 bitmap data to the device.

        Protocol:
          1) Send UI:DISP:BITMAP:RLE <x> <y> <w> <h> <compressed_bytes> [CRC]
          2) Expect "OK READY <compressed_bytes>"
          3) Send compressed data [+ 4-byte CRC32 little-endian]
          4) Receive final OK/ERROR
        """
        if not self.is_connected():
            raise TransportError("Not connected")

        timeout = timeout_ms or self._timeout_ms
        self._drain_pending()

        compressed_len = len(rle_data)
        cmd_str = f"UI:DISP:BITMAP:RLE {x} {y} {w} {h} {compressed_len}"
        if use_crc:
            cmd_str += " CRC"
        self._transport.send_line(cmd_str)

        line = self._recv_response(cmd_str, timeout)
        if line is None:
            raise ProtocolError("No READY response for UI:DISP:BITMAP:RLE")

        if line.startswith("{"):
            resp = self._parse_json_response(line, "UI:DISP:BITMAP:RLE")
            if resp.success:
                raise ProtocolError(
                    "Expected READY for UI:DISP:BITMAP:RLE, got JSON OK"
                )
            return resp

        resp = self._parse_ascii_response(line, "UI:DISP:BITMAP:RLE")
        ready_bytes = resp.data.get("ready_bytes")
        if ready_bytes is None:
            raise ProtocolError(f"Expected READY response, got: {line}")

        if compressed_len != ready_bytes:
            raise ProtocolError(
                f"RLE size mismatch: expected {ready_bytes}, "
                f"got {compressed_len}"
            )

        offset = 0
        while offset < compressed_len:
            chunk = rle_data[offset:offset + self._stream_chunk]
            self._send_chunk(chunk)
            offset += len(chunk)
            if progress_cb is not None:
                progress_cb(offset, compressed_len)

        if use_crc:
            crc = zlib.crc32(rle_data) & 0xFFFFFFFF
            self._transport.send(struct.pack('<I', crc))

        final_line = self._recv_response(cmd_str, timeout)
        if final_line is None:
            raise ProtocolError("No final response for UI:DISP:BITMAP:RLE")
        return self._parse_response(final_line, "UI:DISP:BITMAP:RLE")

    def flash_upload_rle(self, slot: int, rle_data: bytes,
                         timeout_ms: Optional[int] = None,
                         progress_cb=None,
                         use_crc: bool = False) -> Response:
        """
        Upload RLE-compressed image data to a flash slot.

        Protocol:
          1) Send UI:FLASH:UPLOAD:RLE <slot> <compressed_bytes> [CRC]
          2) Expect "OK READY <compressed_bytes>"
          3) Send compressed data [+ 4-byte CRC32 little-endian]
          4) Receive final OK/ERROR
        """
        if not self.is_connected():
            raise TransportError("Not connected")

        timeout = timeout_ms or max(self._timeout_ms, 30000)
        self._drain_pending()

        compressed_len = len(rle_data)
        cmd_str = f"UI:FLASH:UPLOAD:RLE {slot} {compressed_len}"
        if use_crc:
            cmd_str += " CRC"
        self._transport.send_line(cmd_str)

        line = self._recv_response(cmd_str, timeout)
        if line is None:
            raise ProtocolError("No READY response for UI:FLASH:UPLOAD:RLE")

        if line.startswith("{"):
            resp = self._parse_json_response(line, "UI:FLASH:UPLOAD:RLE")
            if resp.success:
                raise ProtocolError(
                    "Expected READY for UI:FLASH:UPLOAD:RLE, got JSON OK"
                )
            return resp

        resp = self._parse_ascii_response(line, "UI:FLASH:UPLOAD:RLE")
        ready_bytes = resp.data.get("ready_bytes")
        if ready_bytes is None:
            raise ProtocolError(f"Expected READY response, got: {line}")

        if compressed_len != ready_bytes:
            raise ProtocolError(
                f"RLE size mismatch: expected {ready_bytes}, "
                f"got {compressed_len}"
            )

        offset = 0
        while offset < compressed_len:
            chunk = rle_data[offset:offset + self._stream_chunk]
            self._send_chunk(chunk)
            offset += len(chunk)
            if progress_cb is not None:
                progress_cb(offset, compressed_len)

        if use_crc:
            crc = zlib.crc32(rle_data) & 0xFFFFFFFF
            self._transport.send(struct.pack('<I', crc))

        final_line = self._recv_response(cmd_str, timeout)
        if final_line is None:
            raise ProtocolError("No final response for UI:FLASH:UPLOAD:RLE")
        return self._parse_response(final_line, "UI:FLASH:UPLOAD:RLE")
