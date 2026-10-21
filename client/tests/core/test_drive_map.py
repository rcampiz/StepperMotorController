"""The stick-to-motor mapping and the drivers on the fake board / a recording fleet."""
import pytest

from smc_core import kinematics as K
from smc_core.board import Board, BoardConfig, Liveness
from smc_core.drive_map import BaseDriver, BaseMap, SingleMotorDriver, StickGate, StickMap, TwistSlew
from smc_core.gamepad import GamepadState, SoftwareGamepad
from smc_core.sim import FakeBoard
from smc_core.transport.memory import MemoryTransport


def test_stick_shape_has_a_dead_zone_and_an_expo_curve():
    m = StickMap(dead_zone=0.1, expo=0.5)
    assert m.shape(0.05) == 0.0 and m.shape(-0.09) == 0.0
    assert m.shape(1.0) == pytest.approx(1.0) and m.shape(-1.0) == pytest.approx(-1.0)
    half = m.shape(0.55)                                     # half way through the live range
    assert 0.0 < half < 0.5                                  # expo bends it below linear
    assert m.shape(-0.55) == pytest.approx(-half)


def test_resonance_bands_are_stepped_over():
    m = StickMap(max_rpm=250)
    assert m.snap_band(60.0) == 55.0 and m.snap_band(63.0) == 66.0     # nearer edge
    assert m.snap_band(-110.0) == -105.0 and m.snap_band(118.0) == 122.0
    assert m.snap_band(80.0) == 80.0 and m.snap_band(55.0) == 55.0     # edges and outside untouched
    m.avoid_bands = False
    assert m.snap_band(60.0) == 60.0


def test_target_and_slew():
    m = StickMap(max_rpm=200, expo=0.0, dead_zone=0.0, slew_rpm_per_s=100.0, min_rpm=3.0)
    assert m.target_rpm(0.005) == 0.0                        # under min_rpm: stop
    assert m.target_rpm(1.0) == 200.0
    assert m.slew(0.0, 200.0, 0.5) == 50.0                   # limited
    assert m.slew(150.0, 0.0, 0.5) == 0.0                    # a stop is immediate
    assert m.slew(190.0, 200.0, 0.5) == 200.0


@pytest.fixture
def board():
    t = MemoryTransport()
    fake = FakeBoard(t)
    live = Liveness(max_age_s=0.5)
    b = Board(t, BoardConfig(heartbeat_period_s=0.02, heartbeat_timeout_ms=1000,
                             telemetry_period_ms=100, reply_timeout_ms=500),
              liveness=live, name="sim")
    live.touch()
    assert b.open(wait=True, timeout=3.0)
    b._fake = fake
    b._live = live
    yield b
    b.close()
    fake.stop()


def wait(board, cond, timeout=3.0):
    import time
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        board._live.touch()
        if cond():
            return True
        time.sleep(0.01)
    return False


def test_single_motor_driver_runs_stops_and_handles_buttons(board):
    fake = board._fake
    pad = SoftwareGamepad()
    drv = SingleMotorDriver(board, StickMap(max_rpm=120, expo=0.0, dead_zone=0.0, slew_rpm_per_s=10000, avoid_bands=False))
    cmd = drv.tick(pad.set(ly=0.5), dt=1.0)
    assert cmd is not None and cmd.rpm == pytest.approx(60.0) and cmd.direction == 1
    assert wait(board, lambda: any(r.startswith("MOT:RUN 60.0 RPM 1") for r in fake.received)), fake.received
    assert wait(board, lambda: fake.speed == 200)            # 60 rpm x 200 full steps / 60
    cmd = drv.tick(pad.set(ly=-1.0), dt=1.0)
    assert cmd.direction == 0 and cmd.rpm == pytest.approx(-120.0)
    assert wait(board, lambda: any(r.startswith("MOT:RUN 120.0 RPM 0") for r in fake.received))
    drv.tick(pad.set(ly=0.0), dt=1.0)                         # released: stop
    assert wait(board, lambda: "MOT:STOP" in fake.received and fake.speed == 0)
    n_stop = fake.received.count("MOT:STOP")
    drv.tick(pad.set(ly=0.0), dt=1.0)                         # still released: no repeat
    drv.tick(pad.set(ly=0.0), dt=1.0)
    assert fake.received.count("MOT:STOP") == n_stop
    # buttons on their edges: Y enables, X clears, START stops everything
    drv.tick(pad.set(buttons={"Y"}))
    assert wait(board, lambda: "MOT:EN" in fake.received)
    drv.tick(pad.set(buttons=set()))
    drv.tick(pad.set(buttons={"X"}))
    assert wait(board, lambda: "SYST:FAULT:CLEAR" in fake.received[-3:])
    drv.tick(pad.set(buttons=set(), ly=0.8))
    assert wait(board, lambda: fake.speed > 0)
    drv.tick(pad.set(buttons={"START"}))
    assert wait(board, lambda: fake.state == "ESTOP", timeout=5.0)


