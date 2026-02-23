"""
Display panel for sending images to the LCD.

Provides arrow image selection (64 rotations), preview, and two transfer modes:
  - Direct: stream RGB565 over serial to the LCD (slow, for testing)
  - Flash:  upload to NOR flash on the LCD board, then show instantly
"""

from PySide6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QLabel,
    QGroupBox,
    QPushButton,
    QSpinBox,
    QSlider,
    QProgressBar,
    QMessageBox,
    QComboBox,
    QCheckBox,
)
from PySide6.QtCore import Qt, Slot, Signal
from PySide6.QtGui import QImage, QPixmap
import logging

from gui.arrow_generator import (
    generate_arrow_image,
    image_to_rgb565,
    NUM_FRAMES,
    LCD_WIDTH,
    LCD_HEIGHT,
)

log = logging.getLogger("display_panel")


class DisplayPanel(QWidget):
    """Panel for selecting and sending arrow images to the LCD."""

    # Signal: request bitmap send (x, y, w, h, rgb565_bytes)
    bitmap_send_requested = Signal(int, int, int, int, bytes)

    # Signal: request flash upload of one slot (slot_index, rgb565_bytes)
    flash_upload_requested = Signal(int, bytes)

    # Signal: request all 64 frames uploaded to flash
    flash_upload_all_requested = Signal()

    # Signal: request display of a flash slot
    flash_show_requested = Signal(int)

    # Signal: request flash info query
    flash_info_requested = Signal()

    # Signal: request erase all flash slots
    flash_erase_requested = Signal()

    # Signal: request indicator draw (angle_deg, rotation_dir, has_translation)
    indicator_send_requested = Signal(int, int, bool)

    def __init__(self, parent=None):
        super().__init__(parent)

        # Cache: index -> (QPixmap for preview, rgb565 bytes)
        self._cache = {}

        self._setup_ui()
        self._update_preview(0)

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSpacing(8)

        # --- Arrow Selection Group ---
        sel_group = QGroupBox("Arrow Selection")
        sel_layout = QVBoxLayout(sel_group)

        # Slider + SpinBox row
        ctrl_row = QHBoxLayout()
        ctrl_row.addWidget(QLabel("Frame:"))

        self._slider = QSlider(Qt.Horizontal)
        self._slider.setRange(0, NUM_FRAMES - 1)
        self._slider.setValue(0)
        self._slider.setTickPosition(QSlider.TicksBelow)
        self._slider.setTickInterval(8)
        ctrl_row.addWidget(self._slider, 1)

        self._spinbox = QSpinBox()
        self._spinbox.setRange(0, NUM_FRAMES - 1)
        self._spinbox.setValue(0)
        self._spinbox.setFixedWidth(60)
        ctrl_row.addWidget(self._spinbox)

        self._angle_label = QLabel("0.0\u00b0")
        self._angle_label.setFixedWidth(60)
        ctrl_row.addWidget(self._angle_label)

        sel_layout.addLayout(ctrl_row)

        # Preview image
        self._preview_label = QLabel()
        self._preview_label.setAlignment(Qt.AlignCenter)
        self._preview_label.setMinimumSize(160, 213)  # 240:320 aspect at half
        self._preview_label.setStyleSheet(
            "QLabel { background-color: #1a1a1a; border: 1px solid #444; }")
        sel_layout.addWidget(self._preview_label, 1)

        layout.addWidget(sel_group, 1)

        # --- Flash Operations Group ---
        flash_group = QGroupBox("Flash Storage")
        flash_layout = QVBoxLayout(flash_group)

        # Flash info label
        self._flash_info_label = QLabel("Flash: not queried")
        self._flash_info_label.setStyleSheet("color: #888;")
        flash_layout.addWidget(self._flash_info_label)

        # Flash button row
        flash_btn_row = QHBoxLayout()

        self._flash_info_btn = QPushButton("Flash Info")
        self._flash_info_btn.setFixedWidth(90)
        flash_btn_row.addWidget(self._flash_info_btn)

        self._upload_all_btn = QPushButton("Upload All to Flash")
        flash_btn_row.addWidget(self._upload_all_btn)

        self._flash_show_btn = QPushButton("Show from Flash")
        flash_btn_row.addWidget(self._flash_show_btn)

        self._flash_erase_btn = QPushButton("Erase Flash")
        self._flash_erase_btn.setFixedWidth(100)
        flash_btn_row.addWidget(self._flash_erase_btn)

        flash_layout.addLayout(flash_btn_row)

        layout.addWidget(flash_group)

        # --- MCU Indicator Group ---
        ind_group = QGroupBox("Motion Indicator (MCU-rendered)")
        ind_layout = QVBoxLayout(ind_group)

        # Angle row: slider + spinbox + label
        angle_row = QHBoxLayout()
        angle_row.addWidget(QLabel("Angle:"))

        self._ind_angle_slider = QSlider(Qt.Horizontal)
        self._ind_angle_slider.setRange(0, 359)
        self._ind_angle_slider.setValue(0)
        self._ind_angle_slider.setTickPosition(QSlider.TicksBelow)
        self._ind_angle_slider.setTickInterval(45)
        angle_row.addWidget(self._ind_angle_slider, 1)

        self._ind_angle_spin = QSpinBox()
        self._ind_angle_spin.setRange(0, 359)
        self._ind_angle_spin.setValue(0)
        self._ind_angle_spin.setSuffix("\u00b0")
        self._ind_angle_spin.setFixedWidth(70)
        angle_row.addWidget(self._ind_angle_spin)

        ind_layout.addLayout(angle_row)

        # Options row: rotation direction + has_translation + buttons
        opts_row = QHBoxLayout()

        opts_row.addWidget(QLabel("Rotation:"))
        self._ind_rot_combo = QComboBox()
        self._ind_rot_combo.addItem("CCW", -1)
        self._ind_rot_combo.addItem("None", 0)
        self._ind_rot_combo.addItem("CW", 1)
        self._ind_rot_combo.setCurrentIndex(1)  # default: None
        self._ind_rot_combo.setFixedWidth(70)
        opts_row.addWidget(self._ind_rot_combo)

        self._ind_trans_cb = QCheckBox("Translation")
        self._ind_trans_cb.setChecked(True)
        opts_row.addWidget(self._ind_trans_cb)

        opts_row.addStretch()

        self._ind_send_btn = QPushButton("Send Indicator")
        self._ind_send_btn.setFixedWidth(120)
        opts_row.addWidget(self._ind_send_btn)

        self._ind_clear_btn = QPushButton("Clear")
        self._ind_clear_btn.setFixedWidth(60)
        opts_row.addWidget(self._ind_clear_btn)

        ind_layout.addLayout(opts_row)

        layout.addWidget(ind_group)

        # --- Direct Transfer Group ---
        xfer_group = QGroupBox("Direct Transfer (bypass flash)")
        xfer_layout = QVBoxLayout(xfer_group)

        # Info row
        total_bytes = LCD_WIDTH * LCD_HEIGHT * 2
        info_text = (f"Image: {LCD_WIDTH}\u00d7{LCD_HEIGHT} RGB565  "
                     f"({total_bytes:,} bytes)")
        xfer_layout.addWidget(QLabel(info_text))

        # Send button + progress bar
        btn_row = QHBoxLayout()
        self._send_btn = QPushButton("Send Direct to LCD")
        self._send_btn.setFixedWidth(140)
        btn_row.addWidget(self._send_btn)

        self._progress = QProgressBar()
        self._progress.setRange(0, 100)
        self._progress.setValue(0)
        self._progress.setTextVisible(True)
        btn_row.addWidget(self._progress, 1)

        xfer_layout.addLayout(btn_row)

        layout.addWidget(xfer_group)

        # --- Status Label ---
        self._status_label = QLabel("Ready")
        self._status_label.setStyleSheet("color: #888;")
        layout.addWidget(self._status_label)

        # --- Connections ---
        self._slider.valueChanged.connect(self._on_slider_changed)
        self._spinbox.valueChanged.connect(self._on_spinbox_changed)
        self._send_btn.clicked.connect(self._on_send_clicked)
        self._flash_info_btn.clicked.connect(self._on_flash_info_clicked)
        self._upload_all_btn.clicked.connect(self._on_upload_all_clicked)
        self._flash_show_btn.clicked.connect(self._on_flash_show_clicked)
        self._flash_erase_btn.clicked.connect(self._on_flash_erase_clicked)

        # Indicator controls
        self._ind_angle_slider.valueChanged.connect(self._on_ind_angle_slider)
        self._ind_angle_spin.valueChanged.connect(self._on_ind_angle_spin)
        self._ind_send_btn.clicked.connect(self._on_ind_send_clicked)
        self._ind_clear_btn.clicked.connect(self._on_ind_clear_clicked)

    # --- Slider / SpinBox sync ---

    @Slot(int)
    def _on_slider_changed(self, value):
        self._spinbox.blockSignals(True)
        self._spinbox.setValue(value)
        self._spinbox.blockSignals(False)
        self._update_preview(value)

    @Slot(int)
    def _on_spinbox_changed(self, value):
        self._slider.blockSignals(True)
        self._slider.setValue(value)
        self._slider.blockSignals(False)
        self._update_preview(value)

    def _update_preview(self, index):
        """Update the preview image for the given frame index."""
        angle = index * 360.0 / NUM_FRAMES
        self._angle_label.setText(f"{angle:.1f}\u00b0")

        pixmap, _ = self._get_cached(index)
        scaled = pixmap.scaled(
            self._preview_label.size(),
            Qt.KeepAspectRatio,
            Qt.SmoothTransformation,
        )
        self._preview_label.setPixmap(scaled)

    def _get_cached(self, index):
        """Get or generate cached (QPixmap, rgb565_bytes) for frame index."""
        if index not in self._cache:
            img = generate_arrow_image(index)
            rgb565 = image_to_rgb565(img)

            rgb_data = img.tobytes("raw", "RGB")
            qimg = QImage(rgb_data, LCD_WIDTH, LCD_HEIGHT, LCD_WIDTH * 3,
                          QImage.Format_RGB888)
            pixmap = QPixmap.fromImage(qimg)

            self._cache[index] = (pixmap, rgb565)

        return self._cache[index]

    def get_frame_rgb565(self, index):
        """Get the RGB565 bytes for a frame index (public, for upload-all)."""
        _, rgb565 = self._get_cached(index)
        return rgb565

    # --- Direct Send ---

    @Slot()
    def _on_send_clicked(self):
        """Handle Send Direct to LCD button click."""
        index = self._slider.value()
        _, rgb565 = self._get_cached(index)

        self._send_btn.setEnabled(False)
        self._progress.setValue(0)
        angle = index * 360.0 / NUM_FRAMES
        self._set_status(f"Sending frame {index} ({angle:.1f}\u00b0)...", "blue")

        self.bitmap_send_requested.emit(0, 0, LCD_WIDTH, LCD_HEIGHT, rgb565)

    # --- Flash Operations ---

    @Slot()
    def _on_flash_info_clicked(self):
        self._set_status("Querying flash info...", "blue")
        self.flash_info_requested.emit()

    @Slot()
    def _on_upload_all_clicked(self):
        self._set_buttons_enabled(False)
        self._progress.setValue(0)
        self._set_status("Starting upload of all 64 frames...", "blue")
        self.flash_upload_all_requested.emit()

    @Slot()
    def _on_flash_show_clicked(self):
        index = self._slider.value()
        self._set_status(f"Showing frame {index} from flash...", "blue")
        self.flash_show_requested.emit(index)

    @Slot()
    def _on_flash_erase_clicked(self):
        reply = QMessageBox.question(
            self, "Erase Flash",
            "Erase ALL image slots from NOR flash?\n"
            "This cannot be undone.",
            QMessageBox.Yes | QMessageBox.No, QMessageBox.No)
        if reply == QMessageBox.Yes:
            self._set_status("Erasing flash...", "blue")
            self.flash_erase_requested.emit()

    # --- Indicator controls ---

    @Slot(int)
    def _on_ind_angle_slider(self, value):
        self._ind_angle_spin.blockSignals(True)
        self._ind_angle_spin.setValue(value)
        self._ind_angle_spin.blockSignals(False)

    @Slot(int)
    def _on_ind_angle_spin(self, value):
        self._ind_angle_slider.blockSignals(True)
        self._ind_angle_slider.setValue(value)
        self._ind_angle_slider.blockSignals(False)

    @Slot()
    def _on_ind_send_clicked(self):
        angle = self._ind_angle_spin.value()
        rot_dir = self._ind_rot_combo.currentData()
        has_trans = self._ind_trans_cb.isChecked()
        self._set_status(
            f"Indicator: {angle}\u00b0 rot={rot_dir} trans={has_trans}", "blue")
        self.indicator_send_requested.emit(angle, rot_dir, has_trans)

    @Slot()
    def _on_ind_clear_clicked(self):
        self._set_status("Clearing indicator...", "blue")
        self.indicator_send_requested.emit(0, 0, False)

    # --- Public slots for progress feedback ---

    @Slot(int, int)
    def update_progress(self, bytes_sent, total_bytes):
        """Update transfer progress bar."""
        if total_bytes > 0:
            pct = int(bytes_sent * 100 / total_bytes)
            self._progress.setValue(pct)
            self._status_label.setText(
                f"Transferring... {bytes_sent:,}/{total_bytes:,} bytes")

    @Slot(int, int)
    def update_upload_progress(self, slot_done, total_slots):
        """Update progress for upload-all operation."""
        if total_slots > 0:
            pct = int(slot_done * 100 / total_slots)
            self._progress.setValue(pct)
            self._status_label.setText(
                f"Uploading {slot_done}/{total_slots}...")

    @Slot(bool, str)
    def transfer_complete(self, success, message=""):
        """Handle transfer/operation completion."""
        self._set_buttons_enabled(True)
        if success:
            self._progress.setValue(100)
            self._set_status(message or "Done", "green")
        else:
            self._set_status(message or "Failed", "red")

    @Slot(str)
    def update_flash_info(self, info_text):
        """Update the flash info label."""
        self._flash_info_label.setText(info_text)
        self._flash_info_label.setStyleSheet("color: #ccc;")

    # --- Helpers ---

    def _set_status(self, text, color="gray"):
        colors = {
            "gray": "#888",
            "blue": "#4a9eff",
            "green": "#4CAF50",
            "red": "#F44336",
        }
        self._status_label.setText(text)
        self._status_label.setStyleSheet(f"color: {colors.get(color, color)};")

    def _set_buttons_enabled(self, enabled):
        self._send_btn.setEnabled(enabled)
        self._upload_all_btn.setEnabled(enabled)
        self._flash_show_btn.setEnabled(enabled)
        self._flash_erase_btn.setEnabled(enabled)
        self._ind_send_btn.setEnabled(enabled)
        self._ind_clear_btn.setEnabled(enabled)
