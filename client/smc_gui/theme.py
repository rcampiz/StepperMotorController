"""
The GUI's look, in one place: the palette (the same cyberpunk set the LCD uses, see
server/layers/L3_services/ui/theme.hpp) and the application stylesheet.

Panels take state colours (running green, fault red, hold cyan...) from here so the two
screens of the product read the same.
"""

# --- palette -------------------------------------------------------------------------
BG = "#080810"          # the page: near-black with a blue cast
BASE = "#0c0e1a"        # inputs, plots
PANEL = "#101830"       # raised boxes, tab bar, buttons
SCAN = "#0a1020"        # alternate rows
GRID = "#203858"        # borders, minor grid
GRID_MAJ = "#386488"    # rules, major grid, hover borders
CYAN = "#00f0ff"        # primary neon: titles, labels, measured values
MAGENTA = "#ff18d8"     # secondary neon: selection, setpoints, needles
VIOLET = "#9846ff"
YELLOW = "#ffe400"      # hazard: warnings, holding coils
ORANGE = "#ff8000"
GREEN = "#28ff40"       # running, OK
RED = "#ff2030"         # faults
TEXT = "#d8e4f0"
DIM = "#485068"
MUTED = "#8090a8"       # secondary text that must still be readable

# Trace colours for plots, in the order panels assign them
TRACES = [CYAN, MAGENTA, YELLOW, GREEN, ORANGE, VIOLET, "#ff80c0", "#80ffe0"]

# State chips (background, foreground)
STATE_COLOURS = {
    "IDLE": ("#0a2a30", CYAN),
    "ARMED": ("#2a2800", YELLOW),
    "RUNNING": ("#082a10", GREEN),
    "FAULT": ("#2a0810", RED),
    "ESTOP": ("#3a0810", "#ff6080"),
}

QSS = f"""
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


def apply_theme(app) -> None:
    """Palette for the widgets the stylesheet does not reach, then the stylesheet."""
    from PySide6.QtGui import QColor, QPalette
    palette = QPalette()
    for role, hexcol in ((QPalette.Window, BG), (QPalette.WindowText, TEXT), (QPalette.Base, BASE),
                         (QPalette.AlternateBase, SCAN), (QPalette.ToolTipBase, PANEL),
                         (QPalette.ToolTipText, TEXT), (QPalette.Text, TEXT), (QPalette.Button, PANEL),
                         (QPalette.ButtonText, TEXT), (QPalette.BrightText, RED), (QPalette.Link, CYAN),
                         (QPalette.Highlight, MAGENTA), (QPalette.HighlightedText, BG)):
        palette.setColor(role, QColor(hexcol))
    for role in (QPalette.WindowText, QPalette.Text, QPalette.ButtonText):
        palette.setColor(QPalette.Disabled, role, QColor(DIM))
    app.setPalette(palette)
    app.setStyleSheet(QSS)
