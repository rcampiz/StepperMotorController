"""
Memory panel: the LCD Memory page on the desktop. How full the flash and the RAM are, where the
RAM goes, and how deep every stack has ever been, at a glance.

Two VU-meter gauges (flash, static RAM): segments green below 70 %, yellow to 90 %, red beyond,
lit up to the fill, with the percentage large and used / free under them. A split bar shows the
RAM as task stacks, other globals and free. The stack table has the main stack (boot and
interrupts) and each task: its peak use against its size, the same colours.

Data: smc_core.views.memory of a DIAG:MEM? reply. While the panel is visible it asks for one
every two seconds through `refresh_requested` (MainWindow sends the request); it never builds a
command itself. Pure QPainter in theme colours read at paint time, so a dark/light switch takes
effect on the next repaint.
"""

from typing import Any, Dict, List, Optional

from PySide6.QtCore import QPointF, QRectF, Qt, QTimer, Signal
from PySide6.QtGui import QBrush, QColor, QFont, QPainter, QPen
from PySide6.QtWidgets import QHBoxLayout, QLabel, QPushButton, QSizePolicy, QVBoxLayout, QWidget

from smc_core import views as V
from smc_gui import theme

POLL_MS = 2000
KB = 1024.0


def _level_colour(level: str) -> str:
    return {"ok": theme.GREEN, "tight": theme.YELLOW, "full": theme.RED}.get(level, theme.DIM)


def _mix(a: str, b: str, t: float) -> QColor:
    """a blended toward b by t (0..1)."""
    ca, cb = QColor(a), QColor(b)
    return QColor(int(ca.red() + (cb.red() - ca.red()) * t), int(ca.green() + (cb.green() - ca.green()) * t),
                  int(ca.blue() + (cb.blue() - ca.blue()) * t))


def _kb(n: int) -> str:
    return "%.1f KB" % (n / KB)


def _short(n: int) -> str:
    return "%d B" % n if n < 1000 else "%.1f KB" % (n / KB)


def _corners(p: QPainter, r: QRectF, length: float, colour: str) -> None:
    """HUD bracket corners round a box, like the LCD's."""
    p.setPen(QPen(QColor(colour), 1.5))
    x0, y0, x1, y1 = r.left(), r.top(), r.right(), r.bottom()
    for (x, y, dx, dy) in ((x0, y0, 1, 1), (x1, y0, -1, 1), (x0, y1, 1, -1), (x1, y1, -1, -1)):
        p.drawLine(QPointF(x, y), QPointF(x + dx * length, y))
        p.drawLine(QPointF(x, y), QPointF(x, y + dy * length))


def _segments(p: QPainter, r: QRectF, n: int, pct: float, gap: float = 3.0) -> None:
    """A VU bar: n segments, each coloured by the band its middle falls in, lit up to pct."""
    w = (r.width() - gap * (n - 1)) / n
    lit = int(round(pct * n / 100.0))
    if pct > 0 and lit == 0:
        lit = 1
    p.setPen(Qt.NoPen)
    for i in range(n):
        zone = _level_colour(V.mem_level((i + 0.5) * 100.0 / n))
        x = r.left() + i * (w + gap)
        seg = QRectF(x, r.top(), w, r.height())
        if i < min(lit, n):
            p.setBrush(QBrush(QColor(zone)))
            p.drawRect(seg)
            p.setBrush(QBrush(_mix(zone, "#ffffff", 0.45)))       # the neon tube's bright edge
            p.drawRect(QRectF(x, r.top(), w, max(2.0, r.height() * 0.12)))
        else:
            p.setBrush(QBrush(_mix(theme.BASE, zone, 0.18)))
            p.drawRect(seg)


