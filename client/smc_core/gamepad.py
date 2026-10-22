"""
Gamepads for driving:

- XInputGamepad: an Xbox controller through Windows XInput (no extra package, ctypes on
  xinput1_4.dll).
- SdlGamepad: any pad SDL2 maps, a PS4 DualShock 4 or a PS5 DualSense among them, over USB or
  Bluetooth, on Windows and on Linux (the Raspberry Pi 5), through pygame-ce (its wheels carry
  SDL2). SDL lays every pad out like an Xbox one, so the names below hold: on a PlayStation pad
  Cross = A, Circle = B, Square = X, Triangle = Y, Options = START, Share = BACK, PS = GUIDE,
  L1 / R1 = LB / RB, L2 / R2 the triggers.
- AutoGamepad: XInput first, then SDL; what the GUI uses.
- SoftwareGamepad: the same shape for tests and for an on-screen stick.

No Qt here; the GUI polls `poll()` from its own timer. Axes are -1..1 (stick up and right
positive), triggers 0..1, buttons a frozenset of names.
"""

import ctypes
import os
import sys
import time
from dataclasses import dataclass, field
from typing import Callable, FrozenSet, Iterable, List, Optional, Sequence

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


# SDL_GameControllerButton values (stable across SDL2) -> the names above
SDL_BUTTONS = ((0, "A"), (1, "B"), (2, "X"), (3, "Y"), (4, "BACK"), (5, "GUIDE"), (6, "START"),
               (7, "LTHUMB"), (8, "RTHUMB"), (9, "LB"), (10, "RB"), (11, "DPAD_UP"), (12, "DPAD_DOWN"),
               (13, "DPAD_LEFT"), (14, "DPAD_RIGHT"), (20, "TOUCHPAD"))
#: SDL hints for the PlayStation pads' full Bluetooth reports (battery, light bar, rumble)
BLUETOOTH_FULL_REPORTS = ("SDL_JOYSTICK_HIDAPI_PS4_RUMBLE", "SDL_JOYSTICK_HIDAPI_PS5_RUMBLE")
#: SDL2's battery bands, as a person reads them
BATTERY_TEXT = {"empty": "battery empty, 5 % or less", "low": "battery low, 20 % or less",
                "medium": "battery medium, 70 % or less", "full": "battery full", "max": "battery full",
                "wired": "wired"}
SDL_AXES = 6                     # left x, left y, right x, right y, left trigger, right trigger


def sdl_state(axes: Sequence[int], buttons: Iterable[str], packet: int) -> GamepadState:
    """A GamepadState from SDL's raw values: sticks -32768..32767 with y growing downward,
    triggers 0..32767."""
    lx, ly, rx, ry, lt, rt = axes
    return GamepadState(lx=_axis(lx), ly=0.0 - _axis(ly), rx=_axis(rx), ry=0.0 - _axis(ry),
                        lt=max(0.0, min(1.0, lt / 32767.0)), rt=max(0.0, min(1.0, rt / 32767.0)),
                        buttons=frozenset(buttons), packet=packet)


def is_playstation(name: str) -> bool:
    """A Sony pad, by the name SDL gives it ("PS4 Controller", "PS5 Controller", ...)."""
    n = name.lower()
    return any(k in n for k in ("ps4", "ps5", "dualshock", "dualsense", "playstation", "wireless controller"))


