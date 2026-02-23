"""
Display panel for sending images to the LCD.

Provides image file loading (JPEG/PNG/SVG), preview, and two transfer modes:
  - Direct: stream RLE-compressed RGB565 over serial to the LCD
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
    QLineEdit,
    QFileDialog,
)
from PySide6.QtCore import Qt, Slot, Signal
from PySide6.QtGui import QImage, QPixmap
import logging

from gui.image_loader import (
    load_image,
    resize_for_lcd,
    crop_for_lcd,
    rotate_image,
    image_to_rgb565,
    SUPPORTED_FILTER,
    LCD_WIDTH,
    LCD_HEIGHT,
)
from gui.rle_codec import encode_rle_rgb565

log = logging.getLogger("display_panel")


class DisplayPanel(QWidget):
    """Panel for loading images and sending them to the LCD."""

    # Signal: request direct-to-LCD send (rgb565_bytes, rle_bytes)
    image_send_requested = Signal(bytes, bytes)

    # Signal: request flash upload (slot, rgb565_bytes, rle_bytes)
    image_flash_upload_requested = Signal(int, bytes, bytes)

    # Signal: request display of a flash slot
    flash_show_requested = Signal(int)

    # Signal: request flash info query
    flash_info_requested = Signal()

    # Signal: request erase all flash slots
    flash_erase_requested = Signal()

    # Signal: request flash diagnostics (FLASH_TEST)
    flash_diag_requested = Signal()

    # Signal: request indicator draw (angle_deg, rotation_dir, has_translation)
    indicator_send_requested = Signal(int, int, bool)

    def __init__(self, parent=None):
        super().__init__(parent)

        self._original_image = None  # PIL Image (original loaded, before transform)
        self._loaded_pixmap = None   # QPixmap for preview
        self._loaded_rgb565 = None   # bytes: raw RGB565 data
        self._loaded_rle = None      # bytes: RLE-compressed data
        self._max_slots = 53         # Updated by flash info query

        self._setup_ui()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSpacing(8)

        # --- Image Upload Group ---
        img_group = QGroupBox("Image Upload")
        img_layout = QVBoxLayout(img_group)

        # File picker row
        file_row = QHBoxLayout()
        self._file_path = QLineEdit()
        self._file_path.setReadOnly(True)
        self._file_path.setPlaceholderText("No image loaded")
        file_row.addWidget(self._file_path, 1)

        self._browse_btn = QPushButton("Browse...")
        self._browse_btn.setFixedWidth(80)
        file_row.addWidget(self._browse_btn)

        img_layout.addLayout(file_row)

        # Transform controls row
        xform_row = QHBoxLayout()

        xform_row.addWidget(QLabel("Rotate:"))
        self._rotate_combo = QComboBox()
        self._rotate_combo.addItem("0\u00b0", 0)
        self._rotate_combo.addItem("90\u00b0 CCW", 90)
        self._rotate_combo.addItem("180\u00b0", 180)
        self._rotate_combo.addItem("90\u00b0 CW", 270)
        self._rotate_combo.setFixedWidth(85)
        xform_row.addWidget(self._rotate_combo)

        xform_row.addWidget(QLabel("Scaling:"))
        self._crop_combo = QComboBox()
        self._crop_combo.addItem("Fit (Letterbox)", "fit")
        self._crop_combo.addItem("Fill (Crop)", "fill")
        self._crop_combo.setFixedWidth(120)
        xform_row.addWidget(self._crop_combo)

        xform_row.addStretch()

        img_layout.addLayout(xform_row)

        # Preview image
        self._preview_label = QLabel()
        self._preview_label.setAlignment(Qt.AlignCenter)
        self._preview_label.setMinimumSize(160, 213)  # 240:320 aspect at half
        self._preview_label.setStyleSheet(
            "QLabel { background-color: #1a1a1a; border: 1px solid #444; }")
        self._preview_label.setText("No image loaded")
        img_layout.addWidget(self._preview_label, 1)

        # Image info label
        self._img_info_label = QLabel("")
        self._img_info_label.setStyleSheet("color: #888;")
        img_layout.addWidget(self._img_info_label)

        # Transfer buttons row
        xfer_row = QHBoxLayout()

        self._send_btn = QPushButton("Send Direct to LCD")
        self._send_btn.setFixedWidth(140)
        self._send_btn.setEnabled(False)
        xfer_row.addWidget(self._send_btn)

        self._flash_upload_btn = QPushButton("Upload to Flash")
        self._flash_upload_btn.setFixedWidth(120)
        self._flash_upload_btn.setEnabled(False)
        xfer_row.addWidget(self._flash_upload_btn)

        xfer_row.addWidget(QLabel("Slot:"))
        self._slot_spin = QSpinBox()
        self._slot_spin.setRange(0, self._max_slots - 1)
        self._slot_spin.setValue(0)
        self._slot_spin.setFixedWidth(60)
        xfer_row.addWidget(self._slot_spin)

        self._progress = QProgressBar()
        self._progress.setRange(0, 100)
        self._progress.setValue(0)
        self._progress.setTextVisible(True)
        xfer_row.addWidget(self._progress, 1)

        img_layout.addLayout(xfer_row)

        layout.addWidget(img_group, 1)

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

        flash_btn_row.addWidget(QLabel("Show Slot:"))
        self._flash_show_spin = QSpinBox()
        self._flash_show_spin.setRange(0, self._max_slots - 1)
        self._flash_show_spin.setValue(0)
        self._flash_show_spin.setFixedWidth(60)
        flash_btn_row.addWidget(self._flash_show_spin)

        self._flash_show_btn = QPushButton("Show from Flash")
        flash_btn_row.addWidget(self._flash_show_btn)

        self._flash_erase_btn = QPushButton("Erase Flash")
        self._flash_erase_btn.setFixedWidth(100)
        flash_btn_row.addWidget(self._flash_erase_btn)

        self._flash_diag_btn = QPushButton("Diagnostics")
        self._flash_diag_btn.setFixedWidth(100)
        self._flash_diag_btn.setToolTip("Run FLASH_TEST: erase/write/read-back self-test on slot 0")
        flash_btn_row.addWidget(self._flash_diag_btn)

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

        self._ind_auto_cb = QCheckBox("Auto")
        self._ind_auto_cb.setChecked(False)
        self._ind_auto_cb.setToolTip("Automatically send indicator on any change")
        opts_row.addWidget(self._ind_auto_cb)

        opts_row.addStretch()

        self._ind_send_btn = QPushButton("Send Indicator")
        self._ind_send_btn.setFixedWidth(120)
        opts_row.addWidget(self._ind_send_btn)

        self._ind_clear_btn = QPushButton("Clear")
        self._ind_clear_btn.setFixedWidth(60)
        opts_row.addWidget(self._ind_clear_btn)

        ind_layout.addLayout(opts_row)

        layout.addWidget(ind_group)

        # --- Status Label ---
        self._status_label = QLabel("Ready")
        self._status_label.setStyleSheet("color: #888;")
        layout.addWidget(self._status_label)

        # --- Connections ---
        self._browse_btn.clicked.connect(self._on_browse_clicked)
        self._send_btn.clicked.connect(self._on_send_clicked)
        self._flash_upload_btn.clicked.connect(self._on_flash_upload_clicked)
        self._flash_info_btn.clicked.connect(self._on_flash_info_clicked)
        self._flash_show_btn.clicked.connect(self._on_flash_show_clicked)
        self._flash_erase_btn.clicked.connect(self._on_flash_erase_clicked)
        self._flash_diag_btn.clicked.connect(self._on_flash_diag_clicked)

        # Transform controls
        self._rotate_combo.currentIndexChanged.connect(self._on_transform_changed)
        self._crop_combo.currentIndexChanged.connect(self._on_transform_changed)

        # Indicator controls
        self._ind_angle_slider.valueChanged.connect(self._on_ind_angle_slider)
        self._ind_angle_spin.valueChanged.connect(self._on_ind_angle_spin)
        self._ind_send_btn.clicked.connect(self._on_ind_send_clicked)
        self._ind_clear_btn.clicked.connect(self._on_ind_clear_clicked)
        self._ind_rot_combo.currentIndexChanged.connect(self._on_ind_auto_maybe)
        self._ind_trans_cb.toggled.connect(self._on_ind_auto_maybe)
        self._ind_angle_slider.valueChanged.connect(self._on_ind_auto_maybe)

    # --- Image Loading ---

    @Slot()
    def _on_browse_clicked(self):
        """Open file dialog to select an image."""
        path, _ = QFileDialog.getOpenFileName(
            self, "Select Image", "", SUPPORTED_FILTER)
        if not path:
            return

        try:
            self._set_status("Loading image...", "blue")
            self._original_image = load_image(path)
            self._file_path.setText(path)
            self._reprocess_image()
            self._set_status("Image loaded", "green")

        except Exception as e:
            log.error("Failed to load image: %s", e)
            self._set_status(f"Load failed: {e}", "red")
            self._original_image = None
            self._loaded_pixmap = None
            self._loaded_rgb565 = None
            self._loaded_rle = None
            self._send_btn.setEnabled(False)
            self._flash_upload_btn.setEnabled(False)

    @Slot()
    def _on_transform_changed(self, *_args):
        """Re-process image when rotation or crop mode changes."""
        if self._original_image is None:
            return
        try:
            self._reprocess_image()
            self._set_status("Image updated", "green")
        except Exception as e:
            log.error("Failed to reprocess image: %s", e)
            self._set_status(f"Transform failed: {e}", "red")

    def _reprocess_image(self):
        """Apply rotation and crop/fit, then update preview and cached data."""
        img = self._original_image
        orig_w, orig_h = img.size

        # Apply rotation
        degrees = self._rotate_combo.currentData()
        if degrees:
            img = rotate_image(img, degrees)

        # Apply scaling mode
        mode = self._crop_combo.currentData()
        if mode == "fill":
            final = crop_for_lcd(img)
        else:
            final = resize_for_lcd(img)

        rgb565 = image_to_rgb565(final)
        rle = encode_rle_rgb565(rgb565)

        # Convert PIL image to QPixmap for preview
        rgb_data = final.tobytes("raw", "RGB")
        qimg = QImage(rgb_data, LCD_WIDTH, LCD_HEIGHT, LCD_WIDTH * 3,
                      QImage.Format_RGB888)
        pixmap = QPixmap.fromImage(qimg)

        self._loaded_pixmap = pixmap
        self._loaded_rgb565 = rgb565
        self._loaded_rle = rle

        # Update preview
        scaled = pixmap.scaled(
            self._preview_label.size(),
            Qt.KeepAspectRatio,
            Qt.SmoothTransformation,
        )
        self._preview_label.setPixmap(scaled)

        # Update info label
        raw_kb = len(rgb565) / 1024
        rle_kb = len(rle) / 1024
        ratio = len(rle) * 100 / len(rgb565) if len(rgb565) > 0 else 0
        rot_str = f" rot {degrees}\u00b0" if degrees else ""
        mode_str = "crop" if mode == "fill" else "fit"
        self._img_info_label.setText(
            f"{orig_w}\u00d7{orig_h}{rot_str} \u2192 "
            f"{LCD_WIDTH}\u00d7{LCD_HEIGHT} ({mode_str}) "
            f"\u2014 {raw_kb:.1f} KB raw, {rle_kb:.1f} KB RLE ({ratio:.0f}%)")
        self._img_info_label.setStyleSheet("color: #ccc;")

        # Enable transfer buttons
        self._send_btn.setEnabled(True)
        self._flash_upload_btn.setEnabled(True)

    # --- Direct Send ---

    @Slot()
    def _on_send_clicked(self):
        """Handle Send Direct to LCD button click."""
        if self._loaded_rgb565 is None or self._loaded_rle is None:
            self._set_status("No image loaded", "red")
            return

        self._set_buttons_enabled(False)
        self._progress.setValue(0)
        self._set_status("Sending image to LCD...", "blue")
        self.image_send_requested.emit(self._loaded_rgb565, self._loaded_rle)

    # --- Flash Upload ---

    @Slot()
    def _on_flash_upload_clicked(self):
        """Handle Upload to Flash button click."""
        if self._loaded_rgb565 is None or self._loaded_rle is None:
            self._set_status("No image loaded", "red")
            return

        slot = self._slot_spin.value()
        self._set_buttons_enabled(False)
        self._progress.setValue(0)
        self._set_status(f"Uploading to flash slot {slot}...", "blue")
        self.image_flash_upload_requested.emit(
            slot, self._loaded_rgb565, self._loaded_rle)

    # --- Flash Operations ---

    @Slot()
    def _on_flash_info_clicked(self):
        self._set_status("Querying flash info...", "blue")
        self.flash_info_requested.emit()

    @Slot()
    def _on_flash_show_clicked(self):
        slot = self._flash_show_spin.value()
        self._set_status(f"Showing slot {slot} from flash...", "blue")
        self.flash_show_requested.emit(slot)

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

    @Slot()
    def _on_flash_diag_clicked(self):
        self._set_status("Running flash diagnostics...", "blue")
        self.flash_diag_requested.emit()

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
    def _on_ind_auto_maybe(self, *_args):
        if self._ind_auto_cb.isChecked():
            self._on_ind_send_clicked()

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
        self._set_status("Flash info received", "green")

    @Slot(int)
    def update_max_slots(self, max_slots):
        """Update the max flash slot count (from flash info query)."""
        self._max_slots = max_slots
        self._slot_spin.setRange(0, max(0, max_slots - 1))
        self._flash_show_spin.setRange(0, max(0, max_slots - 1))

    @Slot(bool, str)
    def show_diag_result(self, success, message):
        """Show flash diagnostics result in a message box."""
        if success:
            QMessageBox.information(self, "Flash Diagnostics", message)
            self._set_status("Flash test PASSED", "green")
        else:
            QMessageBox.warning(self, "Flash Diagnostics", message)
            self._set_status("Flash test FAILED", "red")

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
        has_image = self._loaded_rgb565 is not None
        self._send_btn.setEnabled(enabled and has_image)
        self._flash_upload_btn.setEnabled(enabled and has_image)
        self._browse_btn.setEnabled(enabled)
        self._flash_show_btn.setEnabled(enabled)
        self._flash_erase_btn.setEnabled(enabled)
        self._flash_info_btn.setEnabled(enabled)
        self._flash_diag_btn.setEnabled(enabled)
        self._ind_send_btn.setEnabled(enabled)
        self._ind_clear_btn.setEnabled(enabled)