class MemoryGauge(QWidget):
    """One region (flash or RAM) as a bracketed VU gauge with its percentage."""

    SEGMENTS = 25

    def __init__(self, title: str, suffix: str = "", parent=None):
        super().__init__(parent)
        self.title = title
        self.suffix = suffix
        self.region: Optional[Dict[str, Any]] = None
        self.setMinimumSize(320, 132)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)

    def set_region(self, region: Optional[Dict[str, Any]]) -> None:
        self.region = region
        self.update()

    def paintEvent(self, event):  # noqa: N802
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        r = QRectF(self.rect()).adjusted(4, 4, -4, -4)
        p.fillRect(r, QColor(theme.BG))
        reg = self.region
        level = reg["level"] if reg else ""
        colour = _level_colour(level)
        # head: a magenta tick, the name, the size; the percentage large on the right
        p.setPen(Qt.NoPen)
        p.setBrush(QBrush(QColor(theme.MAGENTA)))
        p.drawRect(QRectF(r.left() + 4, r.top() + 8, 4, 22))
        p.setPen(QPen(QColor(theme.CYAN)))
        p.setFont(QFont("Consolas", 15, QFont.Bold))
        p.drawText(QRectF(r.left() + 16, r.top() + 4, 200, 30), Qt.AlignLeft | Qt.AlignVCenter, self.title)
        title_w = p.fontMetrics().horizontalAdvance(self.title)
        p.setFont(QFont("Consolas", 10))
        p.setPen(QPen(QColor(theme.VIOLET)))
        size = ("of %d KB%s" % (reg["total"] // 1024, self.suffix)) if reg else ""
        p.drawText(QRectF(r.left() + 24 + title_w, r.top() + 4, 240, 30), Qt.AlignLeft | Qt.AlignVCenter, size)
        p.setFont(QFont("Consolas", 26, QFont.Bold))
        p.setPen(QPen(QColor(colour)))
        p.drawText(QRectF(r.right() - 160, r.top(), 152, 40), Qt.AlignRight | Qt.AlignVCenter,
                   ("%d%%" % int(reg["pct"] + 0.5)) if reg else "--")
        # the bar in its brackets
        box = QRectF(r.left() + 2, r.top() + 46, r.width() - 4, 40)
        _corners(p, box, 9, theme.GRID_MAJ)
        _segments(p, box.adjusted(6, 6, -6, -6), self.SEGMENTS, reg["pct"] if reg else 0.0)
        # used, left; free, right in the band's colour
        p.setFont(QFont("Consolas", 11))
        p.setPen(QPen(QColor(theme.TEXT)))
        p.drawText(QRectF(r.left() + 6, r.top() + 92, 260, 24), Qt.AlignLeft | Qt.AlignVCenter,
                   ("%s used" % _kb(reg["used"])) if reg else "no data")
        if reg:
            p.setPen(QPen(QColor(colour)))
            p.drawText(QRectF(r.right() - 266, r.top() + 92, 260, 24), Qt.AlignRight | Qt.AlignVCenter,
                       "%s free" % _kb(reg["free"]))
        p.end()


class RamSplit(QWidget):
    """Where the static RAM goes: task stacks, other globals, free (hatched)."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.split: Optional[Dict[str, int]] = None
        self.pool: Optional[Dict[str, Any]] = None
        self.setMinimumSize(320, 84)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)

    def set_split(self, split: Optional[Dict[str, int]], pool: Optional[Dict[str, Any]]) -> None:
        self.split, self.pool = split, pool
        self.update()

    def paintEvent(self, event):  # noqa: N802
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        r = QRectF(self.rect()).adjusted(4, 4, -4, -4)
        p.fillRect(r, QColor(theme.BG))
        p.setFont(QFont("Consolas", 10, QFont.Bold))
        p.setPen(QPen(QColor(theme.VIOLET)))
        p.drawText(QRectF(r.left() + 4, r.top(), 300, 18), Qt.AlignLeft | Qt.AlignVCenter, "RAM SPLIT")
        box = QRectF(r.left() + 2, r.top() + 22, r.width() - 4, 30)
        _corners(p, box, 8, theme.GRID_MAJ)
        bar = box.adjusted(5, 5, -5, -5)
        s = self.split
        total = (s["stacks"] + s["globals"] + s["free"]) if s else 0
        if total > 0:
            x = bar.left()
            p.setPen(Qt.NoPen)
            for value, colour in ((s["stacks"], theme.MAGENTA), (s["globals"], theme.CYAN)):
                w = bar.width() * value / total
                p.setBrush(QBrush(QColor(colour)))
                p.drawRect(QRectF(x, bar.top(), max(w - 2, 0), bar.height()))
                p.setBrush(QBrush(_mix(colour, "#ffffff", 0.45)))
                p.drawRect(QRectF(x, bar.top(), max(w - 2, 0), 2))
                x += w
            p.setBrush(QBrush(QColor(theme.GRID_MAJ), Qt.BDiagPattern))
            p.drawRect(QRectF(x, bar.top(), bar.right() - x, bar.height()))
        # the key under it
        p.setFont(QFont("Consolas", 10))
        pool = self.pool or {}
        items = [(theme.MAGENTA, "task stacks %s" % _kb(s["stacks"]) if s else "task stacks",
                  (" (%s handed out)" % _kb(pool["used"])) if pool else ""),
                 (theme.CYAN, "other globals %s" % _kb(s["globals"]) if s else "other globals", ""),
                 (theme.GRID_MAJ, "free %s" % _kb(s["free"]) if s else "free", "")]
        x = r.left() + 6
        for colour, text, extra in items:
            p.setPen(Qt.NoPen)
            p.setBrush(QBrush(QColor(colour)))
            p.drawRect(QRectF(x, r.top() + 62, 10, 10))
            p.setPen(QPen(QColor(theme.TEXT)))
            label = text + extra
            p.drawText(QRectF(x + 15, r.top() + 56, 400, 22), Qt.AlignLeft | Qt.AlignVCenter, label)
            x += 15 + p.fontMetrics().horizontalAdvance(label) + 22
        p.end()


class StackPeaks(QWidget):
    """The main stack and every task: deepest use since boot against the stack's size."""

    ROW_H = 28
    SEGMENTS = 20

    def __init__(self, parent=None):
        super().__init__(parent)
        self.rows: List[Dict[str, Any]] = []
        self.setMinimumWidth(320)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self._fit()

    def _fit(self) -> None:
        self.setFixedHeight(30 + self.ROW_H * max(len(self.rows), 1) + 6)

    def set_rows(self, rows: List[Dict[str, Any]]) -> None:
        self.rows = rows
        self._fit()
        self.update()

    def paintEvent(self, event):  # noqa: N802
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        r = QRectF(self.rect()).adjusted(4, 4, -4, -4)
        p.fillRect(r, QColor(theme.BG))
        p.setFont(QFont("Consolas", 10, QFont.Bold))
        p.setPen(QPen(QColor(theme.VIOLET)))
        p.drawText(QRectF(r.left() + 4, r.top(), 300, 18), Qt.AlignLeft | Qt.AlignVCenter, "STACK PEAKS")
        p.setFont(QFont("Consolas", 9))
        p.setPen(QPen(QColor(theme.MUTED)))
        p.drawText(QRectF(r.right() - 420, r.top(), 416, 18), Qt.AlignRight | Qt.AlignVCenter,
                   "deepest use since boot / stack size")
        p.setPen(QPen(QColor(theme.GRID_MAJ), 1))
        p.drawLine(QPointF(r.left() + 110, r.top() + 9), QPointF(r.right() - 270, r.top() + 9))
        if not self.rows:
            p.setPen(QPen(QColor(theme.MUTED)))
            p.drawText(QRectF(r.left() + 6, r.top() + 24, 400, self.ROW_H), Qt.AlignLeft | Qt.AlignVCenter, "no data")
            p.end()
            return
        name_w, pct_w, detail_w = 150.0, 64.0, 190.0
        bar_l = r.left() + 12 + name_w
        bar_r = r.right() - pct_w - detail_w - 12
        for i, row in enumerate(self.rows):
            y = r.top() + 24 + i * self.ROW_H
            colour = _level_colour(row["level"])
            p.setPen(Qt.NoPen)
            p.setBrush(QBrush(QColor(colour)))
            p.drawRect(QRectF(r.left() + 2, y + 5, 3, self.ROW_H - 10))
            p.setFont(QFont("Consolas", 11, QFont.Bold))
            p.setPen(QPen(QColor(theme.TEXT)))
            p.drawText(QRectF(r.left() + 12, y, name_w, self.ROW_H), Qt.AlignLeft | Qt.AlignVCenter, row["name"])
            if bar_r - bar_l > 40:
                _segments(p, QRectF(bar_l, y + 8, bar_r - bar_l, self.ROW_H - 16), self.SEGMENTS, row["pct"], gap=2.0)
            p.setPen(QPen(QColor(colour)))
            p.drawText(QRectF(bar_r + 4, y, pct_w, self.ROW_H), Qt.AlignRight | Qt.AlignVCenter,
                       "%d%%" % int(row["pct"] + 0.5))
            p.setFont(QFont("Consolas", 10))
            p.setPen(QPen(QColor(theme.TEXT)))
            p.drawText(QRectF(r.right() - detail_w, y, detail_w - 4, self.ROW_H), Qt.AlignRight | Qt.AlignVCenter,
                       "%s / %s" % (_short(row["used"]), _short(row["size"])))
        p.end()


class MemoryPanel(QWidget):
    """The Diagnostics > Memory page. Connect `refresh_requested` to a DIAG:MEM? request and
    feed the parsed reply to update_memory(); show_error() when the board cannot answer."""

    refresh_requested = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.view: Dict[str, Any] = {}
        self._timer = QTimer(self)
        self._timer.setInterval(POLL_MS)
        self._timer.timeout.connect(self.refresh_requested.emit)

        self._head = QLabel("MEMORY")
        self._status = QLabel("waiting for the board")
        self._refresh_btn = QPushButton("Refresh")
        self._refresh_btn.clicked.connect(self.refresh_requested.emit)
        head = QHBoxLayout()
        head.addWidget(self._head)
        head.addWidget(self._status, 1)
        head.addWidget(self._refresh_btn)

        self.flash = MemoryGauge("FLASH")
        self.ram = MemoryGauge("RAM", " static")
        self.split = RamSplit()
        self.stacks = StackPeaks()
        gauges = QHBoxLayout()
        gauges.addWidget(self.flash, 1)
        gauges.addWidget(self.ram, 1)

        self._key = QLabel()
        self._key.setTextFormat(Qt.RichText)
        self._notes = QLabel(
            "Flash: code, constants and the initial values of variables.  RAM (static): every "
            "global and the task stacks, fixed at build time; there is no heap.  ISR: the main "
            "stack, used at boot and by interrupts.  A stack's peak is the most it has ever used "
            "since boot (FreeRTOS high-water mark): near 100 % it is about to overflow.")
        self._notes.setWordWrap(True)

        v = QVBoxLayout(self)
        v.setContentsMargins(8, 8, 8, 8)
        v.setSpacing(6)
        v.addLayout(head)
        v.addLayout(gauges)
        v.addWidget(self.split)
        v.addWidget(self.stacks)
        v.addWidget(self._key)
        v.addWidget(self._notes)
        v.addStretch()
        self.restyle()

    # --- polling: only while the page is on screen --------------------------------------
    @property
    def polling(self) -> bool:
        return self._timer.isActive()

    def showEvent(self, event):  # noqa: N802
        super().showEvent(event)
        if not self._timer.isActive():
            self._timer.start()
            self.refresh_requested.emit()

    def hideEvent(self, event):  # noqa: N802
        super().hideEvent(event)
        self._timer.stop()

    # --- data ----------------------------------------------------------------------------
    def update_memory(self, view: Dict[str, Any]) -> None:
        if "flash" not in view and "ram" not in view:
            self.show_error("no memory figures in the reply")
            return
        self.view = view
        self.flash.set_region(view.get("flash"))
        self.ram.set_region(view.get("ram"))
        self.split.set_split(view.get("split"), view.get("stack_pool"))
        rows: List[Dict[str, Any]] = []
        msp = view.get("msp")
        if msp:
            rows.append({"name": "ISR (main)", "used": msp["used"], "size": msp["total"], "pct": msp["pct"],
                         "level": msp["level"]})
        rows.extend({"name": t["name"], "used": t["used"], "size": t["size"], "pct": t["pct"],
                     "level": t["level"]} for t in view.get("tasks", []))
        self.stacks.set_rows(rows)
        worst = max([view[k]["pct"] for k in ("flash", "ram") if k in view] + [r["pct"] for r in rows])
        self._status.setText("updated every %d s  -  worst: %d%%" % (POLL_MS // 1000, int(worst + 0.5)))
        self._status.setStyleSheet("color: %s;" % theme.MUTED)

    def show_error(self, message: str) -> None:
        self._status.setText("memory statistics unavailable: %s" % message)
        self._status.setStyleSheet("color: %s;" % theme.ORANGE)

    def clear(self) -> None:
        self.view = {}
        self.flash.set_region(None)
        self.ram.set_region(None)
        self.split.set_split(None, None)
        self.stacks.set_rows([])
        self._status.setText("waiting for the board")
        self._status.setStyleSheet("color: %s;" % theme.MUTED)

    def restyle(self) -> None:
        """After a theme change: the labels take the new palette; the painted parts repaint."""
        self._head.setStyleSheet("color: %s; font-weight: bold; font-size: 14px; letter-spacing: 2px;" % theme.CYAN)
        self._status.setStyleSheet("color: %s;" % theme.MUTED)
        self._notes.setStyleSheet("color: %s; font-size: 11px;" % theme.MUTED)
        self._key.setText(
            '<span style="color:%s">&#9632; below 70 %%: fine</span> &nbsp;&nbsp; '
            '<span style="color:%s">&#9632; 70 - 90 %%: tight</span> &nbsp;&nbsp; '
            '<span style="color:%s">&#9632; above 90 %%: full</span>' % (theme.GREEN, theme.YELLOW, theme.RED))
        for w in (self.flash, self.ram, self.split, self.stacks):
            w.update()
