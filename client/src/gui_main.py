#!/usr/bin/env python3
"""
STM32 Motor Controller GUI Client

Launches the PySide6 graphical interface for the motor controller.
"""

import sys
import logging
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import Qt

from gui.main_window import MainWindow


def _setup_logging():
    """Configure logging to stderr and optionally to file."""
    fmt = "%(asctime)s.%(msecs)03d [%(name)s] %(levelname)s: %(message)s"
    datefmt = "%H:%M:%S"

    handlers = [logging.StreamHandler(sys.stderr)]

    # Also log to file if --log-file is specified
    for i, arg in enumerate(sys.argv):
        if arg == "--log-file" and i + 1 < len(sys.argv):
            handlers.append(logging.FileHandler(sys.argv[i + 1], mode="w"))
            break

    level = logging.DEBUG if "--debug" in sys.argv else logging.INFO
    logging.basicConfig(level=level, format=fmt, datefmt=datefmt,
                        handlers=handlers)


def main() -> int:
    """Main entry point for GUI application."""
    _setup_logging()

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

    # Create and show main window
    window = MainWindow()
    window.show()

    return app.exec()


def _apply_dark_palette(app: QApplication):
    """Apply dark color palette to application."""
    from PySide6.QtGui import QPalette, QColor

    palette = QPalette()

    # Base colors
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

    # Disabled colors
    palette.setColor(QPalette.Disabled, QPalette.WindowText, QColor(127, 127, 127))
    palette.setColor(QPalette.Disabled, QPalette.Text, QColor(127, 127, 127))
    palette.setColor(QPalette.Disabled, QPalette.ButtonText, QColor(127, 127, 127))

    app.setPalette(palette)


if __name__ == "__main__":
    sys.exit(main())
