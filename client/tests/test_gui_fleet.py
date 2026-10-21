"""Fleet page against four virtual Nucleos: open by role, drive a twist, see the lines the
coordinator sent to each board, base E-STOP and re-arm. Skipped when sim/ is not built."""
import time

import pytest

from smc_core import kinematics as K
from smc_core.fleet import ROLES

from . import vnucleo

pytest.importorskip("PySide6")


def pump(app, cond, timeout=10.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        app.processEvents()
        if cond():
            return True
        time.sleep(0.01)
    return False


@pytest.fixture
def panel(tmp_path):
    from PySide6.QtWidgets import QApplication
    from smc_gui.fleet_panel import FleetPanel
    app = QApplication.instance() or QApplication([])
    with vnucleo.boards(4, log_dir=str(tmp_path)) as procs:
        p = FleetPanel()
        p.set_addresses({r: procs[i][0] for i, r in enumerate(ROLES)},
                        {"FL": "VN000001", "FR": "VN000002", "RL": "VN000003", "RR": "VN000004"})
        yield app, p
        p.shutdown()
        app.processEvents()


def test_fleet_page_opens_drives_logs_and_estops(panel):
    app, p = panel
    p.open_fleet()
    assert pump(app, lambda: p.opened, timeout=15.0), p._status.text()
    assert p._serial["RR"].text() == "VN000004"
    # a forward twist runs every wheel: MOT:RUN lines to all four boards in the log
    p._clear_log()
    scale = p.drive(K.Twist(vx=0.2))
    assert scale == 1.0
    assert pump(app, lambda: sum(1 for l in p.sent_lines() if "MOT:RUN" in l) >= 4)
    roles = {l.split()[1] for l in p.sent_lines() if "MOT:RUN" in l}
    assert roles == set(ROLES)
    assert pump(app, lambda: p._speed["FL"].text() not in ("--", "0 st/s"), timeout=5.0), p._speed["FL"].text()
    assert pump(app, lambda: p._measured.text().startswith("measured: vx 0.2"), timeout=5.0), p._measured.text()
    # the mission runner steps through the script the way the Pi would
    p._mission_edit.setPlainText('{"vy": 0.1, "seconds": 0.3}\n{"stop": true}')
    p.run_mission()
    assert pump(app, lambda: p._mission_status.text() == "done", timeout=5.0)
    assert pump(app, lambda: any("MOT:STOP" in l for l in p.sent_lines()), timeout=3.0)
    # base E-STOP: confirmed on all four, then re-arm
    p._bridge.action("estop")
    assert pump(app, lambda: p.last_estop is not None, timeout=10.0)
    assert p.last_estop.confirmed
    assert pump(app, lambda: p._state["FR"].text() == "ESTOP", timeout=5.0)
    p._bridge.action("rearm")
    assert pump(app, lambda: p._status.text() == "re-armed", timeout=10.0)
    p.close_fleet()
    assert pump(app, lambda: not p.opened, timeout=10.0)


def test_wrong_serial_is_refused(panel):
    app, p = panel
    p._want["FL"].setText("VN000002")
    p.open_fleet()
    assert pump(app, lambda: p._status.text().startswith("open failed"), timeout=15.0)
    assert "wrong corner" in p._status.text()


def test_test_mode_drives_the_boards_you_have_with_virtual_ones_in_the_empty_slots(tmp_path):
    """One "real" controller (a virtual Nucleo standing in for COM5) in the FR slot, the rest
    set to "virtual": test mode starts three virtual Nucleos, opens the base, arms the pad, and
    the pad's left stick and triggers drive every wheel, the real one included."""
    from PySide6.QtWidgets import QApplication
    from smc_core.gamepad import SoftwareGamepad
    from smc_gui.fleet_panel import VIRTUAL, FleetPanel
    app = QApplication.instance() or QApplication([])
    with vnucleo.boards(1, serial_base=99, log_dir=str(tmp_path)) as procs:   # a serial no virtual slot uses
        real = procs[0][0]
        p = FleetPanel()
        released = []
        p.release_port = released.append
        try:
            p.set_addresses({"FL": VIRTUAL, "FR": real, "RL": VIRTUAL, "RR": VIRTUAL})
            pad = SoftwareGamepad()
            p.controller.use_gamepad(pad)
            assert p.start_test_mode(), p._status.text()
            assert released == [real]                                  # the window's link lets it go
            assert len(p._virtual) == 3
            assert pump(app, lambda: p.opened, timeout=20.0), p._status.text()
            assert p.test_mode and p.controller.driving
            assert "TEST MODE" in p._status.text() and "1 real (FR)" in p._status.text()
            p._clear_log()
            pad.set(ly=0.6, rx=-0.3)                                   # forward and turning left
            assert pump(app, lambda: {l.split()[1] for l in p.sent_lines() if "MOT:RUN" in l} == set(ROLES),
                        timeout=5.0), p.sent_lines()[-8:]
            pad.set(ly=0.0, rx=0.0)
            p.close_fleet()
            assert pump(app, lambda: not p.opened, timeout=10.0)
            assert not p.test_mode and not p.controller.driving and p._virtual == []
        finally:
            p.shutdown()
            app.processEvents()
