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

from transport import VcpTransport


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
        self._port_combo.setToolTip("Select serial port")
        inner.addWidget(self._port_combo)

        # Refresh button
        self._refresh_btn = QPushButton("Refresh")
        self._refresh_btn.setToolTip("Refresh available ports")
        self._refresh_btn.clicked.connect(self.refresh_ports)
        inner.addWidget(self._refresh_btn)

        inner.addSpacing(20)

        # Baud rate
        baud_label = QLabel("Baud:")
        inner.addWidget(baud_label)

        self._baud_combo = QComboBox()
        self._baud_combo.addItems(["9600", "19200", "38400", "57600", "115200", "230400"])
        self._baud_combo.setCurrentText("115200")
        self._baud_combo.setToolTip("Serial baud rate")
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
        self._hb_period_spin.setValue(50)
        self._hb_period_spin.setSuffix(" ms")
        self._hb_period_spin.setToolTip("Heartbeat send interval (10-200 ms)")
        inner.addWidget(self._hb_period_spin)

        inner.addStretch()

    @property
    def selected_port(self) -> str:
        """Get selected port name."""
        return self._port_combo.currentText()

    @property
    def baudrate(self) -> int:
        """Get selected baud rate."""
        return int(self._baud_combo.currentText())

    @property
    def heartbeat_enabled(self) -> bool:
        """Whether heartbeat should be enabled on connect."""
        return self._hb_enable_cb.isChecked()

    @property
    def heartbeat_period_ms(self) -> int:
        """Heartbeat period in milliseconds."""
        return self._hb_period_spin.value()

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
        """Get currently selected port device name."""
        # Get the data (device name) not the display text
        return self._port_combo.currentData() or self._port_combo.currentText()

    @Slot()
    def _on_connect_clicked(self):
        """Handle connect/disconnect button click."""
        if self._connected:
            self.disconnect_clicked.emit()
        else:
            self.connect_clicked.emit()
