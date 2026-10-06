"""Tests for transport module."""

import pytest
from unittest.mock import Mock, patch

from smc_core.transport.base import ITransport, TransportInfo
from smc_core.transport.serial import VcpTransport


class TestVcpTransport:
    """Tests for VcpTransport class."""

    def test_init_default(self):
        """Test default initialization."""
        transport = VcpTransport()
        assert transport._port_name is None
        assert transport._baudrate == 115200
        assert not transport.is_connected()

    def test_init_with_port(self):
        """Test initialization with port."""
        transport = VcpTransport(port="COM3", baudrate=9600)
        assert transport._port_name == "COM3"
        assert transport._baudrate == 9600

    def test_get_info_disconnected(self):
        """Test get_info when disconnected."""
        transport = VcpTransport(port="COM3")
        info = transport.get_info()
        assert info.transport_type == "VCP"
        assert info.port == "COM3"
        assert not info.connected

    @patch("smc_core.transport.serial.serial.tools.list_ports.comports")
    def test_detect_ports_stm32(self, mock_comports):
        """Test STM32 port detection."""
        mock_port = Mock()
        mock_port.device = "COM3"
        mock_port.vid = 0x0483  # ST VID
        mock_port.description = "ST-LINK/V2-1"
        mock_port.manufacturer = "STMicroelectronics"
        mock_comports.return_value = [mock_port]

        ports = VcpTransport.detect_ports()
        assert "COM3" in ports

    @patch("smc_core.transport.serial.serial.tools.list_ports.comports")
    def test_detect_ports_empty(self, mock_comports):
        """Test detection with no STM32 ports."""
        mock_port = Mock()
        mock_port.device = "COM1"
        mock_port.vid = 0x1234
        mock_port.description = "Generic Serial"
        mock_port.manufacturer = "Unknown"
        mock_comports.return_value = [mock_port]

        ports = VcpTransport.detect_ports()
        assert len(ports) == 0


class TestPartialLines:
    """R4: a partial line at the deadline stays buffered instead of becoming a reply."""

    @staticmethod
    def _open(pending):
        t = VcpTransport(port="COM9")
        fake = Mock()
        fake.is_open = True
        fake.in_waiting = 0
        fake.read.side_effect = lambda n=1: pending.pop(0) if pending else b""
        t._serial = fake
        return t

    def test_partial_line_waits_for_its_end(self):
        pending = [b"OK REA"]
        t = self._open(pending)
        assert t.recv_line(timeout_ms=30) is None      # not "OK REA"
        pending.append(b"DY 96\r\n")
        assert t.recv_line(timeout_ms=200) == "OK READY 96"


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
