"""
Mecanum kinematics for a four-wheel base (docs/MULTI_BOARD.md section 5).

Plain functions over small dataclasses: no numpy, no I/O, so the module ports to Rust
line for line (each function is a pure map from numbers to numbers).

Conventions
- Body frame: x forward, y to the left, z up; wz > 0 turns counter-clockwise seen from above.
- Wheels FL, FR, RL, RR; rollers in the usual "X" pattern seen from above (the rollers
  touching the ground form an X), so a positive vy (strafe left) turns FL and RR backwards.
- Wheel speed: rad/s, positive = the wheel rolls the base forward.
- lx: half the wheelbase (front-to-rear axle distance / 2); ly: half the track width.

    w_FL = (vx - vy - k*wz) / r        k = lx + ly
    w_FR = (vx + vy + k*wz) / r
    w_RL = (vx + vy - k*wz) / r
    w_RR = (vx - vy + k*wz) / r

Motor side: wheel rad/s -> motor full steps/s through the gear ratio and the motor's full
steps per revolution; `inverted` per wheel accounts for the mirrored mounting of the
right-hand motors (a forward roll needs the opposite shaft direction).
"""

import math
from dataclasses import dataclass, field
from typing import Dict, Tuple

ROLES = ("FL", "FR", "RL", "RR")


@dataclass(frozen=True)
class Twist:
    vx: float = 0.0       # m/s, forward
    vy: float = 0.0       # m/s, left
    wz: float = 0.0       # rad/s, counter-clockwise


def indicator(twist: Twist, full: Twist) -> Tuple[int, int, bool, int, int]:
    """The LCD motion indicator (UI:DISP:INDICATOR) for a body twist: (angle, rot_dir,
    has_translation, speed_pct, rot_pct). The arrow points where the base moves, in degrees
    clockwise from straight ahead (forward 0, right 90), its length the linear speed as a share
    of `full` (vx and vy scaled on their own full scale); the ring turns -1 counter-clockwise,
    +1 clockwise, lit by |wz| as a share of full.wz."""
    nx = twist.vx / full.vx if full.vx else 0.0
    ny = twist.vy / full.vy if full.vy else 0.0
    speed = min(100, int(round(100.0 * math.hypot(nx, ny))))
    turn = min(100, int(round(100.0 * abs(twist.wz) / full.wz))) if full.wz else 0
    angle = int(round(math.degrees(math.atan2(-twist.vy, twist.vx)))) % 360 if speed else 0
    rot = 0 if turn == 0 else (-1 if twist.wz > 0 else 1)
    return angle, rot, speed > 0, speed, turn


@dataclass(frozen=True)
class Geometry:
    wheel_radius_m: float = 0.048           # 97 mm mecanum wheel
    half_wheelbase_m: float = 0.15          # lx
    half_track_m: float = 0.17              # ly
    gear_ratio: float = 1.0                 # motor revolutions per wheel revolution
    full_steps_per_rev: int = 200
    max_motor_sps: float = 1000.0           # full steps/s, keep under MAX_SPEED
    inverted: Dict[str, bool] = field(default_factory=lambda: {"FL": False, "FR": True,
                                                               "RL": False, "RR": True})

    @property
    def k(self) -> float:
        return self.half_wheelbase_m + self.half_track_m


def inverse(t: Twist, g: Geometry) -> Dict[str, float]:
    """Body twist -> wheel angular speeds (rad/s)."""
    r, k = g.wheel_radius_m, g.k
    return {
        "FL": (t.vx - t.vy - k * t.wz) / r,
        "FR": (t.vx + t.vy + k * t.wz) / r,
        "RL": (t.vx + t.vy - k * t.wz) / r,
        "RR": (t.vx - t.vy + k * t.wz) / r,
    }


def forward(w: Dict[str, float], g: Geometry) -> Twist:
    """Wheel angular speeds (rad/s) -> body twist (least-squares; exact for a rigid base)."""
    r, k = g.wheel_radius_m, g.k
    fl, fr, rl, rr = w["FL"], w["FR"], w["RL"], w["RR"]
    return Twist(vx=r / 4.0 * (fl + fr + rl + rr),
                 vy=r / 4.0 * (-fl + fr + rl - rr),
                 wz=r / (4.0 * k) * (-fl + fr - rl + rr))


def wheel_to_motor_sps(w_rad_s: float, role: str, g: Geometry) -> float:
    """Wheel rad/s -> signed motor full steps/s (sign = shaft direction, inversion applied)."""
    sps = w_rad_s / (2.0 * math.pi) * g.gear_ratio * g.full_steps_per_rev
    return -sps if g.inverted.get(role, False) else sps


def motor_sps_to_wheel(sps: float, role: str, g: Geometry) -> float:
    if g.inverted.get(role, False):
        sps = -sps
    return sps / (g.full_steps_per_rev * g.gear_ratio) * 2.0 * math.pi


def motor_commands(t: Twist, g: Geometry) -> Tuple[Dict[str, Tuple[int, int]], float]:
    """Twist -> per-role (full steps/s >= 0, direction 0/1) for MOT:RUN, and the scale
    applied. If any wheel would exceed max_motor_sps, every wheel is scaled by the same
    factor, so the base keeps its direction of travel and its turn ratio."""
    sps = {role: wheel_to_motor_sps(w, role, g) for role, w in inverse(t, g).items()}
    peak = max(abs(v) for v in sps.values())
    scale = 1.0 if peak <= g.max_motor_sps or peak == 0 else g.max_motor_sps / peak
    out = {}
    for role in ROLES:
        v = sps[role] * scale
        out[role] = (int(round(abs(v))), 1 if v >= 0 else 0)
    return out, scale


def twist_from_motor_sps(sps: Dict[str, float], g: Geometry) -> Twist:
    """Signed motor full steps/s per role (from telemetry) -> body twist."""
    return forward({role: motor_sps_to_wheel(v, role, g) for role, v in sps.items()}, g)
