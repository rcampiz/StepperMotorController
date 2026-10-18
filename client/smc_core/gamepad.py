"""
Gamepads for driving: a wired Xbox controller through Windows XInput (no extra package,
ctypes on xinput1_4.dll), and a software pad with the same shape for tests and for an
on-screen stick. No Qt here; the GUI polls `poll()` from its own timer.

Axes are -1..1 (stick up and right positive), triggers 0..1, buttons a frozenset of names.
"""

import ctypes
import sys
from dataclasses import dataclass, field
from typing import FrozenSet, Optional

BUTTONS = {
    0x0001: "DPAD_UP", 0x0002: "DPAD_DOWN", 0x0004: "DPAD_LEFT", 0x0008: "DPAD_RIGHT",
    0x0010: "START", 0x0020: "BACK", 0x0040: "LTHUMB", 0x0080: "RTHUMB",
    0x0100: "LB", 0x0200: "RB", 0x1000: "A", 0x2000: "B", 0x4000: "X", 0x8000: "Y",
}


@dataclass(frozen=True)
class GamepadState:
    lx: float = 0.0
    ly: float = 0.0
    rx: float = 0.0
    ry: float = 0.0
    lt: float = 0.0
    rt: float = 0.0
    buttons: FrozenSet[str] = field(default_factory=frozenset)
    packet: int = 0            # changes when anything changes (XInput's packet number)

    def pressed(self, name: str) -> bool:
        return name in self.buttons


class IGamepad:
    """poll() returns the current state, or None while no pad is connected."""
    name = "none"

    def poll(self) -> Optional[GamepadState]:
        return None


class SoftwareGamepad(IGamepad):
    """A pad driven from code: tests, and the GUI's on-screen stick."""
    name = "software"

    def __init__(self):
        self.state: Optional[GamepadState] = GamepadState()
        self._packet = 0

    def set(self, **axes) -> GamepadState:
        buttons = axes.pop("buttons", None)
        base = self.state or GamepadState()
        self._packet += 1
        self.state = GamepadState(
            lx=axes.get("lx", base.lx), ly=axes.get("ly", base.ly),
            rx=axes.get("rx", base.rx), ry=axes.get("ry", base.ry),
            lt=axes.get("lt", base.lt), rt=axes.get("rt", base.rt),
            buttons=frozenset(buttons) if buttons is not None else base.buttons,
            packet=self._packet)
        return self.state

    def disconnect(self) -> None:
        self.state = None

    def poll(self) -> Optional[GamepadState]:
        return self.state


class _XInputGamepadStruct(ctypes.Structure):
    _fields_ = [("wButtons", ctypes.c_ushort), ("bLeftTrigger", ctypes.c_ubyte),
                ("bRightTrigger", ctypes.c_ubyte), ("sThumbLX", ctypes.c_short),
                ("sThumbLY", ctypes.c_short), ("sThumbRX", ctypes.c_short), ("sThumbRY", ctypes.c_short)]


class _XInputState(ctypes.Structure):
    _fields_ = [("dwPacketNumber", ctypes.c_ulong), ("Gamepad", _XInputGamepadStruct)]


class XInputGamepad(IGamepad):
    """A wired Xbox controller (or anything XInput) on Windows. Elsewhere: never connected."""
    name = "xinput"
    ERROR_DEVICE_NOT_CONNECTED = 1167

    def __init__(self, index: int = 0):
        self.index = index
        self._dll = None
        self.connected = False
        if sys.platform == "win32":
            for lib in ("xinput1_4", "xinput1_3", "xinput9_1_0"):
                try:
                    self._dll = ctypes.windll.LoadLibrary(lib)
                    break
                except OSError:
                    continue

    @property
    def available(self) -> bool:
        return self._dll is not None

    def poll(self) -> Optional[GamepadState]:
        if self._dll is None:
            return None
        st = _XInputState()
        if self._dll.XInputGetState(self.index, ctypes.byref(st)) != 0:
            self.connected = False
            return None
        self.connected = True
        g = st.Gamepad
        buttons = frozenset(n for bit, n in BUTTONS.items() if g.wButtons & bit)
        return GamepadState(lx=_axis(g.sThumbLX), ly=_axis(g.sThumbLY), rx=_axis(g.sThumbRX), ry=_axis(g.sThumbRY),
                            lt=g.bLeftTrigger / 255.0, rt=g.bRightTrigger / 255.0,
                            buttons=buttons, packet=int(st.dwPacketNumber))


def _axis(raw: int) -> float:
    return max(-1.0, min(1.0, raw / 32767.0))


def default_gamepad() -> IGamepad:
    """XInput where it exists, otherwise a software pad (nothing to drive with)."""
    pad = XInputGamepad()
    return pad if pad.available else SoftwareGamepad()
