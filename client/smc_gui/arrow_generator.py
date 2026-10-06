"""
Arrow image generator for LCD display.

Generates 64 rotated arrow images (0 to ~354.4 degrees) as RGB565 data
suitable for streaming to the ST7789 240x320 LCD.
"""

import math
from PIL import Image, ImageDraw

# LCD dimensions
LCD_WIDTH = 240
LCD_HEIGHT = 320

# Number of arrow frames
NUM_FRAMES = 64

# Arrow geometry (relative to center)
# Arrow points upward (0 degrees = up), rotated clockwise
ARROW_LENGTH = 240       # Total length (80% of height)
SHAFT_WIDTH = 30         # Shaft rectangle width
HEAD_WIDTH = 100         # Arrowhead base width
HEAD_LENGTH = 80         # Arrowhead triangle height


def _arrow_polygon():
    """
    Return the 7 vertices of an upward-pointing arrow centered at origin.

    The arrow consists of:
    - A triangular arrowhead at the top
    - A rectangular shaft below it

    Vertices are ordered clockwise starting from the tip.
    """
    half_shaft = SHAFT_WIDTH / 2
    half_head = HEAD_WIDTH / 2
    half_len = ARROW_LENGTH / 2

    tip_y = -half_len                        # Top of arrow (tip)
    head_base_y = tip_y + HEAD_LENGTH        # Bottom of arrowhead
    shaft_bottom_y = half_len                # Bottom of shaft

    return [
        (0, tip_y),                          # Tip
        (half_head, head_base_y),            # Head right
        (half_shaft, head_base_y),           # Shaft top-right
        (half_shaft, shaft_bottom_y),        # Shaft bottom-right
        (-half_shaft, shaft_bottom_y),       # Shaft bottom-left
        (-half_shaft, head_base_y),          # Shaft top-left
        (-half_head, head_base_y),           # Head left
    ]


def _rotate_point(x, y, angle_rad):
    """Rotate point (x, y) around origin by angle_rad."""
    cos_a = math.cos(angle_rad)
    sin_a = math.sin(angle_rad)
    return (x * cos_a - y * sin_a, x * sin_a + y * cos_a)


def generate_arrow_image(index):
    """
    Generate a single arrow image for the given frame index.

    Args:
        index: Frame index (0 to NUM_FRAMES-1).
               0 = pointing up, rotating clockwise.

    Returns:
        PIL Image (RGB, 240x320).
    """
    angle_deg = index * 360.0 / NUM_FRAMES
    angle_rad = math.radians(angle_deg)

    # Center of the image
    cx = LCD_WIDTH / 2
    cy = LCD_HEIGHT / 2

    # Get base arrow polygon and rotate each vertex
    base = _arrow_polygon()
    rotated = []
    for px, py in base:
        rx, ry = _rotate_point(px, py, angle_rad)
        rotated.append((cx + rx, cy + ry))

    # Draw on black background
    img = Image.new("RGB", (LCD_WIDTH, LCD_HEIGHT), (0, 0, 0))
    draw = ImageDraw.Draw(img)
    draw.polygon(rotated, fill=(255, 255, 255), outline=(255, 255, 255))

    return img


def image_to_rgb565(image):
    """
    Convert a PIL Image to RGB565 big-endian byte data.

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


def generate_all_arrows():
    """
    Generate all 64 arrow frames.

    Returns:
        List of (angle_deg, PIL Image, rgb565_bytes) tuples.
    """
    frames = []
    for i in range(NUM_FRAMES):
        angle = i * 360.0 / NUM_FRAMES
        img = generate_arrow_image(i)
        rgb565 = image_to_rgb565(img)
        frames.append((angle, img, rgb565))
    return frames
