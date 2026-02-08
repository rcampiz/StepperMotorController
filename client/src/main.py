#!/usr/bin/env python3
"""
STM32 Motor Controller Client

Command-line interface and entry point for the motor controller client.
Supports both CLI and GUI modes.
"""

import argparse
import os
import sys
from typing import Optional

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from transport import VcpTransport, RttTransport, PYLINK_AVAILABLE
from protocol import MotorClient, ResponseFormat


def launch_gui() -> int:
    """Launch the GUI application."""
    try:
        from PySide6.QtWidgets import QApplication
        from PySide6.QtCore import Qt
        from gui.main_window import MainWindow
    except ImportError:
        print("Error: PySide6 not installed. Install with: pip install PySide6", file=sys.stderr)
        return 1

    # Enable high DPI scaling
    QApplication.setHighDpiScaleFactorRoundingPolicy(
        Qt.HighDpiScaleFactorRoundingPolicy.PassThrough
    )

    app = QApplication(sys.argv)
    app.setApplicationName("STM32 Motor Controller")
    app.setOrganizationName("STMicro")
    app.setApplicationVersion("0.1.0")

    # Apply dark theme
    app.setStyle("Fusion")
    _apply_dark_palette(app)

    window = MainWindow()
    window.show()

    return app.exec()


def _apply_dark_palette(app):
    """Apply dark color palette to application."""
    from PySide6.QtGui import QPalette, QColor

    palette = QPalette()
    palette.setColor(QPalette.Window, QColor(53, 53, 53))
    palette.setColor(QPalette.WindowText, QColor(255, 255, 255))
    palette.setColor(QPalette.Base, QColor(25, 25, 25))
    palette.setColor(QPalette.AlternateBase, QColor(53, 53, 53))
    palette.setColor(QPalette.ToolTipBase, QColor(255, 255, 255))
    palette.setColor(QPalette.ToolTipText, QColor(255, 255, 255))
    palette.setColor(QPalette.Text, QColor(255, 255, 255))
    palette.setColor(QPalette.Button, QColor(53, 53, 53))
    palette.setColor(QPalette.ButtonText, QColor(255, 255, 255))
    palette.setColor(QPalette.BrightText, QColor(255, 0, 0))
    palette.setColor(QPalette.Link, QColor(42, 130, 218))
    palette.setColor(QPalette.Highlight, QColor(42, 130, 218))
    palette.setColor(QPalette.HighlightedText, QColor(0, 0, 0))
    palette.setColor(QPalette.Disabled, QPalette.WindowText, QColor(127, 127, 127))
    palette.setColor(QPalette.Disabled, QPalette.Text, QColor(127, 127, 127))
    palette.setColor(QPalette.Disabled, QPalette.ButtonText, QColor(127, 127, 127))
    app.setPalette(palette)


def list_ports() -> None:
    """List available serial ports and J-Link probes."""
    print("Available serial ports:")
    print("-" * 60)

    all_ports = VcpTransport.list_all_ports()
    stm32_ports = VcpTransport.detect_ports()

    for port in all_ports:
        marker = " [STM32]" if port["device"] in stm32_ports else ""
        print(f"  {port['device']}{marker}")
        if port["description"]:
            print(f"    Description: {port['description']}")
        if port["manufacturer"]:
            print(f"    Manufacturer: {port['manufacturer']}")
        if port["vid"] is not None:
            print(f"    VID:PID: {port['vid']:04X}:{port['pid']:04X}")
        print()

    if not all_ports:
        print("  No serial ports found.")

    # List J-Link probes if pylink is available
    print()
    print("Available J-Link probes (for RTT):")
    print("-" * 60)

    if not PYLINK_AVAILABLE:
        print("  pylink-square not installed. Install with: pip install pylink-square")
    else:
        jlinks = RttTransport.detect_jlinks()
        if jlinks:
            for jlink in jlinks:
                print(f"  Serial: {jlink['serial_no']}")
                print(f"    Product: {jlink['product']}")
                print()
        else:
            print("  No J-Link probes found.")


