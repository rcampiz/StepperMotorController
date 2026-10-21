"""
From a stick to a motor or to a base: the mapping layer the GUI and the CLI share.

Single motor: the left stick's vertical axis is a signed speed in RPM: a dead zone around
centre, an expo curve for fine control near zero, a maximum, a slew limit so a flick does not
demand an instant 250 RPM, and the resonance bands of this motor (55-66 and 105-122 RPM
on the 23HS22-2804 at 12.5 V) stepped over: a speed inside a band snaps to its nearer edge,
so the motor never dwells where it shakes. The loop sends MOT:RUN <rpm> RPM each tick
(coalesced on the board: only the newest is sent) and MOT:STOP when the stick is released
or the pad goes away.

Base (fleet): the left stick moves the base (vx up, vy left) through a round dead zone with
hysteresis, the triggers turn it (LT counter-clockwise, RT clockwise), and the twist is rate
limited (quick, bounded speed-up and slow-down; B, START or a lost pad stop at once). The fleet
scales the four wheels together, so bands are not avoided there (four wheels cannot all dodge
at once).

Buttons: B = stop, Y = enable, START = E-STOP (held 3 s: clears it, as the LCD's CENTER hold
does), X = re-arm (fleet) / clear fault (motor).
"""

import math
import time
from dataclasses import dataclass, field
from typing import Callable, List, Optional, Tuple

from . import commands as C
from . import kinematics as K
from .gamepad import GamepadState

RESONANCE_BANDS_RPM: List[Tuple[float, float]] = [(55.0, 66.0), (105.0, 122.0)]
CLEAR_HOLD_S = 3.0                   # START held this long clears the E-STOP (owner 2026-10-20)


class StartHold:
    """START held for CLEAR_HOLD_S: true once per hold (the press itself is the E-STOP)."""

    def __init__(self, hold_s: float = CLEAR_HOLD_S):
        self.hold_s = hold_s
        self._down: Optional[float] = None
        self._fired = False

    def update(self, held: bool, now: float) -> bool:
        if not held:
            self._down = None
            return False
        if self._down is None:
            self._down, self._fired = now, False
        if not self._fired and now - self._down >= self.hold_s:
            self._fired = True
            return True
        return False


@dataclass
class StickMap:
    dead_zone: float = 0.20          # stick fraction ignored around centre (the PowerA 1414135-02
                                     # rests up to 0.11 off centre, bench 2026-10-20)
    expo: float = 0.5                # 0 = linear, 1 = cubic-ish; fine control near centre
    max_rpm: float = 250.0
    min_rpm: float = 3.0             # below this the motor is stopped, not crawled
    slew_rpm_per_s: float = 400.0    # how fast the demanded speed may change
    avoid_bands: bool = True
    bands_rpm: List[Tuple[float, float]] = field(default_factory=lambda: list(RESONANCE_BANDS_RPM))

    def shape(self, axis: float) -> float:
        """Stick -1..1 -> -1..1 after the dead zone and the expo curve."""
        a = max(-1.0, min(1.0, axis))
        mag = abs(a)
        if mag < self.dead_zone:
            return 0.0
        mag = (mag - self.dead_zone) / (1.0 - self.dead_zone)
        mag = (1.0 - self.expo) * mag + self.expo * mag ** 3
        return mag if a > 0 else -mag

    def snap_band(self, rpm: float) -> float:
        """A speed inside a resonance band moves to the band's nearer edge."""
        if not self.avoid_bands:
            return rpm
        mag = abs(rpm)
        for lo, hi in self.bands_rpm:
            if lo < mag < hi:
                mag = lo if (mag - lo) <= (hi - mag) else hi
                break
        return mag if rpm >= 0 else -mag

    def target_rpm(self, axis: float) -> float:
        rpm = self.shape(axis) * self.max_rpm
        if abs(rpm) < self.min_rpm:
            return 0.0
        return self.snap_band(rpm)

    def slew(self, current: float, target: float, dt: float) -> float:
        """Move `current` toward `target` by at most slew_rpm_per_s * dt; a stop is immediate."""
        if target == 0.0:
            return 0.0
        step = self.slew_rpm_per_s * max(0.0, dt)
        if target > current + step:
            return current + step
        if target < current - step:
            return current - step
        return target


@dataclass
class MotorCommand:
    rpm: float                       # signed; 0 = stop
    direction: int                   # 1 forward, 0 reverse

    @property
    def stopped(self) -> bool:
        return self.rpm == 0.0


