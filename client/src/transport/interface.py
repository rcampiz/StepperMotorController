"""
Transport interface for device communication.

Provides abstract base class for different transport mechanisms (VCP, RTT).
"""

from abc import ABC, abstractmethod
from typing import Optional
from dataclasses import dataclass


class TransportError(Exception):
    """Base exception for transport errors."""
    pass


class ConnectionError(TransportError):
    """Failed to connect to device."""
    pass


class TimeoutError(TransportError):
    """Operation timed out."""
    pass


@dataclass
class TransportInfo:
    """Information about a transport connection."""
    transport_type: str  # "VCP" or "RTT"
    port: str           # Port name or device identifier
    connected: bool
    details: dict       # Transport-specific details


class ITransport(ABC):
    """
    Abstract transport interface.

    Implementations must provide thread-safe read/write operations.
    All timeouts are in milliseconds.
    """

    @abstractmethod
    def open(self) -> bool:
        """
        Open the transport connection.

        Returns:
            True if connection successful, False otherwise.

        Raises:
            ConnectionError: If connection fails with an error.
        """
        pass

    @abstractmethod
    def close(self) -> None:
        """
        Close the transport connection.

        Safe to call even if not connected.
        """
        pass

    @abstractmethod
    def is_connected(self) -> bool:
        """
        Check if transport is currently connected.

        Returns:
            True if connected and ready for I/O.
        """
        pass

    @abstractmethod
    def send(self, data: bytes) -> int:
        """
        Send data to the device.

        Args:
            data: Bytes to send.

        Returns:
            Number of bytes actually sent.

        Raises:
            TransportError: If send fails.
        """
        pass

    @abstractmethod
    def send_line(self, line: str) -> int:
        """
        Send a line of text (appends CRLF).

        Args:
            line: Text line to send (without line ending).

        Returns:
            Number of bytes sent (including CRLF).

        Raises:
            TransportError: If send fails.
        """
        pass

    @abstractmethod
    def recv(self, max_bytes: int = 1024, timeout_ms: int = 1000) -> bytes:
        """
        Receive data from the device.

        Args:
            max_bytes: Maximum bytes to read.
            timeout_ms: Timeout in milliseconds (0 = non-blocking).

        Returns:
            Received bytes (may be empty if timeout with no data).

        Raises:
            TimeoutError: If timeout expires with no data.
            TransportError: If receive fails.
        """
        pass

    @abstractmethod
    def recv_line(self, timeout_ms: int = 1000) -> Optional[str]:
        """
        Receive a line of text (waits for LF).

        Args:
            timeout_ms: Timeout in milliseconds.

        Returns:
            Received line (without line ending), or None if timeout.

        Raises:
            TimeoutError: If timeout expires without complete line.
            TransportError: If receive fails.
        """
        pass

    @abstractmethod
    def available(self) -> int:
        """
        Check how many bytes are available to read.

        Returns:
            Number of bytes in receive buffer.
        """
        pass

    @abstractmethod
    def flush_input(self) -> None:
        """Discard any pending input data."""
        pass

    @abstractmethod
    def flush_output(self) -> None:
        """Wait for all output data to be sent."""
        pass

    @abstractmethod
    def get_info(self) -> TransportInfo:
        """
        Get information about this transport.

        Returns:
            TransportInfo with connection details.
        """
        pass
