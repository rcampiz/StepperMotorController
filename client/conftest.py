"""pytest root for client/: the packages (smc_core, ...) live here, legacy code in src/."""
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
for p in (_HERE, os.path.join(_HERE, "src")):
    if p not in sys.path:
        sys.path.insert(0, p)

# Headless Qt for the GUI smoke tests
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")


import pytest  # noqa: E402


@pytest.fixture(scope="session")
def qapp():
    """One QApplication for the whole run (widgets need QApplication, not QCoreApplication)."""
    QtWidgets = pytest.importorskip("PySide6.QtWidgets")
    app = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
    yield app
