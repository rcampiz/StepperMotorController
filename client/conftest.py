"""pytest root for client/: smc_core, smc_gui and smc_cli live here."""
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

# Headless Qt for the GUI smoke tests
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")


import pytest  # noqa: E402


@pytest.fixture(scope="session")
def qapp():
    """One QApplication for the whole run (widgets need QApplication, not QCoreApplication)."""
    QtWidgets = pytest.importorskip("PySide6.QtWidgets")
    app = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
    yield app


@pytest.fixture(autouse=True)
def _delete_windows():
    """Every test's windows are deleted when it ends (after its own fixtures' teardown). Closing
    only hid them: they stayed alive with their timers firing, and every app-wide restyle went
    through all of them (the light-mode test took 35 s at the end of the GUI tests, 2026-10-22)."""
    yield
    if "PySide6.QtWidgets" not in sys.modules:
        return
    from PySide6.QtCore import QCoreApplication, QEvent
    from PySide6.QtWidgets import QApplication
    app = QApplication.instance()
    if app is None:
        return
    # Only the GUI's own windows (a main window, a panel shown on its own): pyqtgraph's menus are
    # parentless windows too but belong to Python objects, and Qt aborts when one is deleted
    # under them; a menu inside one of ours goes with it
    from PySide6.QtWidgets import QMainWindow
    for w in app.topLevelWidgets():
        if w.parent() is None and (isinstance(w, QMainWindow) or type(w).__module__.startswith("smc_gui")):
            w.close()
            w.deleteLater()
    QCoreApplication.sendPostedEvents(None, QEvent.DeferredDelete)
    app.processEvents()
