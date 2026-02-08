"""
VCP (Virtual COM Port) transport implementation.

Uses pyserial for UART communication over USB via the debugger interface.
"""

import logging
import time
import serial
import serial.tools.list_ports
from typing import Optional, List
from .interface import (
    ITransport,
    TransportInfo,
    TransportError,
    ConnectionError,
    TimeoutError,
)

log = logging.getLogger("transport.vcp")


class VcpTransport(ITransport):
    """
    Virtual COM Port transport using pyserial.

    Connects to the STM32 via USART2 routed through the on-board debugger USB.
    """

    # Default connection parameters
    DEFAULT_BAUD = 115200
    DEFAULT_TIMEOUT_MS = 50  # Very short for GUI responsiveness

    def __init__(
        self,
        port: Optional[str] = None,
        baudrate: int = DEFAULT_BAUD,
        timeout_ms: int = DEFAULT_TIMEOUT_MS,
    ):
        """
        Initialize VCP transport.

        Args:
            port: Serial port name (e.g., "COM3" or "/dev/ttyACM0").
                  If None, will auto-detect on open().
            baudrate: Baud rate (default 115200).
            timeout_ms: Default read timeout in milliseconds.
        """
        self._port_name = port
        self._baudrate = baudrate
        self._default_timeout_ms = timeout_ms
        self._serial: Optional[serial.Serial] = None
        self._line_buffer = b""

    def open(self) -> bool:
        """Open the serial port connection."""
        if self._serial is not None and self._serial.is_open:
            return True

        # Auto-detect port if not specified
        port = self._port_name
        if port is None:
            detected = self.detect_ports()
            if not detected:
                raise ConnectionError("No STM32 device detected")
            port = detected[0]

        try:
            self._serial = serial.Serial(
                port=port,
                baudrate=self._baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=self._default_timeout_ms / 1000.0,
                write_timeout=self._default_timeout_ms / 1000.0,
            )
            # Assert DTR — some USB-serial bridges (including ST-LINK VCP)
            # won't forward device TX to host unless DTR is high.
            self._serial.dtr = True
            self._port_name = port
            self._line_buffer = b""
            return True
        except serial.SerialException as e:
            raise ConnectionError(f"Failed to open {port}: {e}")

    def close(self) -> None:
        """Close the serial port connection."""
        if self._serial is not None:
            try:
                self._serial.close()
            except Exception:
                pass
            self._serial = None
        self._line_buffer = b""

    def is_connected(self) -> bool:
        """Check if serial port is open."""
        return self._serial is not None and self._serial.is_open

    def send(self, data: bytes) -> int:
        """Send raw bytes to the device."""
        if not self.is_connected():
            raise TransportError("Not connected")

        try:
            return self._serial.write(data)
        except serial.SerialException as e:
            raise TransportError(f"Send failed: {e}")

    def send_line(self, line: str) -> int:
        """Send a line of text with CRLF terminator."""
        log.debug("TX: %s", line)
        data = (line + "\r\n").encode("utf-8")
        return self.send(data)

    def recv(self, max_bytes: int = 1024, timeout_ms: int = 1000) -> bytes:
        """Receive raw bytes from the device."""
        if not self.is_connected():
            raise TransportError("Not connected")

        # Use deadline loop instead of changing serial.timeout
        deadline = time.monotonic() + timeout_ms / 1000.0
        data = b""

        try:
            while len(data) < max_bytes:
                if time.monotonic() >= deadline:
                    break
                waiting = self._serial.in_waiting
                if waiting > 0:
                    chunk = self._serial.read(min(waiting, max_bytes - len(data)))
                    data += chunk
                else:
                    chunk = self._serial.read(1)
                    if chunk:
                        data += chunk

            if len(data) == 0 and timeout_ms > 0:
                raise TimeoutError("Read timeout")
            return data
        except serial.SerialException as e:
            raise TransportError(f"Receive failed: {e}")

    def recv_line(self, timeout_ms: int = 1000) -> Optional[str]:
        """
        Receive a line of text (waits for LF).

        Uses a deadline loop with the port's native timeout to avoid
        calling SetCommTimeouts (which can cause byte loss on Windows).
        """
        if not self.is_connected():
            raise TransportError("Not connected")

        deadline = time.monotonic() + timeout_ms / 1000.0

        try:
            while True:
                # Check if we already have a complete line in buffer
                if b"\n" in self._line_buffer:
                    line, self._line_buffer = self._line_buffer.split(b"\n", 1)
                    # Strip CR if present
                    if line.endswith(b"\r"):
                        line = line[:-1]
                    decoded = line.decode("utf-8", errors="replace")
                    log.debug("RX: %s", decoded)
                    return decoded

                # Check overall deadline
                if time.monotonic() >= deadline:
                    if len(self._line_buffer) == 0:
                        return None
                    # Return partial line on timeout
                    line = self._line_buffer
                    self._line_buffer = b""
                    decoded = line.decode("utf-8", errors="replace")
                    log.debug("RX (partial): %s", decoded)
                    return decoded

                # Read more data: block for 1 byte using port's native
                # timeout (DEFAULT_TIMEOUT_MS), then drain any buffered bytes.
                chunk = self._serial.read(1)
                if len(chunk) == 0:
                    # Native timeout expired — loop to check deadline
                    continue

                # Grab any additional bytes already in the buffer
                waiting = self._serial.in_waiting
                if waiting > 0:
                    chunk += self._serial.read(waiting)

                self._line_buffer += chunk

        except serial.SerialException as e:
            raise TransportError(f"Receive failed: {e}")

    def available(self) -> int:
        """Return number of bytes in receive buffer."""
        if not self.is_connected():
            return 0
        return self._serial.in_waiting + len(self._line_buffer)

    def flush_input(self) -> None:
        """Discard pending input data."""
        if self.is_connected():
            self._serial.reset_input_buffer()
        self._line_buffer = b""

    def flush_output(self) -> None:
        """Wait for output buffer to drain."""
        if self.is_connected():
            self._serial.flush()

    def get_info(self) -> TransportInfo:
        """Get transport information."""
        return TransportInfo(
            transport_type="VCP",
            port=self._port_name or "auto",
            connected=self.is_connected(),
            details={
                "baudrate": self._baudrate,
                "timeout_ms": self._default_timeout_ms,
            },
        )

    @staticmethod
    def detect_ports() -> List[str]:
        """
        Detect available STM32 serial ports.

        Returns:
            List of port names that appear to be STM32 devices.
        """
        stm32_ports = []

        for port in serial.tools.list_ports.comports():
            # Check for ST-LINK VID:PID (0483:374B for ST-LINK V2-1)
            if port.vid == 0x0483:
                stm32_ports.append(port.device)
                continue

            # Check description for known strings
            desc = (port.description or "").lower()
            if "stm" in desc or "st-link" in desc or "stlink" in desc:
                stm32_ports.append(port.device)
                continue

            # Check manufacturer
            mfr = (port.manufacturer or "").lower()
            if "stmicroelectronics" in mfr:
                stm32_ports.append(port.device)
                continue

        return stm32_ports

    @staticmethod
    def list_all_ports() -> List[dict]:
        """
        List all available serial ports with details.

        Returns:
            List of dicts with port information.
        """
        ports = []
        for port in serial.tools.list_ports.comports():
            ports.append({
                "device": port.device,
                "description": port.description,
                "hwid": port.hwid,
                "vid": port.vid,
                "pid": port.pid,
                "manufacturer": port.manufacturer,
            })
        return ports
