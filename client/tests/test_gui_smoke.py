"""Headless smoke test: the GUI still builds after every refactor step (no board needed)."""

import pytest

pytest.importorskip("PySide6")


def test_main_window_constructs_and_closes(qapp):
    app = qapp
    from smc_gui.main_window import MainWindow
    w = MainWindow()
    w.show()
    app.processEvents()
    assert w.windowTitle()
    w.close()
    app.processEvents()


def test_the_pairing_dialog_shows_this_systems_steps(qapp, monkeypatch):
    """Windows: the steps and a button to its Bluetooth settings; Linux (the Raspberry Pi): the
    steps and Find and pair, which runs bt_pair on a worker thread and shows its log."""
    from smc_gui import pairing_dialog as pd
    win = pd.PairingDialog(platform="win32")
    assert win.kind == "windows" and win.action.text() == "Open Bluetooth settings"
    assert "Wireless Controller" in win.steps.text()
    monkeypatch.setattr(pd.bt_pair, "available", lambda: True)
    monkeypatch.setattr(pd.bt_pair, "pair_playstation_pad", lambda log: (log("[NEW] Device A4:AE:12:34:56:78"), "A4:AE:12:34:56:78")[1])
    lin = pd.PairingDialog(platform="linux")
    assert lin.kind == "linux" and lin.action.text() == "Find and pair" and lin.action.isEnabled()
    lin.find_and_pair()
    import time
    end = time.monotonic() + 3.0
    while "paired" not in lin.result.text() and time.monotonic() < end:
        qapp.processEvents()
    assert lin.result.text() == "paired: A4:AE:12:34:56:78" and "[NEW] Device" in lin.log.toPlainText()
