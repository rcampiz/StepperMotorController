"""
Motion units and powerSTEP01 register scaling (client refactor step 4).

Pure arithmetic, shared by every frontend so a speed typed in the GUI and the same
speed in a CLI scenario produce the same command bytes.

Conventions (firmware): MOT:RUN, the SPEED/ACC/DEC registers and the telemetry speed
are in FULL steps/s (and steps/s^2); positions (MOT:MOVE/GOTO, ABS_POS) are in
MICROSTEPS. Microsteps per revolution = full steps per rev << step mode (0..7).
"""

from dataclasses import dataclass

SPEED_UNITS = ("RPM", "rev/s", "steps/s")
POSITION_UNITS = ("rev", "deg", "steps")
ACCEL_UNITS = ("rev/s²", "steps/s²")

ACC_DEC_FACTOR = 14.5519    # steps/s^2 per raw ACC/DEC register unit (DRV:CFG:MOTION)
MAXSPD_FACTOR = 15.2588     # steps/s per raw MAX_SPEED register unit


@dataclass(frozen=True)
class Geometry:
    full_steps_per_rev: int = 200
    step_mode: int = 3                    # 0 = full step ... 7 = 1/128

    @property
    def microsteps_per_rev(self) -> int:
        return self.full_steps_per_rev * (1 << self.step_mode)


def speed_to_sps(value: float, unit: int, g: Geometry) -> int:
    """User speed (SPEED_UNITS index) -> full steps/s."""
    if unit == 0:
        return round(value * g.full_steps_per_rev / 60.0)
    if unit == 1:
        return round(value * g.full_steps_per_rev)
    return round(value)


def sps_to_speed(sps: float, unit: int, g: Geometry) -> float:
    """Full steps/s -> user speed (SPEED_UNITS index)."""
    if g.full_steps_per_rev == 0:
        return 0.0
    if unit == 0:
        return sps * 60.0 / g.full_steps_per_rev
    if unit == 1:
        return sps / g.full_steps_per_rev
    return float(sps)


def position_to_microsteps(value: float, unit: int, g: Geometry) -> int:
    """User position (POSITION_UNITS index) -> microsteps."""
    if unit == 0:
        return round(value * g.microsteps_per_rev)
    if unit == 1:
        return round(value / 360.0 * g.microsteps_per_rev)
    return round(value)


def microsteps_to_position(usteps: float, unit: int, g: Geometry) -> float:
    if g.microsteps_per_rev == 0:
        return 0.0
    if unit == 0:
        return usteps / g.microsteps_per_rev
    if unit == 1:
        return usteps / g.microsteps_per_rev * 360.0
    return float(usteps)


def accel_to_sps2(value: float, unit: int, g: Geometry) -> int:
    """User acceleration (ACCEL_UNITS index) -> full steps/s^2."""
    if unit == 0:
        return round(value * g.full_steps_per_rev)
    return round(value)


def rpm_to_sps(rpm: float, g: Geometry) -> int:
    return round(rpm * g.full_steps_per_rev / 60.0)


def sps_to_rpm(sps: float, g: Geometry) -> float:
    return sps * 60.0 / g.full_steps_per_rev if g.full_steps_per_rev else 0.0


# --- driver registers ---------------------------------------------------------------
def kval_from_percent(pct: float) -> int:
    return max(0, min(255, round(pct / 100.0 * 255)))


def kval_to_percent(raw: int) -> int:
    return round(raw / 255.0 * 100)


# Current mode: TVAL is the coil current reference, 7.8 mV per unit across the shield's
# 0.1 ohm sense resistors (X-NUCLEO-IHM03A1): 78 mA per unit, 0x7F = 9.9 A.
TVAL_AMPS_PER_UNIT = 0.078


def tval_from_amps(amps: float) -> int:
    return max(0, min(0x7F, round(amps / TVAL_AMPS_PER_UNIT)))


def tval_to_amps(raw: int) -> float:
    return round(raw * TVAL_AMPS_PER_UNIT, 2)


def acc_raw_from_sps2(sps2: float) -> int:
    return max(1, round(sps2 / ACC_DEC_FACTOR))


def acc_sps2_from_raw(raw: int) -> int:
    return round(raw * ACC_DEC_FACTOR)


def maxspd_raw_from_sps(sps: float) -> int:
    return max(1, round(sps / MAXSPD_FACTOR))


def maxspd_sps_from_raw(raw: int) -> int:
    return round(raw * MAXSPD_FACTOR)


# powerSTEP01 ADC_OUT: 5-bit reading of the ADCIN pin, which the X-NUCLEO-IHM03A1 ties to a
# divider on the motor supply. One count is coarse (about 1/32 of full scale). The scale
# below is a single-point bench calibration (2026-10-18: ADC_OUT = 2 at 12.51 V, taken as
# 2.5 counts for a truncating converter); treat the result as a supply-present indicator.
ADC_OUT_VOLTS_PER_COUNT = 5.0


def supply_volts_from_adc(raw: int) -> float:
    return round(raw * ADC_OUT_VOLTS_PER_COUNT, 1)
