"""
RTT (Real-Time Transfer) transport implementation.

Uses pylink-square for SEGGER RTT communication over J-Link.
"""

import time
from typing import Optional, List
from .interface import (
    ITransport,
    TransportInfo,
    TransportError,
    ConnectionError,
    TimeoutError,
)

# Try to import pylink - it's optional
try:
    import pylink
    PYLINK_AVAILABLE = True
except ImportError:
    PYLINK_AVAILABLE = False


class RttTransport(ITransport):
    """
    SEGGER RTT transport using pylink-square.

    Connects to the STM32 via J-Link RTT for high-speed debug communication.
    This transport requires:
    - J-Link probe (or J-Link OB firmware on ST-LINK)
    - pylink-square package installed
    - Target running with RTT initialized
    """

    # Default settings
    DEFAULT_TIMEOUT_MS = 1000
    DEFAULT_RTT_CHANNEL = 0  # Channel 0 for data, Channel 1 for SystemView
    DEVICE_NAME = "STM32F401RE"

    def __init__(
        self,
        serial_no: Optional[int] = None,
        rtt_channel: int = DEFAULT_RTT_CHANNEL,
        timeout_ms: int = DEFAULT_TIMEOUT_MS,
    ):
        """
        Initialize RTT transport.

        Args:
            serial_no: J-Link serial number. If None, uses first available.
            rtt_channel: RTT channel to use (default 0).
            timeout_ms: Default read timeout in milliseconds.
        """
        if not PYLINK_AVAILABLE:
            raise ImportError(
                "pylink-square not installed. Install with: pip install pylink-square"
            )

        self._serial_no = serial_no
        self._rtt_channel = rtt_channel
        self._default_timeout_ms = timeout_ms
        self._jlink: Optional[pylink.JLink] = None
        self._connected = False
        self._line_buffer = b""
        self._device_name = self.DEVICE_NAME

    def open(self) -> bool:
        """Open the J-Link RTT connection."""
        if self._connected:
            return True

        try:
            # Create J-Link instance
            self._jlink = pylink.JLink()

            # Open connection to J-Link
            if self._serial_no is not None:
                self._jlink.open(serial_no=self._serial_no)
            else:
                self._jlink.open()

            # Set target device
            self._jlink.set_tif(pylink.enums.JLinkInterfaces.SWD)
            self._jlink.connect(self._device_name)

            # Reset target to ensure clean state (optional, can be disabled)
            # self._jlink.reset(halt=False)

            # Start RTT
            self._jlink.rtt_start()

            # Wait for RTT control block to be found
            max_wait = 3.0  # seconds
            start = time.time()
            while time.time() - start < max_wait:
                try:
                    num_up = self._jlink.rtt_get_num_up_buffers()
                    num_down = self._jlink.rtt_get_num_down_buffers()
                    if num_up > 0 and num_down > 0:
                        break
                except pylink.errors.JLinkRTTException:
                    pass
                time.sleep(0.1)
            else:
                raise ConnectionError(
                    "RTT control block not found. Is the target running with RTT initialized?"
                )

            self._connected = True
            self._line_buffer = b""
            return True

        except pylink.errors.JLinkException as e:
            self._cleanup()
            raise ConnectionError(f"J-Link connection failed: {e}")

    def close(self) -> None:
        """Close the J-Link RTT connection."""
        self._cleanup()

    def _cleanup(self) -> None:
        """Clean up J-Link resources."""
        if self._jlink is not None:
            try:
                if self._connected:
                    self._jlink.rtt_stop()
            except Exception:
                pass
            try:
                self._jlink.close()
            except Exception:
                pass
            self._jlink = None
        self._connected = False
        self._line_buffer = b""

    def is_connected(self) -> bool:
        """Check if RTT connection is active."""
        return self._connected and self._jlink is not None

    def send(self, data: bytes) -> int:
        """Send raw bytes to the device via RTT."""
        if not self.is_connected():
            raise TransportError("Not connected")

        try:
            # RTT write expects a list of bytes
            written = self._jlink.rtt_write(self._rtt_channel, list(data))
            return written
        except pylink.errors.JLinkException as e:
            raise TransportError(f"RTT write failed: {e}")

    def send_line(self, line: str) -> int:
        """Send a line of text with CRLF terminator."""
        data = (line + "\r\n").encode("utf-8")
        return self.send(data)

    def recv(self, max_bytes: int = 1024, timeout_ms: int = 1000) -> bytes:
        """Receive raw bytes from the device via RTT."""
        if not self.is_connected():
            raise TransportError("Not connected")

        start = time.time()
        timeout_sec = timeout_ms / 1000.0
        data = b""

        try:
            while True:
                # Try to read from RTT
                chunk = self._jlink.rtt_read(self._rtt_channel, max_bytes - len(data))
                if chunk:
                    data += bytes(chunk)
                    if len(data) >= max_bytes:
                        break

                # Check timeout
                elapsed = time.time() - start
                if elapsed >= timeout_sec:
                    if len(data) == 0 and timeout_ms > 0:
                        raise TimeoutError("RTT read timeout")
                    break

                # Small delay to avoid busy-waiting
                time.sleep(0.001)

            return data

        except pylink.errors.JLinkException as e:
            raise TransportError(f"RTT read failed: {e}")

    def recv_line(self, timeout_ms: int = 1000) -> Optional[str]:
        """
        Receive a line of text (waits for LF).

        Handles partial line buffering for efficiency.
        """
        if not self.is_connected():
            raise TransportError("Not connected")

        start = time.time()
        timeout_sec = timeout_ms / 1000.0

        try:
            while True:
                # Check if we already have a complete line in buffer
                if b"\n" in self._line_buffer:
                    line, self._line_buffer = self._line_buffer.split(b"\n", 1)
                    # Strip CR if present
                    if line.endswith(b"\r"):
                        line = line[:-1]
                    return line.decode("utf-8", errors="replace")

                # Check timeout
                elapsed = time.time() - start
                if elapsed >= timeout_sec:
                    if len(self._line_buffer) == 0:
                        return None
                    # Return partial line on timeout
                    line = self._line_buffer
                    self._line_buffer = b""
                    return line.decode("utf-8", errors="replace")

                # Read more data from RTT
                chunk = self._jlink.rtt_read(self._rtt_channel, 256)
                if chunk:
                    self._line_buffer += bytes(chunk)
                else:
                    # Small delay to avoid busy-waiting
                    time.sleep(0.001)

        except pylink.errors.JLinkException as e:
            raise TransportError(f"RTT read failed: {e}")

    def available(self) -> int:
        """Return number of bytes available to read."""
        if not self.is_connected():
            return 0

        # RTT doesn't have a direct "bytes available" query,
        # so we return buffered data count
        return len(self._line_buffer)

    def flush_input(self) -> None:
        """Discard pending input data."""
        if not self.is_connected():
            return

        self._line_buffer = b""

        # Read and discard any pending RTT data
        try:
            while True:
                chunk = self._jlink.rtt_read(self._rtt_channel, 1024)
                if not chunk:
                    break
        except pylink.errors.JLinkException:
            pass

    def flush_output(self) -> None:
        """Wait for output buffer to drain (RTT is immediate)."""
        # RTT writes are essentially immediate to the target's buffer
        pass

    def get_info(self) -> TransportInfo:
        """Get transport information."""
        details = {
            "rtt_channel": self._rtt_channel,
            "timeout_ms": self._default_timeout_ms,
            "device": self._device_name,
        }

        if self._jlink is not None and self._connected:
            try:
                details["jlink_serial"] = self._jlink.serial_number
                details["jlink_product"] = self._jlink.product_name
                details["target_connected"] = self._jlink.connected()
            except Exception:
                pass

        return TransportInfo(
            transport_type="RTT",
            port=f"J-Link:{self._serial_no or 'auto'}",
            connected=self.is_connected(),
            details=details,
        )

    @staticmethod
    def detect_jlinks() -> List[dict]:
        """
        Detect available J-Link probes.

        Returns:
            List of dicts with J-Link information.
        """
        if not PYLINK_AVAILABLE:
            return []

        jlinks = []
        try:
            # Get list of connected J-Links
            jlink = pylink.JLink()
            emulators = jlink.connected_emulators()

            for emu in emulators:
                jlinks.append({
                    "serial_no": emu.SerialNumber,
                    "product": emu.acProduct.decode("utf-8") if emu.acProduct else "Unknown",
                    "connection": emu.Connection,
                })

        except pylink.errors.JLinkException:
            pass

        return jlinks

    @staticmethod
    def is_available() -> bool:
        """Check if pylink-square is available."""
        return PYLINK_AVAILABLE
