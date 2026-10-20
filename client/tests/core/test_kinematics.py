"""Mecanum kinematics: inverse/forward round trips, the canonical motions, scaling."""

import math

import pytest

from smc_core.kinematics import (Geometry, Twist, forward, inverse, motor_commands,
                                 motor_sps_to_wheel, twist_from_motor_sps, wheel_to_motor_sps)

G = Geometry()


def close(a: Twist, b: Twist, tol=1e-9):
    return abs(a.vx - b.vx) < tol and abs(a.vy - b.vy) < tol and abs(a.wz - b.wz) < tol


@pytest.mark.parametrize("t", [Twist(0.3, 0, 0), Twist(0, 0.2, 0), Twist(0, 0, 1.0),
                               Twist(0.25, -0.1, 0.4), Twist(-0.4, 0.3, -0.7)])
def test_forward_inverts_inverse(t):
    assert close(forward(inverse(t, G), G), t)


def test_canonical_motions():
    w = inverse(Twist(vx=0.3), G)                         # straight ahead: all wheels equal
    assert len({round(v, 12) for v in w.values()}) == 1 and w["FL"] > 0
    w = inverse(Twist(vy=0.2), G)                         # strafe left: FL, RR backwards
    assert w["FL"] < 0 and w["RR"] < 0 and w["FR"] > 0 and w["RL"] > 0
    w = inverse(Twist(wz=1.0), G)                         # turn CCW: left side backwards
    assert w["FL"] < 0 and w["RL"] < 0 and w["FR"] > 0 and w["RR"] > 0
    assert math.isclose(w["FR"], G.k / G.wheel_radius_m)


def test_motor_conversion_round_trip_and_inversion():
    for role in ("FL", "FR"):
        sps = wheel_to_motor_sps(2.0, role, G)
        assert math.isclose(motor_sps_to_wheel(sps, role, G), 2.0)
    assert wheel_to_motor_sps(1.0, "FL", G) > 0 > wheel_to_motor_sps(1.0, "FR", G)
    assert math.isclose(wheel_to_motor_sps(2 * math.pi, "FL", G), 200.0)   # 1 rev/s


def test_motor_commands_scale_uniformly_and_keep_direction():
    fast = Twist(vx=5.0, vy=1.0, wz=2.0)
    cmds, scale = motor_commands(fast, G)
    assert scale < 1.0
    assert max(s for s, _ in cmds.values()) == pytest.approx(G.max_motor_sps, abs=1)
    sps = {r: (s if d else -s) for r, (s, d) in cmds.items()}
    t = twist_from_motor_sps(sps, G)
    # same direction of travel and turn ratio, just slower
    assert math.isclose(t.vy / t.vx, fast.vy / fast.vx, rel_tol=2e-2)
    assert math.isclose(t.wz / t.vx, fast.wz / fast.vx, rel_tol=2e-2)


def test_zero_twist_is_all_stopped():
    cmds, scale = motor_commands(Twist(), G)
    assert scale == 1.0 and all(s == 0 for s, _ in cmds.values())


def test_the_lcd_indicator_points_where_the_base_moves_and_rings_for_the_turn():
    from smc_core import kinematics as K
    full = K.Twist(vx=0.5, vy=0.5, wz=2.0)
    assert K.indicator(K.Twist(), full) == (0, 0, False, 0, 0)              # still: no arrow, no ring
    assert K.indicator(K.Twist(vx=0.5), full) == (0, 0, True, 100, 0)       # full ahead
    assert K.indicator(K.Twist(vx=-0.25), full) == (180, 0, True, 50, 0)    # half astern
    assert K.indicator(K.Twist(vy=-0.5), full)[:2] == (90, 0)               # right (vy is left-positive)
    assert K.indicator(K.Twist(vy=0.5), full)[0] == 270                      # left
    assert K.indicator(K.Twist(vx=0.5, vy=-0.5), full)[0] == 45             # ahead and right
    assert K.indicator(K.Twist(wz=1.0), full) == (0, -1, False, 0, 50)      # counter-clockwise at half
    assert K.indicator(K.Twist(wz=-3.0), full) == (0, 1, False, 0, 100)     # clockwise, capped at 100 %