def interactive_mode(client: MotorClient) -> None:
    """Run interactive command mode."""
    print("Interactive mode. Type 'quit' to exit, 'help' for commands.")
    print("-" * 60)

    while True:
        try:
            cmd = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break

        if not cmd:
            continue

        if cmd.lower() in ("quit", "exit", "q"):
            break

        if cmd.lower() == "help":
            print("Local commands:")
            print("  quit, exit, q  - Exit interactive mode")
            print("  json           - Switch to JSON response format")
            print("  ascii          - Switch to ASCII response format")
            print("  status         - Show connection status")
            print()
            print("Device commands: (sent to motor controller)")
            print("  HELP           - Show device command help")
            print("  GET_STATUS     - Get device status")
            print("  PING <seq>     - Latency test")
            print("  MOVE <s> <d>   - Move steps in direction")
            print("  (etc.)")
            continue

        if cmd.lower() == "json":
            resp = client.set_format(ResponseFormat.JSON)
            print(f"Format set: {resp.raw}")
            continue

        if cmd.lower() == "ascii":
            resp = client.set_format(ResponseFormat.ASCII)
            print(f"Format set: {resp.raw}")
            continue

        if cmd.lower() == "status":
            info = client.transport.get_info()
            print(f"Transport: {info.transport_type}")
            print(f"Port: {info.port}")
            print(f"Connected: {info.connected}")
            print(f"Format: {client.format.value}")
            continue

        # Send command to device
        try:
            resp = client.send_command(cmd)
            if resp.success:
                if resp.data:
                    print(f"OK: {resp.data}")
                else:
                    print(f"OK")
            else:
                print(f"ERROR: {resp.error_message}")
        except Exception as e:
            print(f"Error: {e}")


def run_command(client: MotorClient, command: str) -> int:
    """Run a single command and print result."""
    try:
        resp = client.send_command(command)
        if resp.success:
            print(resp.raw)
            return 0
        else:
            print(f"ERROR: {resp.error_message}", file=sys.stderr)
            return 1
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1


def main() -> int:
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description="STM32 Motor Controller Client",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --list                    List available ports and J-Links
  %(prog)s -p COM3                   Connect to COM3 interactively (VCP)
  %(prog)s -p /dev/ttyACM0 -c "PING 1"   Send single command
  %(prog)s --json -c "GET_STATUS"    Get status in JSON format
  %(prog)s --rtt                     Connect via RTT (J-Link auto-detect)
  %(prog)s --rtt --jlink-serial 123456   Connect via specific J-Link
  %(prog)s --gui                     Launch graphical interface
""",
    )

    parser.add_argument(
        "-p", "--port",
        help="Serial port (e.g., COM3, /dev/ttyACM0). Auto-detects if not specified.",
    )
    parser.add_argument(
        "-b", "--baud",
        type=int,
        default=115200,
        help="Baud rate (default: 115200)",
    )
    parser.add_argument(
        "-c", "--command",
        help="Execute single command and exit",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Use JSON response format",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="List available serial ports and exit",
    )
    parser.add_argument(
        "-t", "--timeout",
        type=int,
        default=2000,
        help="Command timeout in ms (default: 2000)",
    )
    parser.add_argument(
        "--gui",
        action="store_true",
        help="Launch graphical user interface",
    )
    parser.add_argument(
        "--rtt",
        action="store_true",
        help="Use RTT transport instead of VCP (requires J-Link)",
    )
    parser.add_argument(
        "--jlink-serial",
        type=int,
        help="J-Link serial number (for RTT, auto-detects if not specified)",
    )

    args = parser.parse_args()

    # GUI mode
    if args.gui:
        return launch_gui()

    # List ports mode
    if args.list:
        list_ports()
        return 0

    # Create transport and client
    if args.rtt:
        if not PYLINK_AVAILABLE:
            print("Error: pylink-square not installed. Install with: pip install pylink-square", file=sys.stderr)
            return 1
        transport = RttTransport(
            serial_no=args.jlink_serial,
            timeout_ms=args.timeout,
        )
        connect_msg = f"Connecting via RTT (J-Link: {args.jlink_serial or 'auto'})..."
    else:
        transport = VcpTransport(
            port=args.port,
            baudrate=args.baud,
            timeout_ms=args.timeout,
        )
        connect_msg = f"Connecting to {args.port or 'auto-detected port'}..."

    client = MotorClient(transport)

    # Connect
    try:
        print(connect_msg)
        client.connect()
        info = transport.get_info()
        print(f"Connected via {info.transport_type} to {info.port}")
    except Exception as e:
        print(f"Connection failed: {e}", file=sys.stderr)
        return 1

    try:
        # Set format if requested
        if args.json:
            client.set_format(ResponseFormat.JSON)

        # Single command mode
        if args.command:
            return run_command(client, args.command)

        # Interactive mode
        interactive_mode(client)
        return 0

    finally:
        client.disconnect()
        print("Disconnected.")


if __name__ == "__main__":
    sys.exit(main())
