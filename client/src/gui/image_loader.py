"""
Image loader and processor for LCD display upload.

Loads JPEG, PNG, BMP, TIFF images via PIL and SVG via PySide6 QSvgRenderer.
Resizes to fit 240x320 LCD with aspect-preserving letterboxing.
Converts to RGB565 big-endian byte data for the ST7789 display.
"""

from pathlib import Path
from PIL import Image

# LCD dimensions
LCD_WIDTH = 240
LCD_HEIGHT = 320

# Supported raster formats (PIL handles these natively)
_RASTER_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".tiff", ".tif"}
_SVG_EXTS = {".svg", ".svgz"}

SUPPORTED_FILTER = "Images (*.jpg *.jpeg *.png *.bmp *.tiff *.svg *.svgz)"


def load_image(path: str) -> Image.Image:
    """Load an image file, returning a PIL Image in RGB mode.

    Supports JPEG, PNG, BMP, TIFF natively via PIL.
    SVG files are rendered via PySide6 QSvgRenderer.

    Args:
        path: Path to the image file.

    Returns:
        PIL Image in RGB mode.

    Raises:
        ValueError: If the file format is unsupported.
        FileNotFoundError: If the file doesn't exist.
        Exception: On load/render failure.
    """
    p = Path(path)
    if not p.exists():
        raise FileNotFoundError(f"File not found: {path}")

    suffix = p.suffix.lower()

    if suffix in _SVG_EXTS:
        return _load_svg(str(p))
    elif suffix in _RASTER_EXTS:
        img = Image.open(str(p))
        img.load()  # Force read so file handle is released
        if img.mode != "RGB":
            img = img.convert("RGB")
        return img
    else:
        raise ValueError(
            f"Unsupported image format: {suffix}. "
            f"Supported: {', '.join(sorted(_RASTER_EXTS | _SVG_EXTS))}"
        )


def _load_svg(path: str) -> Image.Image:
    """Render SVG to PIL Image via PySide6 QSvgRenderer."""
    try:
        from PySide6.QtSvg import QSvgRenderer
        from PySide6.QtGui import QImage, QPainter
        from PySide6.QtCore import Qt, QSize
    except ImportError:
        raise ImportError(
            "SVG support requires PySide6.QtSvg. "
            "Install with: pip install PySide6"
        )

    renderer = QSvgRenderer(path)
    if not renderer.isValid():
        raise ValueError(f"Failed to parse SVG: {path}")

    # Render at LCD resolution
    qimage = QImage(QSize(LCD_WIDTH, LCD_HEIGHT), QImage.Format_RGB32)
    qimage.fill(Qt.black)

    painter = QPainter(qimage)
    renderer.render(painter)
    painter.end()

    # Convert QImage to PIL Image
    qimage = qimage.convertToFormat(QImage.Format_RGB888)
    width = qimage.width()
    height = qimage.height()
    bytes_per_line = qimage.bytesPerLine()
    raw = qimage.constBits()
    # PySide6 returns memoryview; copy as bytes
    data = bytes(raw)

    # QImage may have padding per scanline; extract only pixel data
    if bytes_per_line == width * 3:
        img = Image.frombytes("RGB", (width, height), data)
    else:
        img = Image.new("RGB", (width, height))
        for y in range(height):
            row_start = y * bytes_per_line
            row_data = data[row_start:row_start + width * 3]
            for x in range(width):
                offset = x * 3
                img.putpixel((x, y), (
                    row_data[offset],
                    row_data[offset + 1],
                    row_data[offset + 2],
                ))

    return img


def resize_for_lcd(image: Image.Image,
                   width: int = LCD_WIDTH,
                   height: int = LCD_HEIGHT) -> Image.Image:
    """Resize image to fit LCD dimensions with aspect-preserving letterboxing.

    The image is scaled to fit within width x height while maintaining
    aspect ratio. Any remaining space is filled with black.

    Args:
        image: Source PIL Image.
        width: Target width (default: 240).
        height: Target height (default: 320).

    Returns:
        Resized PIL Image (RGB, exactly width x height).
    """
    if image.mode != "RGB":
        image = image.convert("RGB")

    src_w, src_h = image.size

    # Calculate scale factor to fit within target
    scale = min(width / src_w, height / src_h)
    new_w = int(src_w * scale)
    new_h = int(src_h * scale)

    # Resize with high-quality resampling
    resized = image.resize((new_w, new_h), Image.LANCZOS)

    # Create black canvas and paste centered
    canvas = Image.new("RGB", (width, height), (0, 0, 0))
    offset_x = (width - new_w) // 2
    offset_y = (height - new_h) // 2
    canvas.paste(resized, (offset_x, offset_y))

    return canvas


def rotate_image(image: Image.Image, degrees: int) -> Image.Image:
    """Rotate an image by 0, 90, 180, or 270 degrees counter-clockwise.

    Args:
        image: Source PIL Image.
        degrees: Rotation angle (must be 0, 90, 180, or 270).

    Returns:
        Rotated PIL Image (RGB).
    """
    if image.mode != "RGB":
        image = image.convert("RGB")
    degrees = degrees % 360
    if degrees == 0:
        return image.copy()
    elif degrees == 90:
        return image.transpose(Image.ROTATE_90)
    elif degrees == 180:
        return image.transpose(Image.ROTATE_180)
    elif degrees == 270:
        return image.transpose(Image.ROTATE_270)
    else:
        return image.rotate(degrees, expand=True, fillcolor=(0, 0, 0))


def crop_for_lcd(image: Image.Image,
                 width: int = LCD_WIDTH,
                 height: int = LCD_HEIGHT) -> Image.Image:
    """Resize and center-crop image to fill LCD dimensions exactly.

    The image is scaled so the *largest* dimension fills the target,
    then the excess is cropped from the center.  No black bars.

    Args:
        image: Source PIL Image.
        width: Target width (default: 240).
        height: Target height (default: 320).

    Returns:
        Cropped PIL Image (RGB, exactly width x height).
    """
    if image.mode != "RGB":
        image = image.convert("RGB")

    src_w, src_h = image.size

    # Scale so the smallest axis fills the target (opposite of resize_for_lcd)
    scale = max(width / src_w, height / src_h)
    new_w = int(src_w * scale)
    new_h = int(src_h * scale)

    resized = image.resize((new_w, new_h), Image.LANCZOS)

    # Center-crop to exact target size
    left = (new_w - width) // 2
    top = (new_h - height) // 2
    return resized.crop((left, top, left + width, top + height))


def image_to_rgb565(image: Image.Image) -> bytes:
    """Convert a PIL Image to RGB565 big-endian byte data.

    Args:
        image: PIL Image (will be converted to RGB if needed).

    Returns:
        bytes: RGB565 pixel data, big-endian, 2 bytes per pixel.
    """
    if image.mode != "RGB":
        image = image.convert("RGB")

    w, h = image.size
    pixels = image.load()
    data = bytearray(w * h * 2)

    idx = 0
    for y in range(h):
        for x in range(w):
            r, g, b = pixels[x, y]
            r5 = (r >> 3) & 0x1F
            g6 = (g >> 2) & 0x3F
            b5 = (b >> 3) & 0x1F
            rgb565 = (r5 << 11) | (g6 << 5) | b5
            data[idx] = (rgb565 >> 8) & 0xFF
            data[idx + 1] = rgb565 & 0xFF
            idx += 2

    return bytes(data)
