"""
From a stick to a motor or to a base: the mapping layer the GUI and the CLI share.

Single motor: the left stick's vertical axis is a signed speed in RPM: a dead zone around
centre, an expo curve for fine control near zero, a maximum, a slew limit so a flick does not
demand an instant 250 RPM, and the resonance bands of this motor (55-66 and 105-122 RPM
on the 23HS22-2804 at 12.5 V) stepped over: a speed inside a band snaps to its nearer edge,
so the motor never dwells where it shakes. The loop sends MOT:RUN <rpm> RPM each tick
(coalesced on the board: only the newest is sent) and MOT:STOP when the stick is released
or the pad goes away.

Base (fleet): left stick = vx (up) and vy (left), right stick x = wz; the fleet scales the
four wheels together, so bands are not avoided there (four wheels cannot all dodge at once).

Buttons: B = stop, Y = enable, START = E-STOP, X = re-arm (fleet) / clear fault (motor).
"""

from dataclasses import dataclass, field
from typing import Callable, List, Optional, Tuple

from . import commands as C
from . import kinematics as K
from .gamepad import GamepadState

RESONANCE_BANDS_RPM: List[Tuple[float, float]] = [(55.0, 66.0), (105.0, 122.0)]


@dataclass
class StickMap:
    dead_zone: float = 0.08          # stick fraction ignored around centre
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

    def __init__(self, board, stick: Optional[StickMap] = None, period_s: float = 0.05):
        self.board = board
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
        if "B" in pressed:
            self._stop()
        if "Y" in pressed:
            self.board.submit(C.mot_enable())
        if "X" in pressed:
            self.board.submit(C.fault_clear())


@dataclass
class BaseMap:
    max_vx: float = 0.5              # m/s at full stick
    max_vy: float = 0.5
    max_wz: float = 2.0              # rad/s at full stick
    stick: StickMap = field(default_factory=lambda: StickMap(avoid_bands=False))

    def twist(self, state: GamepadState) -> K.Twist:
        return K.Twist(vx=self.stick.shape(state.ly) * self.max_vx,
                       vy=-self.stick.shape(state.lx) * self.max_vy,      # stick right = move right = -vy (left positive)
                       wz=-self.stick.shape(state.rx) * self.max_wz)      # stick right = turn clockwise = -wz


class BaseDriver:
    """Turns pad states into fleet twists; buttons act on the whole base."""

    def __init__(self, fleet, base: Optional[BaseMap] = None):
        self.fleet = fleet
        self.base = base or BaseMap()
        self.last: Optional[K.Twist] = None
        self._buttons: frozenset = frozenset()
        self._was_connected = False
        self.on_action: Optional[Callable[[str], None]] = None      # "estop" / "rearm" / "enable" to run off-thread

    def tick(self, state: Optional[GamepadState]) -> Optional[K.Twist]:
        if state is None:
            if self._was_connected:
                self.fleet.stop()
                self.last = K.Twist()
            self._was_connected = False
            return self.last
        self._was_connected = True
        pressed = state.buttons - self._buttons
        self._buttons = state.buttons
        if "START" in pressed and self.on_action: self.on_action("estop")
        if "X" in pressed and self.on_action: self.on_action("rearm")
        if "Y" in pressed and self.on_action: self.on_action("enable")
        if "B" in pressed:
            self.fleet.stop()
            self.last = K.Twist()
            return self.last
        t = self.base.twist(state)
        if t == K.Twist():
            if self.last is None or self.last != t:
                self.fleet.stop()
        else:
            self.fleet.drive(t)
        self.last = t
        return t
