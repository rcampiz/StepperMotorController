"""The stick-to-motor mapping and the drivers on the fake board / a recording fleet."""
import pytest

from smc_core import kinematics as K
from smc_core.board import Board, BoardConfig, Liveness
from smc_core.drive_map import BaseDriver, BaseMap, SingleMotorDriver, StickMap
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
    drv = BaseDriver(fleet, BaseMap(max_vx=1.0, max_vy=1.0, max_wz=2.0, stick=StickMap(expo=0.0, dead_zone=0.0)))
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
