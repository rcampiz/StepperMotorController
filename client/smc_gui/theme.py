"""
The GUI's look, in one place: two palettes (dark, the same cyberpunk set the LCD uses, and
a light one for readability in daylight) and the application stylesheet built from the
active palette.

Panels read colours as `theme.CYAN` etc. at the moment they style a widget, so after
`set_mode()` anything built or restyled afterwards picks the new palette; MainWindow calls
the panels' `restyle()` for the few widgets that style themselves once. The choice is
kept in QSettings ("ui/theme").
"""

import os
import tempfile
from typing import Dict

_DARK: Dict[str, object] = dict(
    BG="#080810",          # the page: near-black with a blue cast
    BASE="#0c0e1a",        # inputs, plots
    PANEL="#101830",       # raised boxes, tab bar, buttons
    SCAN="#0a1020",        # alternate rows
    GRID="#203858",        # borders, minor grid
    GRID_MAJ="#386488",    # rules, major grid, hover borders
    CYAN="#00f0ff",        # primary neon: titles, labels, measured values
    MAGENTA="#ff18d8",     # secondary neon: selection, setpoints, needles
    VIOLET="#9846ff",
    YELLOW="#ffe400",      # hazard: warnings, holding coils
    ORANGE="#ff8000",
    GREEN="#28ff40",       # running, OK
    RED="#ff2030",         # faults
    TEXT="#d8e4f0",
    DIM="#485068",
    MUTED="#8090a8",       # secondary text that must still be readable
    STATE_COLOURS={
        "IDLE": ("#0a2a30", "#00f0ff"),
        "ARMED": ("#2a2800", "#ffe400"),
        "RUNNING": ("#082a10", "#28ff40"),
        "FAULT": ("#2a0810", "#ff2030"),
        "ESTOP": ("#3a0810", "#ff6080"),
    },
)

_LIGHT: Dict[str, object] = dict(
    BG="#f3f5f9",
    BASE="#ffffff",
    PANEL="#e4e9f2",
    SCAN="#eef1f6",
    GRID="#c9d2de",
    GRID_MAJ="#93a2b8",
    CYAN="#0077a8",        # the neons, deepened so they read on white
    MAGENTA="#b8127a",
    VIOLET="#6a34c8",
    YELLOW="#9a7a00",
    ORANGE="#c25a00",
    GREEN="#178a2c",
    RED="#c41a2a",
    TEXT="#141a26",
    DIM="#9aa4b4",
    MUTED="#5a6678",
    STATE_COLOURS={
        "IDLE": ("#d6f2f8", "#0077a8"),
        "ARMED": ("#fbf2c8", "#8a6a00"),
        "RUNNING": ("#d8f5dc", "#178a2c"),
        "FAULT": ("#fbd9dc", "#c41a2a"),
        "ESTOP": ("#f8c8d0", "#b01020"),
    },
)

MODE = "dark"
# Trace colours for plots, in the order panels assign them
TRACES = ["#00f0ff", "#ff18d8", "#ffe400", "#28ff40", "#ff8000", "#9846ff", "#ff80c0", "#80ffe0"]
QSS = ""


def _arrow_image(up: bool, colour: str) -> str:
    """A small spin-box arrow (styled buttons lose Qt's own) in `colour`, drawn once into the
    temp folder; the stylesheet points at it. "" when it cannot be drawn (no Qt GUI)."""
    path = os.path.join(tempfile.gettempdir(), "smc_gui",
                        "arrow_%s_%s.png" % ("up" if up else "down", colour.lstrip("#")))
    if not os.path.exists(path):
        try:
            from PySide6.QtCore import QPointF, Qt
            from PySide6.QtGui import QColor, QImage, QPainter, QPolygonF
            os.makedirs(os.path.dirname(path), exist_ok=True)
            img = QImage(10, 6, QImage.Format_ARGB32)
            img.fill(Qt.transparent)
            p = QPainter(img)
            p.setRenderHint(QPainter.Antialiasing)
            p.setPen(Qt.NoPen)
            p.setBrush(QColor(colour))
            pts = [QPointF(0, 6), QPointF(10, 6), QPointF(5, 0)] if up else [QPointF(0, 0), QPointF(10, 0), QPointF(5, 6)]
            p.drawPolygon(QPolygonF(pts))
            p.end()
            if not img.save(path):
                return ""
        except Exception:                                   # noqa: BLE001 - no GUI: Qt's default
            return ""
    return path.replace("\\", "/")