def test_single_motor_driver_stops_when_the_pad_goes_away(board):
    fake = board._fake
    pad = SoftwareGamepad()
    drv = SingleMotorDriver(board, StickMap(slew_rpm_per_s=10000, expo=0.0, dead_zone=0.0))
    drv.tick(pad.set(ly=0.6), dt=1.0)
    assert wait(board, lambda: fake.speed > 0)
    pad.disconnect()
    drv.tick(pad.poll(), dt=1.0)
    assert wait(board, lambda: fake.speed == 0)
    assert drv.last is not None and drv.last.stopped


def test_band_avoidance_moves_a_demand_off_the_resonance(board):
    fake = board._fake
    pad = SoftwareGamepad()
    drv = SingleMotorDriver(board, StickMap(max_rpm=100, expo=0.0, dead_zone=0.0, slew_rpm_per_s=10000))
    cmd = drv.tick(pad.set(ly=0.6), dt=1.0)                  # 60 rpm: inside the 55-66 band
    assert cmd.rpm == 55.0
    assert wait(board, lambda: any(r.startswith("MOT:RUN 55.0 RPM") for r in fake.received))


class RecordingFleet:
    def __init__(self):
        self.twists = []
        self.stops = 0

    def drive(self, t):
        self.twists.append(t)
        return 1.0

    def stop(self):
        self.stops += 1


def test_base_driver_maps_the_left_stick_and_the_triggers_to_a_twist_and_buttons_to_actions():
    fleet = RecordingFleet()
    actions = []
    # the mapping and the buttons alone: no dead zone, no rate limit (tested below)
    free = BaseMap(max_vx=1.0, max_vy=1.0, max_wz=2.0, stick=StickMap(expo=0.0, dead_zone=0.0),
                   left=StickGate(0.0, 0.0), triggers=(StickGate(0.0, 0.0), StickGate(0.0, 0.0)),
                   right=StickGate(0.0, 0.0))
    drv = BaseDriver(fleet, free, slew=None)
    drv.on_action = actions.append
    # left stick: linear (up = forward, right = -vy); left trigger: counter-clockwise (+wz)
    t = drv.tick(GamepadState(ly=0.5, lx=0.5, lt=0.5, packet=1))
    assert t.vx == pytest.approx(0.5) and t.vy == pytest.approx(-0.5) and t.wz == pytest.approx(1.0)
    assert fleet.twists[-1] == t
    t = drv.tick(GamepadState(ly=0.5, lx=0.5, rt=0.25, rx=-0.5, packet=11))
    assert t.wz == pytest.approx(-0.5)                         # right trigger: clockwise; the right stick does not turn
    drv.base.turn = "right_stick"
    t = drv.tick(GamepadState(ly=0.5, lx=0.5, rx=-0.5, packet=12))
    assert t.wz == pytest.approx(1.0)                          # the right-stick option: stick left = counter-clockwise
    drv.base.turn = "triggers"
    drv.tick(GamepadState(packet=2))                          # centred: one stop
    assert fleet.stops == 1
    drv.tick(GamepadState(packet=3))
    assert fleet.stops == 1
    drv.tick(GamepadState(buttons=frozenset({"START"}), packet=4))
    drv.tick(GamepadState(buttons=frozenset({"START", "X"}), packet=5))
    assert actions == ["estop", "rearm"]
    drv.tick(None)                                             # pad gone: stop
    assert fleet.stops == 2


def test_a_pad_resting_off_centre_does_not_move_the_base_and_the_gate_has_hysteresis():
    # the PowerA 1414135-02 at rest, bench 2026-10-20: left stick (0.077, 0.076), LT 0.03
    fleet = RecordingFleet()
    drv = BaseDriver(fleet, slew=None)
    t = drv.tick(GamepadState(lx=0.077, ly=0.076, lt=0.03, packet=1))
    assert t == K.Twist() and fleet.twists == []
    g = StickGate(enter=0.20, exit=0.15)
    assert g.gate(0.0, 0.18) == (0.0, 0.0)               # inside enter: still
    x, y = g.gate(0.0, 0.25)                               # past enter: moving, rescaled from the exit edge
    assert x == 0.0 and y == pytest.approx((0.25 - 0.15) / 0.85)
    assert g.gate(0.0, 0.18)[1] > 0.0                      # back between exit and enter: still moving (no chatter)
    assert g.gate(0.0, 0.14) == (0.0, 0.0)                 # inside exit: stopped
    assert g.gate(0.0, 1.0)[1] == pytest.approx(1.0)       # full deflection is still full


