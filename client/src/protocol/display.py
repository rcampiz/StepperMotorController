"""
Display protocol helpers for remote LCD control.

Provides methods for sending display commands including binary bitmap streaming.
"""

import base64
import struct
from typing import Optional, Tuple, Union
from dataclasses import dataclass

from .client import MotorClient, Response, ProtocolError
from transport.interface import TransportError


# LCD dimensions
LCD_WIDTH = 240
LCD_HEIGHT = 320


@dataclass
class RGB565Color:
    """RGB565 color value."""
    value: int

    @classmethod
    def from_rgb(cls, r: int, g: int, b: int) -> "RGB565Color":
        """
        Create RGB565 color from 8-bit RGB components.

        Args:
            r, g, b: 8-bit color components (0-255).
        """
        # RGB565: RRRRRGGGGGGBBBBB
        r5 = (r >> 3) & 0x1F
        g6 = (g >> 2) & 0x3F
        b5 = (b >> 3) & 0x1F
        return cls((r5 << 11) | (g6 << 5) | b5)

    @classmethod
    def from_hex(cls, hex_str: str) -> "RGB565Color":
        """Create from hex string like 'FF0000' or '#FF0000'."""
        hex_str = hex_str.lstrip("#")
        if len(hex_str) == 6:
            r = int(hex_str[0:2], 16)
            g = int(hex_str[2:4], 16)
            b = int(hex_str[4:6], 16)
            return cls.from_rgb(r, g, b)
        elif len(hex_str) == 4:
            # Assume RGB565 directly
            return cls(int(hex_str, 16))
        else:
            raise ValueError(f"Invalid hex color: {hex_str}")

    def to_hex(self) -> str:
        """Return as 4-digit hex string."""
        return f"{self.value:04X}"


# Common colors
BLACK = RGB565Color(0x0000)
WHITE = RGB565Color(0xFFFF)
RED = RGB565Color(0xF800)
GREEN = RGB565Color(0x07E0)
BLUE = RGB565Color(0x001F)
YELLOW = RGB565Color(0xFFE0)
CYAN = RGB565Color(0x07FF)
MAGENTA = RGB565Color(0xF81F)
GRAY = RGB565Color(0x8410)


