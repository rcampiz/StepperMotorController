#!/usr/bin/env python3
"""Quick automated GUI connection test — opens window, connects, waits for telemetry, closes."""

import sys
import logging
import time

logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s.%(msecs)03d [%(name)s] %(levelname)s: %(message)s",
    datefmt="%H:%M:%S",
    stream=sys.stdout,
)
log = logging.getLogger("test_gui")

from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QTimer

from gui.main_window import MainWindow

app = QApplication(sys.argv)
window = MainWindow()
window.show()

# Track telemetry updates
telemetry_count = [0]


def on_status_updated(data):
    telemetry_count[0] += 1
    state = data.get("state", "?")
    tick = data.get("tick", 0)
    log.info("Telemetry #%d: state=%s tick=%s", telemetry_count[0], state, tick)
    if telemetry_count[0] >= 3:
        log.info("=== PASS: Got %d telemetry updates, closing ===", telemetry_count[0])
        window.close()


window.status_updated.connect(on_status_updated)

# Auto-connect after 500ms
def auto_connect():
    panel = window._connection_panel
    # Set port to COM6
    idx = panel._port_combo.findText("COM6", flags=0x0)
    if idx < 0:
        # Try finding it by partial match
        for i in range(panel._port_combo.count()):
            if "COM6" in panel._port_combo.itemText(i):
                idx = i
                break
    if idx >= 0:
        panel._port_combo.setCurrentIndex(idx)
    log.info("Auto-connecting to %s...", panel.selected_port)
    panel.connect_clicked.emit()


QTimer.singleShot(500, auto_connect)

# Timeout after 15 seconds
QTimer.singleShot(15000, lambda: (
    log.error("=== TIMEOUT: Only got %d telemetry updates ===" % telemetry_count[0]),
    window.close(),
))

sys.exit(app.exec())