class SingleMotorDriver:
    """Turns pad states into MOT:RUN / MOT:STOP on one board. Call tick() every period."""

    def __init__(self, board, stick: Optional[StickMap] = None, period_s: float = 0.05,
                 clock: Callable[[], float] = time.monotonic):
        self.board = board
        self._clock = clock
        self._start_hold = StartHold()
        self.stick = stick or StickMap()
        self.period_s = period_s
        self.rpm = 0.0                       # what is being demanded now (after slew)
        self.last: Optional[MotorCommand] = None
        self._was_connected = False
        self._buttons: frozenset = frozenset()

    def tick(self, state: Optional[GamepadState], dt: Optional[float] = None) -> Optional[MotorCommand]:
        dt = self.period_s if dt is None else dt
        if state is None:
            # Pad gone: stop once, then nothing until it is back
            if self._was_connected or (self.last is not None and not self.last.stopped):
                self._stop()
            self._was_connected = False
            return self.last
        self._was_connected = True
        self._edges(state)
        target = self.stick.target_rpm(state.ly)
        self.rpm = self.stick.slew(self.rpm, target, dt)
        if self.rpm == 0.0:
            if self.last is None or not self.last.stopped:
                self._stop()
            return self.last
        cmd = MotorCommand(self.rpm, 1 if self.rpm > 0 else 0)
        if self.last is None or self.last.stopped or abs(cmd.rpm - self.last.rpm) >= 0.05 or cmd.direction != self.last.direction:
            self.board.set_latest("drive", C.mot_run_rpm(abs(cmd.rpm), cmd.direction))
            self.last = cmd
        return self.last

    def _stop(self) -> None:
        self.rpm = 0.0
        self.board.set_latest("drive", C.mot_stop())
        self.last = MotorCommand(0.0, 1)

    def _edges(self, state: GamepadState) -> None:
        pressed = state.buttons - self._buttons
        self._buttons = state.buttons
        if "START" in pressed:
            self.board.estop()
        if self._start_hold.update("START" in state.buttons, self._clock()):
            self.board.submit(C.fault_clear())          # START held 3 s: clear the E-STOP
        if "B" in pressed:
            self._stop()
        if "Y" in pressed:
            self.board.submit(C.mot_enable())
        if "X" in pressed:
            self.board.submit(C.fault_clear())


@dataclass
class StickGate:
    """A round dead zone with hysteresis for a stick (x, y) or a trigger (x alone): the input
    must pass `enter` to start and come back inside `exit` to stop, so a pad that rests off
    centre does not creep and one held at the edge does not chatter. Past the gate the output is
    rescaled from the `exit` edge, so full deflection is still full."""
    enter: float = 0.20
    exit: float = 0.15
    active: bool = False

    def gate(self, x: float, y: float = 0.0) -> Tuple[float, float]:
        r = math.hypot(x, y)
        self.active = r > (self.exit if self.active else self.enter)
        if not self.active or r <= 0.0:
            return 0.0, 0.0
        scale = min(1.0, (r - self.exit) / (1.0 - self.exit)) / r
        return x * scale, y * scale


@dataclass
class TwistSlew:
    """How fast the commanded twist may change: quick enough to feel immediate, bounded so a
    flick or a release does not jerk the base. Slowing down (toward zero) is faster than
    speeding up."""
    accel: float = 1.5               # m/s^2 speeding up (0 -> 0.5 m/s in 0.33 s)
    decel: float = 3.0               # m/s^2 slowing down (0.5 m/s -> 0 in 0.17 s)
    alpha: float = 6.0               # rad/s^2 turning up (0 -> 2 rad/s in 0.33 s)
    alpha_down: float = 12.0         # rad/s^2 turning down

    @staticmethod
    def _axis(cur: float, tgt: float, up: float, down: float, dt: float) -> float:
        rate = up if (abs(tgt) > abs(cur) and tgt * cur >= 0.0) else down
        step = rate * dt
        return tgt if abs(tgt - cur) <= step else cur + math.copysign(step, tgt - cur)

    def step(self, cur: K.Twist, tgt: K.Twist, dt: float) -> K.Twist:
        dt = max(0.0, dt)
        return K.Twist(vx=self._axis(cur.vx, tgt.vx, self.accel, self.decel, dt),
                       vy=self._axis(cur.vy, tgt.vy, self.accel, self.decel, dt),
                       wz=self._axis(cur.wz, tgt.wz, self.alpha, self.alpha_down, dt))


def full_scale_for_rpm(rpm: float, g: K.Geometry) -> K.Twist:
    """The twist that runs the motors at `rpm` on each axis alone: full stick forward or
    sideways, or a full trigger, is that motor speed on every wheel. A diagonal or a move with a
    turn would ask more; the fleet's speed cap (Fleet.set_max_rpm) scales those down, so no
    motor passes `rpm`."""
    w = max(0.0, rpm) / 60.0 * 2.0 * math.pi / g.gear_ratio       # wheel rad/s
    v = w * g.wheel_radius_m
    return K.Twist(vx=v, vy=v, wz=v / g.k)


