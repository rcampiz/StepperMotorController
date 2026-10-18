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
    app.setOrganizationName("SMC")
    app.setApplicationName("STM32 Motor Controller")
    app.setApplicationVersion("0.2.0")
    app.setStyle("Fusion")
    from smc_gui.theme import apply_theme
    apply_theme(app)
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