class DisplayClient:
    """
    Client for remote LCD display control.

    Wraps MotorClient to provide display-specific commands.
    Requires UI mode to be set to REMOTE before use.
    """

    def __init__(self, client: MotorClient):
        """
        Initialize display client.

        Args:
            client: Connected MotorClient instance.
        """
        self._client = client

    def set_remote_mode(self) -> Response:
        """Switch to REMOTE UI mode."""
        return self._client.send_command("UI:MODE REMOTE")

    def set_local_mode(self) -> Response:
        """Switch to LOCAL UI mode."""
        return self._client.send_command("UI:MODE LOCAL")

    def get_mode(self) -> Response:
        """Get current UI mode."""
        return self._client.send_command("UI:MODE?")

    def clear(self, color: Union[RGB565Color, int] = BLACK) -> Response:
        """
        Clear the display with a color.

        Args:
            color: RGB565 color value or RGB565Color object.
        """
        if isinstance(color, RGB565Color):
            color = color.value
        return self._client.send_command(f"UI:DISP:CLEAR {color:04X}")

    def draw_text(
        self,
        x: int,
        y: int,
        text: str,
        fg: Union[RGB565Color, int] = WHITE,
        bg: Union[RGB565Color, int] = BLACK,
    ) -> Response:
        """
        Draw text at position.

        Args:
            x, y: Top-left position.
            text: Text string to draw.
            fg: Foreground color.
            bg: Background color.
        """
        if isinstance(fg, RGB565Color):
            fg = fg.value
        if isinstance(bg, RGB565Color):
            bg = bg.value
        return self._client.send_command(f"UI:DISP:TEXT {x} {y} {fg:04X} {bg:04X} {text}")

    def draw_rect(
        self,
        x: int,
        y: int,
        w: int,
        h: int,
        color: Union[RGB565Color, int] = WHITE,
        filled: bool = False,
    ) -> Response:
        """
        Draw a rectangle.

        Args:
            x, y: Top-left position.
            w, h: Width and height.
            color: RGB565 color.
            filled: True for filled rectangle.
        """
        if isinstance(color, RGB565Color):
            color = color.value
        cmd = f"UI:DISP:RECT {x} {y} {w} {h} {color:04X}"
        if filled:
            cmd += " fill"
        return self._client.send_command(cmd)

    def draw_line(
        self,
        x0: int,
        y0: int,
        x1: int,
        y1: int,
        color: Union[RGB565Color, int] = WHITE,
    ) -> Response:
        """
        Draw a line.

        Args:
            x0, y0: Start point.
            x1, y1: End point.
            color: RGB565 color.
        """
        if isinstance(color, RGB565Color):
            color = color.value
        return self._client.send_command(f"UI:DISP:LINE {x0} {y0} {x1} {y1} {color:04X}")

    def draw_bitmap_b64(
        self,
        x: int,
        y: int,
        w: int,
        h: int,
        data: bytes,
    ) -> Response:
        """
        Draw a bitmap using base64 encoding.

        Limited to small bitmaps (max ~340 bytes decoded due to command buffer).

        Args:
            x, y: Top-left position.
            w, h: Bitmap dimensions.
            data: Raw RGB565 pixel data (big-endian, 2 bytes per pixel).
        """
        b64_data = base64.b64encode(data).decode("ascii")
        return self._client.send_command(f"UI:DISP:BITMAP:B64 {x} {y} {w} {h} {b64_data}")

    def draw_bitmap(
        self,
        x: int,
        y: int,
        w: int,
        h: int,
        data: bytes,
        timeout_ms: int = 10000,
    ) -> Response:
        """
        Draw a bitmap using binary streaming.

        Supports full-screen images (240x240 = 115200 bytes).
        Delegates to MotorClient.disp_bitmap() for protocol handling.

        Args:
            x, y: Top-left position.
            w, h: Bitmap dimensions.
            data: Raw RGB565 pixel data (big-endian, 2 bytes per pixel).
            timeout_ms: Timeout for the entire transfer.

        Returns:
            Response from device.

        Raises:
            ProtocolError: If protocol handshake fails.
            TransportError: If communication fails.
        """
        expected_size = w * h * 2
        if len(data) < expected_size:
            raise ValueError(f"Data too small: {len(data)} < {expected_size}")

        # Delegate to MotorClient's implementation which handles chunking
        return self._client.disp_bitmap(x, y, w, h, data[:expected_size], timeout_ms)

    def draw_image(
        self,
        x: int,
        y: int,
        image,
        use_binary: bool = True,
    ) -> Response:
        """
        Draw a PIL Image to the display.

        Requires Pillow (PIL) to be installed.

        Args:
            x, y: Top-left position.
            image: PIL Image object (will be converted to RGB565).
            use_binary: True for binary streaming (faster), False for base64.

        Returns:
            Response from device.
        """
        # Convert PIL image to RGB565 bytes
        data = self._image_to_rgb565(image)
        w, h = image.size

        if use_binary:
            return self.draw_bitmap(x, y, w, h, data)
        else:
            return self.draw_bitmap_b64(x, y, w, h, data)

    @staticmethod
    def _image_to_rgb565(image) -> bytes:
        """
        Convert PIL Image to RGB565 byte data.

        Args:
            image: PIL Image object.

        Returns:
            RGB565 pixel data as bytes (big-endian).
        """
        # Ensure RGB mode
        if image.mode != "RGB":
            image = image.convert("RGB")

        w, h = image.size
        pixels = image.load()
        data = bytearray(w * h * 2)

        idx = 0
        for y in range(h):
            for x in range(w):
                r, g, b = pixels[x, y]
                # RGB565: RRRRRGGGGGGBBBBB (big-endian)
                r5 = (r >> 3) & 0x1F
                g6 = (g >> 2) & 0x3F
                b5 = (b >> 3) & 0x1F
                rgb565 = (r5 << 11) | (g6 << 5) | b5
                # Big-endian (high byte first)
                data[idx] = (rgb565 >> 8) & 0xFF
                data[idx + 1] = rgb565 & 0xFF
                idx += 2

        return bytes(data)

    @staticmethod
    def create_solid_bitmap(w: int, h: int, color: Union[RGB565Color, int]) -> bytes:
        """
        Create a solid color bitmap.

        Args:
            w, h: Dimensions.
            color: RGB565 color.

        Returns:
            RGB565 pixel data as bytes.
        """
        if isinstance(color, RGB565Color):
            color = color.value

        # Big-endian RGB565
        hi = (color >> 8) & 0xFF
        lo = color & 0xFF
        pixel = bytes([hi, lo])
        return pixel * (w * h)

    @staticmethod
    def create_gradient_bitmap(
        w: int,
        h: int,
        color1: Union[RGB565Color, int],
        color2: Union[RGB565Color, int],
        horizontal: bool = True,
    ) -> bytes:
        """
        Create a gradient bitmap.

        Args:
            w, h: Dimensions.
            color1, color2: Start and end colors.
            horizontal: True for horizontal gradient, False for vertical.

        Returns:
            RGB565 pixel data as bytes.
        """
        if isinstance(color1, RGB565Color):
            color1 = color1.value
        if isinstance(color2, RGB565Color):
            color2 = color2.value

        # Extract RGB components
        r1 = (color1 >> 11) & 0x1F
        g1 = (color1 >> 5) & 0x3F
        b1 = color1 & 0x1F

        r2 = (color2 >> 11) & 0x1F
        g2 = (color2 >> 5) & 0x3F
        b2 = color2 & 0x1F

        data = bytearray(w * h * 2)
        idx = 0

        steps = w if horizontal else h

        for y in range(h):
            for x in range(w):
                t = x / (steps - 1) if horizontal else y / (steps - 1)
                r = int(r1 + (r2 - r1) * t)
                g = int(g1 + (g2 - g1) * t)
                b = int(b1 + (b2 - b1) * t)
                rgb565 = (r << 11) | (g << 5) | b
                data[idx] = (rgb565 >> 8) & 0xFF
                data[idx + 1] = rgb565 & 0xFF
                idx += 2

        return bytes(data)
