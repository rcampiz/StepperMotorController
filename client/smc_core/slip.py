"""
Slip: how far the shaft is from where the motor was commanded, in shaft degrees.

The commanded position is the driver's step count (microsteps at the resolution in force), the
real one the encoder's count; both are turned into shaft degrees from a baseline (start, or the
operator's "zero") and compared. A full step is 360 / full_steps_per_rev degrees (1.8 degrees
on a 200-step motor), so a lost step shows as a 1.8 degree jump.

Details handled here: the driver's position counter wraps (a powerSTEP01's is 22 bits), the
encoder may be wired to count the other way (the sign is learned once the motor has moved
`learn_deg`), and a resolution change restarts from a new baseline.
"""
from typing import Optional


class SlipTracker:
    def __init__(self, usteps_per_rev: float = 3200.0, counts_per_rev: float = 4000.0,
                 position_modulus: int = 1 << 22, learn_deg: float = 45.0):
        self.usteps_per_rev = float(usteps_per_rev)
        self.counts_per_rev = float(counts_per_rev)
        self.position_modulus = int(position_modulus)
        self.learn_deg = float(learn_deg)
        self.sign: Optional[int] = None          # +1 encoder counts with the motor, -1 against
        self.reset()

    def reset(self) -> None:
        """A new baseline: the next sample is zero slip (keeps the learned encoder sign)."""
        self._last_raw: Optional[int] = None
        self._motor = 0                          # unwrapped microsteps since the baseline
        self._enc0: Optional[int] = None
        self.last: float = 0.0

    def set_geometry(self, usteps_per_rev: Optional[float] = None,
                     counts_per_rev: Optional[float] = None) -> None:
        """The resolution in force or the encoder changed: restart from a new baseline."""
        changed = False
        if usteps_per_rev and float(usteps_per_rev) != self.usteps_per_rev:
            self.usteps_per_rev, changed = float(usteps_per_rev), True
        if counts_per_rev and float(counts_per_rev) != self.counts_per_rev:
            self.counts_per_rev, changed = float(counts_per_rev), True
        if changed:
            self.reset()

    def update(self, motor_usteps: int, encoder_counts: int) -> float:
        """One telemetry sample -> slip in shaft degrees (encoder minus commanded; 0 until the
        encoder's direction is known)."""
        raw = int(motor_usteps)
        if self._last_raw is None:
            self._last_raw, self._enc0, self._motor = raw, int(encoder_counts), 0
            self.last = 0.0
            return self.last
        d = raw - self._last_raw
        half = self.position_modulus // 2
        if d > half:
            d -= self.position_modulus
        elif d < -half:
            d += self.position_modulus
        self._motor += d
        self._last_raw = raw
        motor_deg = self._motor / self.usteps_per_rev * 360.0
        enc_deg = (int(encoder_counts) - self._enc0) / self.counts_per_rev * 360.0
        if self.sign is None:
            if abs(motor_deg) < self.learn_deg:
                self.last = 0.0
                return self.last
            self.sign = 1 if enc_deg * motor_deg >= 0 else -1
        self.last = self.sign * enc_deg - motor_deg
        return self.last