class SdlGamepad(IGamepad):
    """The first pad SDL2 maps (PS4 / PS5 / Switch Pro / Xbox ...), USB or Bluetooth, Windows or
    Linux. Pairs and wakes are picked up as they come: a pad that drops (Bluetooth out of range,
    asleep) reads as not connected and is looked for again every RESCAN_S. Needs pygame-ce; without
    it `available` is False and `error` says why."""
    name = "sdl"
    RESCAN_S = 1.0
    LIGHT_BAR = (255, 41, 117)               # a DualShock's light bar in the panel's hot pink: ours

    def __init__(self, clock: Callable[[], float] = time.monotonic):
        self._clock = clock
        self._pg = None
        self._ctl = None
        self._pad = None
        self._next_scan = 0.0
        self._last = None
        self._packet = 0
        self.pad_name = ""
        self.error = ""
        try:
            os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")
            # The window with the focus is Qt's, not SDL's: deliver the pad's input regardless
            os.environ.setdefault("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1")
            # A DualShock 4 / DualSense over Bluetooth sends a short report (no battery: it read
            # "empty", bench 2026-10-22) until the host asks for the full one; these hints ask
            for hint in BLUETOOTH_FULL_REPORTS:
                os.environ.setdefault(hint, "1")
            import pygame
            from pygame._sdl2 import controller
        except ImportError:
            self.error = "pygame-ce is not installed (pip install pygame-ce): no PS4 / Bluetooth pads"
            return
        try:
            try:
                pygame.display.init()        # SDL pumps its events through the video subsystem (no window opens)
            except pygame.error:
                os.environ["SDL_VIDEODRIVER"] = "dummy"      # a headless Pi: no display at all
                pygame.display.init()
            pygame.joystick.init()
            controller.init()
        except Exception as e:                # noqa: BLE001 (SDL can fail in many ways; the pad is optional)
            self.error = "SDL could not start: %s" % e
            return
        self._pg, self._ctl = pygame, controller

    @property
    def available(self) -> bool:
        return self._pg is not None

    @property
    def connected(self) -> bool:
        return self._pad is not None

    @property
    def playstation(self) -> bool:
        return is_playstation(self.pad_name)

    @property
    def battery(self) -> str:
        """"empty" / "low" / "medium" / "full" / "max" on battery, "wired" on a cable, "" unknown."""
        try:
            level = self._pad.as_joystick().get_power_level() if self._pad is not None else ""
        except Exception:                     # noqa: BLE001
            return ""
        return "" if level == "unknown" else level

    def _open(self) -> None:
        now = self._clock()
        if now < self._next_scan:
            return
        self._next_scan = now + self.RESCAN_S
        for i in range(self._pg.joystick.get_count()):
            if not self._ctl.is_controller(i):
                continue
            try:
                pad = self._ctl.Controller(i)
            except Exception:                 # noqa: BLE001 (gone again between the count and the open)
                continue
            self._pad = pad
            self.pad_name = pad.name or "pad"
            try:
                pad.set_led(self.LIGHT_BAR)
            except Exception:                 # noqa: BLE001 (no light bar, or no access to it on Linux)
                pass
            return

    def poll(self) -> Optional[GamepadState]:
        if self._pg is None:
            return None
        try:
            self._pg.event.get()             # run SDL's event loop (hot-plug, input); nothing here wants the events
            if self._pad is not None and not self._pad.attached():
                self._pad = None
                self.pad_name = ""
            if self._pad is None:
                self._open()
                if self._pad is None:
                    return None
            pad = self._pad
            axes = [pad.get_axis(a) for a in range(SDL_AXES)]
            buttons: List[str] = []
            for b, n in SDL_BUTTONS:
                try:
                    if pad.get_button(b):
                        buttons.append(n)
                except Exception:             # noqa: BLE001 (a button this pad or SDL build does not have)
                    pass
        except Exception:                     # noqa: BLE001 (the pad went mid-read)
            self._pad = None
            self.pad_name = ""
            return None
        key = (tuple(axes), frozenset(buttons))
        if key != self._last:
            self._packet += 1
            self._last = key
        return sdl_state(axes, buttons, self._packet)


class AutoGamepad(IGamepad):
    """The pad the GUI drives with: XInput first (an Xbox pad on Windows), then SDL (a PS4 / PS5
    pad over USB or Bluetooth, or any pad SDL maps; the only one on Linux and the Pi). `name`
    says which is answering, `playstation` whether its buttons wear Sony's symbols."""

    def __init__(self, pads: Optional[Sequence[IGamepad]] = None):
        if pads is None:
            pads = [p for p in (XInputGamepad(), SdlGamepad()) if getattr(p, "available", False)]
        self.pads = list(pads)
        self.active: Optional[IGamepad] = None
        # Why a PS4 pad would not be seen (SDL missing), for the GUI to say
        self.error = "" if any(isinstance(p, SdlGamepad) for p in self.pads) else             "PS4 / Bluetooth pads need pygame-ce (pip install pygame-ce)"

    @property
    def available(self) -> bool:
        return bool(self.pads)

    @property
    def name(self) -> str:
        p = self.active
        if p is None:
            return "none"
        if isinstance(p, SdlGamepad):
            b = p.battery
            return p.pad_name + (" (%s)" % BATTERY_TEXT.get(b, "battery " + b) if b else "")
        return "Xbox (XInput)" if isinstance(p, XInputGamepad) else p.name

    @property
    def playstation(self) -> bool:
        return isinstance(self.active, SdlGamepad) and self.active.playstation

    def poll(self) -> Optional[GamepadState]:
        for p in self.pads:
            state = p.poll()
            if state is not None:
                self.active = p
                return state
        self.active = None
        return None


def default_gamepad() -> IGamepad:
    """XInput and SDL where they exist (AutoGamepad), otherwise a software pad (nothing to
    drive with)."""
    pad = AutoGamepad()
    return pad if pad.available else SoftwareGamepad()
