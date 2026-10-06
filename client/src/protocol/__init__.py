"""Protocol layer for command/response handling (legacy; parsing is in smc_core)."""
import os as _os
import sys as _sys
# Migration shim: smc_core lives in client/, one level above src/.
_CLIENT = _os.path.dirname(_os.path.dirname(_os.path.dirname(_os.path.abspath(__file__))))
if _CLIENT not in _sys.path:
    _sys.path.insert(0, _CLIENT)

from .client import MotorClient, Response, ResponseFormat
from .display import (
    DisplayClient,
    RGB565Color,
    BLACK,
    WHITE,
    RED,
    GREEN,
    BLUE,
    YELLOW,
    CYAN,
    MAGENTA,
    GRAY,
    LCD_WIDTH,
    LCD_HEIGHT,
)

__all__ = [
    "MotorClient",
    "Response",
    "ResponseFormat",
    "DisplayClient",
    "RGB565Color",
    "BLACK",
    "WHITE",
    "RED",
    "GREEN",
    "BLUE",
    "YELLOW",
    "CYAN",
    "MAGENTA",
    "GRAY",
    "LCD_WIDTH",
    "LCD_HEIGHT",
]
