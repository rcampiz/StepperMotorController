"""
Launch the GUI:  python -m smc_gui [--sim] [--debug] [--log-file PATH]

--sim connects to an in-process FakeBoard (smc_core.sim) instead of a serial port, so
the whole GUI can be exercised without hardware.
"""

import argparse
import logging
import os
import sys

_CLIENT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _CLIENT not in sys.path:
    sys.path.insert(0, _CLIENT)


def _setup_logging(debug: bool, log_file: str = None) -> None:
    handlers = [logging.StreamHandler(sys.stderr)]
    if log_file:
        handlers.append(logging.FileHandler(log_file, mode="w"))
    logging.basicConfig(level=logging.DEBUG if debug else logging.INFO,
                        format="%(asctime)s.%(msecs)03d [%(name)s] %(levelname)s: %(message)s",
                        datefmt="%H:%M:%S", handlers=handlers)


def apply_dark_palette(app) -> None:
    from PySide6.QtGui import QColor, QPalette
    palette = QPalette()
    for role, rgb in ((QPalette.Window, (53, 53, 53)), (QPalette.WindowText, (255, 255, 255)),
                      (QPalette.Base, (25, 25, 25)), (QPalette.AlternateBase, (53, 53, 53)),
                      (QPalette.ToolTipBase, (255, 255, 255)), (QPalette.ToolTipText, (255, 255, 255)),
                      (QPalette.Text, (255, 255, 255)), (QPalette.Button, (53, 53, 53)),
                      (QPalette.ButtonText, (255, 255, 255)), (QPalette.BrightText, (255, 0, 0)),
                      (QPalette.Link, (42, 130, 218)), (QPalette.Highlight, (42, 130, 218)),
                      (QPalette.HighlightedText, (0, 0, 0))):
        palette.setColor(role, QColor(*rgb))
    for role in (QPalette.WindowText, QPalette.Text, QPalette.ButtonText):
        palette.setColor(QPalette.Disabled, role, QColor(127, 127, 127))
    app.setPalette(palette)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="smc-gui", description="Stepper motor controller GUI")
    ap.add_argument("--sim", action="store_true", help="connect to the in-process fake board")
    ap.add_argument("--debug", action="store_true", help="verbose logging")
    ap.add_argument("--log-file", help="also log to this file")
    args, qt_args = ap.parse_known_args(argv)
    _setup_logging(args.debug, args.log_file)

    from PySide6.QtCore import Qt
    from PySide6.QtWidgets import QApplication
    from smc_gui.main_window import MainWindow

    QApplication.setHighDpiScaleFactorRoundingPolicy(Qt.HighDpiScaleFactorRoundingPolicy.PassThrough)
    app = QApplication([sys.argv[0]] + qt_args)
    app.setApplicationName("STM32 Motor Controller")
    app.setApplicationVersion("0.2.0")
    app.setStyle("Fusion")
    apply_dark_palette(app)
    window = MainWindow()
    window.show()
    if args.sim:
        from smc_core.sim import FakeBoard
        from smc_core.transport.memory import MemoryTransport
        transport = MemoryTransport(name="sim")
        window._sim = FakeBoard(transport)          # keep it alive with the window
        window.attach_transport(transport, "simulator")
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
