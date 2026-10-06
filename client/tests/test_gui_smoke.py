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
