"""Gamepads without hardware: SDL's raw values to a GamepadState, Sony pads told apart, the SDL
pad's hot-plug (a Bluetooth pad that drops and comes back) against a stand-in for pygame, and the
GUI's pad choosing XInput before SDL."""

import sys
import types

import pytest

from smc_core.gamepad import (AutoGamepad, GamepadState, IGamepad, SdlGamepad, SoftwareGamepad,
                              is_playstation, sdl_state)


def test_sdl_values_become_a_gamepad_state_with_up_positive():
    s = sdl_state([32767, -32768, -16384, 16384, 32767, 0], ["A", "START"], 7)
    assert s.lx == pytest.approx(1.0) and s.ly == pytest.approx(1.0)        # SDL's y grows downward
    assert s.rx == pytest.approx(-0.5, abs=1e-3) and s.ry == pytest.approx(-0.5, abs=1e-3)
    assert s.lt == pytest.approx(1.0) and s.rt == 0.0
    assert s.buttons == frozenset({"A", "START"}) and s.packet == 7
    centred = sdl_state([0] * 6, [], 1)
    assert str(centred.ly) == "0.0" and str(centred.ry) == "0.0"           # not -0.0


@pytest.mark.parametrize("name, sony", [("PS4 Controller", True), ("PS5 Controller", True),
                                        ("Wireless Controller", True), ("DualSense Wireless Controller", True),
                                        ("Xbox Series X Controller", False), ("Nintendo Switch Pro Controller", False)])
def test_sony_pads_are_told_apart(name, sony):
    assert is_playstation(name) is sony


class FakePad:
    def __init__(self, name="PS4 Controller"):
        self.name = name
        self.axes = [0] * 6
        self.buttons = set()
        self.alive = True
        self.led = None

    def attached(self):
        return self.alive

    def get_axis(self, a):
        return self.axes[a]

    def get_button(self, b):
        if b == 20:
            raise ValueError("no touchpad on this SDL")            # a button the build lacks: skipped
        return b in self.buttons

    def set_led(self, colour):
        self.led = colour

    def as_joystick(self):
        return types.SimpleNamespace(get_power_level=lambda: "medium")


@pytest.fixture
def fake_sdl(monkeypatch):
    """pygame and pygame._sdl2.controller with a list of pads standing in for the devices."""
    pads = []
    pg = types.ModuleType("pygame")
    pg.error = RuntimeError
    pg.display = types.SimpleNamespace(init=lambda: None)
    pg.joystick = types.SimpleNamespace(init=lambda: None, get_count=lambda: len(pads))
    pg.event = types.SimpleNamespace(get=lambda: [])
    ctl = types.ModuleType("pygame._sdl2.controller")
    ctl.init = lambda: None
    ctl.is_controller = lambda i: True
    ctl.Controller = lambda i: pads[i]
    sdl2 = types.ModuleType("pygame._sdl2")
    sdl2.controller = ctl
    monkeypatch.setitem(sys.modules, "pygame", pg)
    monkeypatch.setitem(sys.modules, "pygame._sdl2", sdl2)
    monkeypatch.setitem(sys.modules, "pygame._sdl2.controller", ctl)
    return pads


def test_a_bluetooth_pad_is_found_read_lost_and_found_again(fake_sdl):
    now = [0.0]
    pad = SdlGamepad(clock=lambda: now[0])
    assert pad.available and pad.poll() is None                     # nothing paired yet
    ps4 = FakePad()
    fake_sdl.append(ps4)
    assert pad.poll() is None                                        # the next look is RESCAN_S on
    now[0] += SdlGamepad.RESCAN_S
    ps4.axes = [16384, -32768, 0, 0, 0, 32767]
    ps4.buttons = {1, 6}                                             # Circle, Options
    s = pad.poll()
    assert s is not None and pad.playstation and pad.battery == "medium"
    assert ps4.led == SdlGamepad.LIGHT_BAR                           # the light bar says it is ours
    assert s.ly == pytest.approx(1.0) and s.rt == pytest.approx(1.0) and s.buttons == {"B", "START"}
    assert pad.poll().packet == s.packet                             # nothing changed: same packet
    ps4.axes[0] = 0
    assert pad.poll().packet == s.packet + 1
    ps4.alive = False                                                # out of range, or asleep
    assert pad.poll() is None and not pad.connected
    ps4.alive = True
    now[0] += SdlGamepad.RESCAN_S
    assert pad.poll() is not None                                    # back


def test_without_pygame_the_sdl_pad_says_why(monkeypatch):
    monkeypatch.setitem(sys.modules, "pygame", None)                 # import fails
    pad = SdlGamepad()
    assert not pad.available and "pygame-ce" in pad.error and pad.poll() is None


class StubPad(IGamepad):
    def __init__(self, name, state=None):
        self.name = name
        self.state = state

    def poll(self):
        return self.state


def test_the_gui_pad_takes_xinput_first_then_sdl(fake_sdl):
    xbox = StubPad("xinput")
    sdl = SdlGamepad(clock=lambda: 100.0)
    ps4 = FakePad()
    fake_sdl.append(ps4)
    auto = AutoGamepad([xbox, sdl])
    assert auto.poll() is not None and auto.active is sdl and auto.playstation
    assert auto.name == "PS4 Controller (battery medium)"
    xbox.state = GamepadState(ly=0.5)
    assert auto.poll().ly == 0.5 and auto.active is xbox and not auto.playstation
    xbox.state = None
    ps4.alive = False
    assert auto.poll() is None and auto.active is None and auto.name == "none"
    assert AutoGamepad([SoftwareGamepad()]).error                    # no SDL: says PS4 pads need pygame-ce