def _build_qss() -> str:
    g = globals()
    BG, BASE, PANEL, SCAN, GRID, GRID_MAJ = g["BG"], g["BASE"], g["PANEL"], g["SCAN"], g["GRID"], g["GRID_MAJ"]
    CYAN, MAGENTA, TEXT, DIM, MUTED = g["CYAN"], g["MAGENTA"], g["TEXT"], g["DIM"], g["MUTED"]
    return f"""
QWidget {{ background: {BG}; color: {TEXT}; font-size: 12px; }}
QMainWindow, QDialog {{ background: {BG}; }}
QToolTip {{ background: {PANEL}; color: {TEXT}; border: 1px solid {CYAN}; padding: 4px; }}

QMenuBar {{ background: {PANEL}; color: {TEXT}; border-bottom: 1px solid {GRID_MAJ}; }}
QMenuBar::item:selected {{ background: {GRID}; color: {CYAN}; }}
QMenu {{ background: {PANEL}; color: {TEXT}; border: 1px solid {GRID_MAJ}; }}
QMenu::item:selected {{ background: {GRID}; color: {CYAN}; }}

QGroupBox {{
    border: 1px solid {GRID_MAJ}; border-radius: 0px; margin-top: 14px; padding-top: 6px;
    background: {BG};
}}
QGroupBox::title {{
    subcontrol-origin: margin; subcontrol-position: top left; left: 10px; padding: 0 6px;
    color: {CYAN}; font-weight: bold; letter-spacing: 1px;
}}

QTabWidget::pane {{ border: 1px solid {GRID_MAJ}; top: -1px; background: {BG}; }}
QTabBar::tab {{
    background: {PANEL}; color: {MUTED}; padding: 7px 18px; margin-right: 2px;
    border: 1px solid {GRID}; border-bottom: none;
}}
QTabBar::tab:selected {{ background: {BG}; color: {CYAN}; border: 1px solid {GRID_MAJ}; border-bottom: 2px solid {MAGENTA}; }}
QTabBar::tab:hover:!selected {{ color: {TEXT}; border-color: {GRID_MAJ}; }}

QPushButton {{
    background: {PANEL}; color: {TEXT}; border: 1px solid {GRID_MAJ}; padding: 5px 12px; border-radius: 0px;
}}
QPushButton:hover {{ border-color: {CYAN}; color: {CYAN}; }}
QPushButton:pressed {{ background: {GRID}; }}
QPushButton:checked {{ background: {GRID}; color: {CYAN}; border-color: {CYAN}; }}
QPushButton:disabled {{ color: {DIM}; border-color: {GRID}; background: {BG}; }}

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QTextEdit, QPlainTextEdit, QListWidget, QTableWidget, QTreeWidget {{
    background: {BASE}; color: {TEXT}; border: 1px solid {GRID_MAJ}; padding: 2px 4px;
    selection-background-color: {MAGENTA}; selection-color: {BG};
}}
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled {{ color: {DIM}; border-color: {GRID}; }}
QComboBox QAbstractItemView {{ background: {PANEL}; color: {TEXT}; selection-background-color: {GRID}; selection-color: {CYAN}; }}
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {{ background: {PANEL}; border-left: 1px solid {GRID}; width: 16px; }}
QSpinBox::up-button:hover, QSpinBox::down-button:hover, QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover {{ background: {GRID}; }}
/* Styled buttons lose Qt's own arrows: draw them, in the neon (they were invisible, 2026-10-20) */
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {{ image: url("{_arrow_image(True, CYAN)}"); width: 10px; height: 6px; }}
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {{ image: url("{_arrow_image(False, CYAN)}"); width: 10px; height: 6px; }}
QSpinBox::up-arrow:disabled, QDoubleSpinBox::up-arrow:disabled {{ image: url("{_arrow_image(True, DIM)}"); }}
QSpinBox::down-arrow:disabled, QDoubleSpinBox::down-arrow:disabled {{ image: url("{_arrow_image(False, DIM)}"); }}
QHeaderView::section {{ background: {PANEL}; color: {CYAN}; border: 1px solid {GRID}; padding: 3px; }}

QCheckBox::indicator, QRadioButton::indicator {{ width: 14px; height: 14px; border: 1px solid {GRID_MAJ}; background: {BASE}; }}
QCheckBox::indicator:checked, QRadioButton::indicator:checked {{ background: {CYAN}; border-color: {CYAN}; }}
QCheckBox:disabled, QRadioButton:disabled, QLabel:disabled {{ color: {DIM}; }}

QScrollBar:vertical {{ background: {BG}; width: 10px; margin: 0; }}
QScrollBar::handle:vertical {{ background: {GRID_MAJ}; min-height: 24px; }}
QScrollBar::handle:vertical:hover {{ background: {CYAN}; }}
QScrollBar:horizontal {{ background: {BG}; height: 10px; margin: 0; }}
QScrollBar::handle:horizontal {{ background: {GRID_MAJ}; min-width: 24px; }}
QScrollBar::add-line, QScrollBar::sub-line {{ width: 0; height: 0; }}
QScrollArea {{ border: none; }}

QProgressBar {{ background: {BASE}; border: 1px solid {GRID_MAJ}; color: {TEXT}; text-align: center; }}
QProgressBar::chunk {{ background: {CYAN}; }}
QStatusBar {{ background: {PANEL}; color: {MUTED}; border-top: 1px solid {GRID_MAJ}; }}
QSplitter::handle {{ background: {GRID}; }}
"""


