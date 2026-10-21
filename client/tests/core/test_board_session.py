"""The session moves a serial link to a fast rate after setup, and hands it back on close."""
import pytest

from smc_core.board import BOOT_BAUD, Board, BoardConfig
from smc_core.sim import FakeBoard
from smc_core.transport.memory import MemoryTransport


class RatePipe(MemoryTransport):
    """A test pipe with a rate the client can change (as a serial port has)."""
    baudrate = BOOT_BAUD

    def set_baudrate(self, rate: int) -> None:
        self.baudrate = rate


def test_the_session_moves_to_the_fast_rate_and_hands_back_the_boot_rate():
    t = RatePipe(name="sim")
    fake = FakeBoard(t)
    b = Board(t, BoardConfig(heartbeat=False, telemetry_period_ms=0, fast_baud=460800), name="sim")
    assert b.open(wait=True, timeout=5.0)
    assert t.baudrate == 460800 and fake.baud == 460800
    b.close()
    assert fake.baud == BOOT_BAUD


def test_without_fast_baud_or_without_a_rate_the_link_stays():
    t = RatePipe(name="sim")
    fake = FakeBoard(t)
    b = Board(t, BoardConfig(heartbeat=False, telemetry_period_ms=0), name="sim")
    assert b.open(wait=True, timeout=5.0)
    assert t.baudrate == BOOT_BAUD and not hasattr(fake, "baud")
    b.close()
    plain = MemoryTransport(name="sim")                        # TCP-like: no rate at all
    FakeBoard(plain)
    b = Board(plain, BoardConfig(heartbeat=False, telemetry_period_ms=0, fast_baud=460800), name="sim")
    assert b.open(wait=True, timeout=5.0)
    b.close()
