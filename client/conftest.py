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