def set_mode(mode: str) -> None:
    """Bind the palette names (theme.CYAN ...) to the dark or the light set."""
    global MODE, QSS
    palette = _LIGHT if mode == "light" else _DARK
    MODE = "light" if mode == "light" else "dark"
    globals().update(palette)
    QSS = _build_qss()


set_mode("dark")


def saved_mode() -> str:
    try:
        from PySide6.QtCore import QSettings
        return str(QSettings("SMC", "StepperMotorController").value("ui/theme", "dark"))
    except Exception:                          # noqa: BLE001
        return "dark"


def save_mode(mode: str) -> None:
    try:
        from PySide6.QtCore import QSettings
        QSettings("SMC", "StepperMotorController").setValue("ui/theme", mode)
    except Exception:                          # noqa: BLE001
        pass


def apply_theme(app, mode: str = None) -> None:
    """Palette for the widgets the stylesheet does not reach, then the stylesheet.
    mode None: the saved choice."""
    from PySide6.QtGui import QColor, QPalette
    set_mode(mode or saved_mode())
    g = globals()
    palette = QPalette()
    for role, hexcol in ((QPalette.Window, g["BG"]), (QPalette.WindowText, g["TEXT"]), (QPalette.Base, g["BASE"]),
                         (QPalette.AlternateBase, g["SCAN"]), (QPalette.ToolTipBase, g["PANEL"]),
                         (QPalette.ToolTipText, g["TEXT"]), (QPalette.Text, g["TEXT"]), (QPalette.Button, g["PANEL"]),
                         (QPalette.ButtonText, g["TEXT"]), (QPalette.BrightText, g["RED"]), (QPalette.Link, g["CYAN"]),
                         (QPalette.Highlight, g["MAGENTA"]), (QPalette.HighlightedText, g["BG"])):
        palette.setColor(role, QColor(hexcol))
    for role in (QPalette.WindowText, QPalette.Text, QPalette.ButtonText):
        palette.setColor(QPalette.Disabled, role, QColor(g["DIM"]))
    app.setPalette(palette)
    app.setStyleSheet(QSS)