@dataclass
class BaseMap:
    max_vx: float = 0.5              # m/s at full stick
    max_vy: float = 0.5
    max_wz: float = 2.0              # rad/s at full trigger (or stick)
    stick: StickMap = field(default_factory=lambda: StickMap(avoid_bands=False, dead_zone=0.0))   # expo only
    left: StickGate = field(default_factory=StickGate)                                  # the move stick
    triggers: Tuple[StickGate, StickGate] = field(default_factory=lambda: (StickGate(0.12, 0.08), StickGate(0.12, 0.08)))
    right: StickGate = field(default_factory=StickGate)
    #: What turns the base: "triggers" (owner 2026-10-20: LT counter-clockwise, RT clockwise,
    #: proportional to the pull) or "right_stick" (stick right = clockwise)
    turn: str = "triggers"

    def twist(self, state: GamepadState) -> K.Twist:
        if self.turn == "right_stick":
            rx, _ = self.right.gate(state.rx)
            wz = -self.stick.shape(rx) * self.max_wz                   # stick right = turn clockwise = -wz
        else:
            lt, _ = self.triggers[0].gate(state.lt)
            rt, _ = self.triggers[1].gate(state.rt)
            wz = self.stick.shape(lt - rt) * self.max_wz               # LT = counter-clockwise = +wz
        lx, ly = self.left.gate(state.lx, state.ly)
        # The expo curve acts on how far the stick is pushed, in any direction (it acted on each
        # axis, so a full diagonal came out at ~75 %; owner 2026-10-20): full stick is full speed
        m = math.hypot(lx, ly)
        if m > 0.0:
            k = self.stick.shape(min(1.0, m)) / m
            lx, ly = lx * k, ly * k
        return K.Twist(vx=ly * self.max_vx,
                       vy=-lx * self.max_vy,                            # stick right = move right = -vy (left positive)
                       wz=wz)


class BaseDriver:
    """Turns pad states into fleet twists; buttons act on the whole base."""

    def __init__(self, fleet, base: Optional[BaseMap] = None, slew: Optional[TwistSlew] = TwistSlew(),
                 clock: Callable[[], float] = time.monotonic):
        self.fleet = fleet
        self.base = base or BaseMap()
        self.slew = slew                                # None: the twist follows the pad at once
        self._clock = clock
        self._start_hold = StartHold()
        self._t: Optional[float] = None
        self._cur = K.Twist()
        if hasattr(fleet, "lcd_full_scale"):          # full stick / full trigger = 100 % on the LCDs
            fleet.lcd_full_scale = K.Twist(vx=self.base.max_vx, vy=self.base.max_vy, wz=self.base.max_wz)
        self.last: Optional[K.Twist] = None
        self._buttons: frozenset = frozenset()
        self._was_connected = False
        self.on_action: Optional[Callable[[str], None]] = None      # "estop" / "rearm" / "enable" to run off-thread

    def tick(self, state: Optional[GamepadState], dt: Optional[float] = None) -> Optional[K.Twist]:
        now = self._clock()
        if dt is None:                                  # the real time since the last tick, bounded
            dt = 0.05 if self._t is None else min(0.2, max(0.0, now - self._t))
        self._t = now
        if state is None:
            if self._was_connected:
                self._halt()                            # pad gone: stop at once
            self._was_connected = False
            return self.last
        self._was_connected = True
        pressed = state.buttons - self._buttons
        self._buttons = state.buttons
        if "START" in pressed:
            self._cur = K.Twist()                       # after a re-arm the base starts from rest
            if self.on_action: self.on_action("estop")
        if self._start_hold.update("START" in state.buttons, now) and self.on_action:
            self._cur = K.Twist()
            self.on_action("rearm")                     # START held 3 s: clear the E-STOP
        if "X" in pressed and self.on_action: self.on_action("rearm")
        if "Y" in pressed and self.on_action: self.on_action("enable")
        if "B" in pressed:
            self._halt()                                # B: stop at once, no ramp
            return self.last
        target = self.base.twist(state)
        t = self.slew.step(self._cur, target, dt) if self.slew is not None else target
        self._cur = t
        if t == K.Twist():
            if self.last is None or self.last != t:
                self.fleet.stop()
        else:
            self.fleet.drive(t)
        self.last = t
        return t

    def _halt(self) -> None:
        self._cur = K.Twist()
        self.fleet.stop()
        self.last = K.Twist()

    def set_max_rpm(self, rpm: float, geometry: K.Geometry) -> None:
        """Full stick / full trigger = `rpm` on the motors (full_scale_for_rpm); the fleet caps
        every motor at it; the LCDs show 100 % there; the ramps keep their time to full speed
        (about a third of a second up, a sixth down)."""
        full = full_scale_for_rpm(rpm, geometry)
        self.base.max_vx, self.base.max_vy, self.base.max_wz = full.vx, full.vy, full.wz
        if self.slew is not None:
            self.slew = TwistSlew(accel=3.0 * full.vx, decel=6.0 * full.vx,
                                  alpha=3.0 * full.wz, alpha_down=6.0 * full.wz)
        if hasattr(self.fleet, "lcd_full_scale"):
            self.fleet.lcd_full_scale = full
        if hasattr(self.fleet, "set_max_rpm"):
            self.fleet.set_max_rpm(rpm)