def test_the_twist_ramps_quickly_and_b_or_a_lost_pad_stop_at_once():
    fleet = RecordingFleet()
    base = BaseMap(stick=StickMap(expo=0.0, dead_zone=0.0))
    drv = BaseDriver(fleet, base, slew=TwistSlew(accel=1.5, decel=3.0))
    full = GamepadState(ly=1.0, packet=1)
    t = drv.tick(full, dt=0.1)
    assert t.vx == pytest.approx(0.15)                     # 1.5 m/s^2 for 0.1 s
    for _ in range(3):
        t = drv.tick(full, dt=0.1)
    assert t.vx == pytest.approx(0.5)                      # full (0.5 m/s) after about a third of a second
    t = drv.tick(GamepadState(packet=2), dt=0.1)           # released: slows at 3 m/s^2
    assert t.vx == pytest.approx(0.2)
    t = drv.tick(GamepadState(packet=3), dt=0.1)
    assert t == K.Twist() and fleet.stops == 1             # stopped within 0.2 s
    drv.tick(full, dt=0.1)
    t = drv.tick(GamepadState(ly=1.0, buttons=frozenset({'B'}), packet=4), dt=0.1)
    assert t == K.Twist() and fleet.stops == 2             # B: at once, no ramp
    drv.tick(full, dt=0.1)
    drv.tick(None, dt=0.1)
    assert fleet.stops == 3                                # pad gone: at once


def test_max_rpm_sets_full_stick_and_caps_every_motor():
    from smc_core.drive_map import full_scale_for_rpm
    g = K.Geometry()
    full = full_scale_for_rpm(100, g)
    sps = 100 * g.full_steps_per_rev / 60.0                 # 333 full steps/s
    cmds, scale = K.motor_commands(K.Twist(vx=full.vx), g)
    assert scale == 1.0 and all(v == round(sps) for v, _ in cmds.values())      # full forward: 100 RPM each
    cmds, _ = K.motor_commands(K.Twist(wz=full.wz), g)
    assert all(v == round(sps) for v, _ in cmds.values())                        # full turn: 100 RPM each
    capped = K.Geometry(max_motor_sps=sps)
    cmds, scale = K.motor_commands(K.Twist(vx=full.vx, vy=full.vy), capped)      # a diagonal asks 2x
    assert scale == pytest.approx(0.5) and max(v for v, _ in cmds.values()) <= round(sps)
    # the driver: full scale, LCD scale and the ramps follow; the fleet is told the cap
    class CapFleet(RecordingFleet):
        lcd_full_scale = None
        cap = None
        def set_max_rpm(self, rpm): self.cap = rpm
    fleet = CapFleet()
    drv = BaseDriver(fleet)
    drv.set_max_rpm(200, g)
    assert drv.base.max_vx == pytest.approx(2 * full.vx) and fleet.cap == 200
    assert fleet.lcd_full_scale.vx == pytest.approx(2 * full.vx)
    assert drv.slew.accel == pytest.approx(3 * 2 * full.vx)                     # full speed still in ~1/3 s


def test_start_e_stops_and_holding_it_3_s_clears():
    now = [0.0]
    fleet = RecordingFleet()
    actions = []
    drv = BaseDriver(fleet, slew=None, clock=lambda: now[0])
    drv.on_action = actions.append
    start = GamepadState(buttons=frozenset({"START"}), packet=1)
    drv.tick(start)
    assert actions == ["estop"]                              # the press: E-STOP
    for t in (1.0, 2.0, 2.9):
        now[0] = t
        drv.tick(start)
    assert actions == ["estop"]                              # not yet
    now[0] = 3.0
    drv.tick(start)
    assert actions == ["estop", "rearm"]                     # held 3 s: cleared
    now[0] = 6.5
    drv.tick(start)
    assert actions == ["estop", "rearm"]                     # once per hold
    now[0] = 7.0
    drv.tick(GamepadState(packet=2))                         # released
    now[0] = 7.1
    drv.tick(start)
    assert actions == ["estop", "rearm", "estop"]            # a new press E-STOPs again


def test_the_single_motor_driver_clears_its_fault_after_a_3_s_start_hold():
    now = [0.0]
    sent = []
    class Board:
        def estop(self): sent.append("ESTOP")
        def submit(self, req): sent.append(req.line)
        def set_latest(self, key, req): pass
    drv = SingleMotorDriver(Board(), clock=lambda: now[0])
    start = GamepadState(buttons=frozenset({"START"}), packet=1)
    drv.tick(start)
    now[0] = 3.0
    drv.tick(start)
    assert sent == ["ESTOP", "SYST:FAULT:CLEAR"]


def test_full_stick_is_full_speed_in_every_direction():
    # 2026-10-20: the expo curve acted per axis, so a full diagonal showed ~75 % on the LCD
    base = BaseMap()
    full = K.Twist(vx=base.max_vx, vy=base.max_vy, wz=base.max_wz)
    for lx, ly in ((0.0, 1.0), (-1.0, 0.0), (-0.7071, -0.7071), (0.6, 0.8)):
        t = base.twist(GamepadState(lx=lx, ly=ly))
        assert K.indicator(t, full)[3] == 100, (lx, ly, t)
    half = base.twist(GamepadState(lx=-0.5, ly=-0.5))       # a half diagonal: the same as half straight
    straight = base.twist(GamepadState(ly=-0.7071))
    assert K.indicator(half, full)[3] == K.indicator(straight, full)[3]
