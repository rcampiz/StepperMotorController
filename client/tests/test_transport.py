"""Tests for transport module."""

import pytest
from unittest.mock import Mock, patch

import sys
sys.path.insert(0, str(Path(__file__).parent.parent / "src"))

from pathlib import Path
from transport.interface import ITransport, TransportInfo
from transport.vcp import VcpTransport


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

    @patch("transport.vcp.serial.tools.list_ports.comports")
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

    @patch("transport.vcp.serial.tools.list_ports.comports")
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


class TestProtocolParsing:
    """Tests for response parsing in MotorClient."""

    def test_parse_ascii_ok(self):
        """Test parsing ASCII OK response."""
        from protocol.client import MotorClient, Response

        # Create mock transport
        mock_transport = Mock(spec=ITransport)
        client = MotorClient(mock_transport)

        # Test parsing
        resp = client._parse_ascii_response("OK ENABLED", "ENABLE")
        assert resp.success
        assert resp.data.get("message") == "ENABLED"

    def test_parse_ascii_error(self):
        """Test parsing ASCII ERROR response."""
        from protocol.client import MotorClient

        mock_transport = Mock(spec=ITransport)
        client = MotorClient(mock_transport)

        resp = client._parse_ascii_response("ERROR dir must be 0 or 1", "MOVE")
        assert not resp.success
        assert "dir must be 0 or 1" in resp.error_message

    def test_parse_json_ok(self):
        """Test parsing JSON OK response."""
        from protocol.client import MotorClient

        mock_transport = Mock(spec=ITransport)
        client = MotorClient(mock_transport)

        json_resp = '{"status":"ok","command":"PING","data":{"seq":1,"rx_tick":100}}'
        resp = client._parse_json_response(json_resp, "PING")
        assert resp.success
        assert resp.data["seq"] == 1
        assert resp.data["rx_tick"] == 100

    def test_parse_json_error(self):
        """Test parsing JSON error response."""
        from protocol.client import MotorClient

        mock_transport = Mock(spec=ITransport)
        client = MotorClient(mock_transport)

        json_resp = '{"status":"error","command":"MOVE","code":"INVALID_ARG","message":"bad"}'
        resp = client._parse_json_response(json_resp, "MOVE")
        assert not resp.success
        assert resp.error_code == "INVALID_ARG"


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
