"""
Connection panel widget.

Provides controls for selecting serial port and connecting to the device.
"""

from PySide6.QtWidgets import (
    QWidget,
    QHBoxLayout,
    QVBoxLayout,
    QLabel,
    QComboBox,
    QPushButton,
    QGroupBox,
    QSpinBox,
    QCheckBox,
)
from PySide6.QtCore import Signal, Slot

from smc_core.transport.serial import VcpTransport
from smc_core.board import BOOT_BAUD, BoardConfig

VNUCLEO_DEFAULT = "tcp://127.0.0.1:5761"


class ConnectionPanel(QWidget):
    """Panel for connection settings and control."""

    # Signals
    connect_clicked = Signal()
    disconnect_clicked = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._connected = False
        self._setup_ui()
        self.refresh_ports()

    def _setup_ui(self):
        """Set up the user interface."""
        # Group box
        group = QGroupBox("Connection")
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(group)

        # Inner layout
        inner = QHBoxLayout(group)

        # Port selection
        port_label = QLabel("Port:")
        inner.addWidget(port_label)

        self._port_combo = QComboBox()
        self._port_combo.setMinimumWidth(120)
        self._port_combo.setEditable(True)        # type tcp://HOST:PORT for a virtual Nucleo
        self._port_combo.setToolTip("Serial port, or tcp://HOST:PORT for a virtual Nucleo")
        inner.addWidget(self._port_combo)

        # Refresh button
        self._refresh_btn = QPushButton("Refresh")
        self._refresh_btn.setToolTip("Refresh available ports")
        self._refresh_btn.clicked.connect(self.refresh_ports)
        inner.addWidget(self._refresh_btn)

        inner.addSpacing(20)

        # Link rate: the port opens at the board's boot rate (115200) and the session then moves
        # the link to the chosen rate (SYST:BAUD). Bench 2026-10-20 on the ST-LINK VCP, firmware
        # 0.6.4: clean both ways up to 2 Mbaud; scenarios and E-STOP pass at 1 Mbaud (ESTOP
        # confirmed in 16 ms instead of 94).
        baud_label = QLabel("Link:")
        inner.addWidget(baud_label)

        self._baud_combo = QComboBox()
        self._baud_combo.addItems(["1000000", "460800", "115200"])
        self._baud_combo.setCurrentText("1000000")
        self._baud_combo.setToolTip("Serial link rate after connecting. The board boots at 115200; the "
                                    "session switches to this rate (and back on disconnect). Firmware "
                                    "before 0.6.4 is clean only up to 460800")
        self._baud_combo.currentTextChanged.connect(self._follow_link_rate)
        inner.addWidget(self._baud_combo)

        inner.addSpacing(20)

        # Connect/Disconnect button
        self._connect_btn = QPushButton("Connect")
        self._connect_btn.setMinimumWidth(100)
        self._connect_btn.clicked.connect(self._on_connect_clicked)
        inner.addWidget(self._connect_btn)

        # Status indicator
        self._status_label = QLabel("Disconnected")
        self._status_label.setStyleSheet("color: gray;")
        inner.addWidget(self._status_label)

        inner.addSpacing(20)

        # Heartbeat configuration
        hb_label = QLabel("Heartbeat:")
        inner.addWidget(hb_label)

        self._hb_enable_cb = QCheckBox("Enable")
        self._hb_enable_cb.setChecked(True)
        self._hb_enable_cb.setToolTip("Enable heartbeat watchdog on connection")
        inner.addWidget(self._hb_enable_cb)

        self._hb_period_spin = QSpinBox()
        self._hb_period_spin.setRange(10, 200)
        self._hb_period_spin.setValue(int(BoardConfig.heartbeat_period_s * 1000))
        self._hb_period_spin.setSuffix(" ms")
        self._hb_period_spin.setToolTip("Heartbeat send interval (10-200 ms)")
        inner.addWidget(self._hb_period_spin)

        inner.addWidget(QLabel("Watchdog:"))
        self._wd_spin = QSpinBox()
        self._wd_spin.setRange(100, 5000)
        self._wd_spin.setSingleStep(100)
        self._wd_spin.setValue(BoardConfig.heartbeat_timeout_ms)
        self._wd_spin.setSuffix(" ms")
        self._wd_spin.setToolTip(
            "Firmware stops the motor this long after the last heartbeat (S-5). "
            "Widened automatically to 5 s around flash-writing commands.")
        inner.addWidget(self._wd_spin)

        inner.addWidget(QLabel("Telemetry:"))
        self._tlm_spin = QSpinBox()
        self._tlm_spin.setRange(20, 5000)
        self._tlm_spin.setSingleStep(50)
        self._tlm_spin.setValue(self.FAST_TELEMETRY_MS)
        self._tlm_spin.setSuffix(" ms")
        self._tlm_spin.setToolTip(
            "Telemetry push period; follows the Link rate (20 ms = 50 Hz on a fast link, 200 ms "
            "at 115200). A full status line is ~900 bytes: at 115200 the firmware floors the "
            "period at 139 ms (42 ms compact); at 1 Mbaud 20 ms uses under half the link.")
        inner.addWidget(self._tlm_spin)
        self._tlm_compact_cb = QCheckBox("Compact")
        self._tlm_compact_cb.setChecked(BoardConfig.telemetry_compact)
        self._tlm_compact_cb.setToolTip(
            "Push only the live fields (state, position, speed, Hi-Z, encoder count and "
            "velocity, following error, watchdog): ~170 bytes per push, so 50 ms fits the link. "
            "The full status is still refreshed once a second.")
        inner.addWidget(self._tlm_compact_cb)

        inner.addStretch()

    FAST_TELEMETRY_MS = 20    # 50 Hz: bench 2026-10-21 at 1 Mbaud, 266 samples in 5.8 s (7x 115200)

    @Slot(str)
    def _follow_link_rate(self, text: str):
        """The telemetry period goes with the link: as fast as the link carries."""
        fast = int(text) > BOOT_BAUD
        self._tlm_spin.setValue(self.FAST_TELEMETRY_MS if fast else BoardConfig.telemetry_period_ms)

    @property
    def baudrate(self) -> int:
        """The link rate to move to after connecting (115200: stay at the boot rate)."""
        return int(self._baud_combo.currentText())

    @property
    def heartbeat_enabled(self) -> bool:
        """Whether heartbeat should be enabled on connect."""
        return self._hb_enable_cb.isChecked()

    @property
    def heartbeat_period_ms(self) -> int:
        """Heartbeat period in milliseconds."""
        return self._hb_period_spin.value()

    @property
    def watchdog_ms(self) -> int:
        """Firmware heartbeat watchdog timeout in milliseconds."""
        return self._wd_spin.value()

    @property
    def telemetry_ms(self) -> int:
        """Telemetry push period in milliseconds."""
        return self._tlm_spin.value()

    @property
    def telemetry_compact(self) -> bool:
        """Compact telemetry pushes (SYST:TLM:FMT COMPACT)."""
        return self._tlm_compact_cb.isChecked()

    def set_connected(self, connected: bool):
        """Update connection state."""
        self._connected = connected

        if connected:
            self._connect_btn.setText("Disconnect")
            self._status_label.setText("Connected")
            self._status_label.setStyleSheet("color: green; font-weight: bold;")
            self._port_combo.setEnabled(False)
            self._baud_combo.setEnabled(False)
            self._refresh_btn.setEnabled(False)
            self._hb_enable_cb.setEnabled(False)
            self._hb_period_spin.setEnabled(False)
        else:
            self._connect_btn.setText("Connect")
            self._status_label.setText("Disconnected")
            self._status_label.setStyleSheet("color: gray;")
            self._port_combo.setEnabled(True)
            self._baud_combo.setEnabled(True)
            self._refresh_btn.setEnabled(True)
            self._hb_enable_cb.setEnabled(True)
            self._hb_period_spin.setEnabled(True)

    @Slot()
    def refresh_ports(self):
        """Refresh available serial ports."""
        current = self._port_combo.currentText()
        self._port_combo.clear()

        # Get all ports
        all_ports = VcpTransport.list_all_ports()
        stm32_ports = VcpTransport.detect_ports()

        for port in all_ports:
            device = port["device"]
            desc = port.get("description", "")

            # Mark STM32 ports
            if device in stm32_ports:
                display = f"{device} - {desc} [STM32]"
            else:
                display = f"{device} - {desc}" if desc else device

            self._port_combo.addItem(display, device)

        # Virtual Nucleo on its default port (sim/README.md)
        self._port_combo.addItem(f"{VNUCLEO_DEFAULT} - virtual Nucleo", VNUCLEO_DEFAULT)

        # Restore selection if possible
        if current:
            idx = self._port_combo.findData(current)
            if idx >= 0:
                self._port_combo.setCurrentIndex(idx)
            elif self._port_combo.count() > 0:
                # Try to select first STM32 port
                for i in range(self._port_combo.count()):
                    if self._port_combo.itemData(i) in stm32_ports:
                        self._port_combo.setCurrentIndex(i)
                        break

    @property
    def selected_port(self) -> str:
        """Selected port: the item's device name, or what the user typed."""
        text = self._port_combo.currentText().strip()
        idx = self._port_combo.currentIndex()
        if idx >= 0 and text == self._port_combo.itemText(idx):
            return self._port_combo.itemData(idx) or text
        return text.split(" - ")[0]

    @Slot()
    def _on_connect_clicked(self):
        """Handle connect/disconnect button click."""
        if self._connected:
            self.disconnect_clicked.emit()
        else:
            self.connect_clicked.emit()
