"""Slip: encoder against commanded position, in shaft degrees."""
import pytest

from smc_core.slip import SlipTracker


def test_no_slip_when_the_encoder_follows_and_a_lost_step_shows_as_1_8_degrees():
    s = SlipTracker(usteps_per_rev=3200, counts_per_rev=4000)
    assert s.update(0, 1000) == 0.0                          # the baseline
    assert s.update(3200, 5000) == pytest.approx(0.0)        # one turn each
    assert s.update(6400, 9000 - 20) == pytest.approx(-1.8)  # 20 counts behind = one full step lost


def test_an_encoder_wired_the_other_way_is_learned():
    s = SlipTracker(usteps_per_rev=3200, counts_per_rev=4000)
    s.update(0, 0)
    assert s.update(200, -250) == 0.0                        # under 45 degrees: not learned yet
    assert s.update(800, -1000) == pytest.approx(0.0)        # 90 degrees, counting down
    assert s.sign == -1
    assert s.update(1600, -1990) == pytest.approx(-0.9)      # 10 counts short


def test_the_drivers_position_counter_wraps():
    s = SlipTracker(usteps_per_rev=3200, counts_per_rev=4000, position_modulus=1 << 22)
    top = (1 << 21) - 100
    s.update(top, 0)
    assert s.update(top + 800 - (1 << 22), 1000) == pytest.approx(0.0)   # wrapped past +2^21


def test_zero_and_a_resolution_change_start_a_new_baseline():
    s = SlipTracker(usteps_per_rev=3200, counts_per_rev=4000)
    s.update(0, 0)
    s.update(3200, 3990)
    assert s.last == pytest.approx(-0.9)
    s.reset()
    s.update(3200, 3990)
    assert s.update(6400, 7990) == pytest.approx(0.0)
    s.set_geometry(usteps_per_rev=25600)                       # 1/128: positions rescaled
    s.update(51200, 7990)
    assert s.update(76800, 11990) == pytest.approx(0.0)
