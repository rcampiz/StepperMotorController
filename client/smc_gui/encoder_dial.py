"""
Encoder dial: the shaft angle as a needle on a dial, the index mark, revolutions in the
centre. Fed from telemetry (count, counts per revolution) and from the encoder status
(index seen). Pure QPainter, no pyqtgraph; colours from the theme at paint time, so a
dark/light switch takes effect on the next repaint.
"""

import math

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QBrush, QColor, QFont, QPainter, QPen
from PySide6.QtWidgets import QWidget

from smc_gui import theme


class EncoderDial(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._count = 0
        self._cpr = 4000
        self._velocity = 0
        self._index_seen = False
        self._index_count = None          # count at the last index pulse, if known
        self.setMinimumSize(200, 200)

    def set_cpr(self, cpr: int) -> None:
        if cpr:
            self._cpr = int(cpr)
            self.update()

    def update_telemetry(self, data: dict) -> None:
        enc = data.get("encoder", {}) if isinstance(data.get("encoder"), dict) else {}
        if "count" in enc:
            self._count = int(enc["count"])
        if "velocity" in enc:
            self._velocity = int(enc["velocity"])
        if "index_seen" in enc:
            self._index_seen = bool(enc["index_seen"])
        self.update()

    @property
    def angle_deg(self) -> float:
        return (self._count % self._cpr) * 360.0 / self._cpr

    def paintEvent(self, event):  # noqa: N802
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        side = min(self.width(), self.height())
        cx, cy = self.width() / 2.0, self.height() / 2.0
        r = side / 2.0 - 10
        p.setPen(QPen(QColor(theme.GRID_MAJ), 2))
        p.setBrush(QBrush(QColor(theme.BASE)))
        p.drawEllipse(QPointF(cx, cy), r, r)
        # ticks: a bright one every 90 degrees, faint ones every 10
        for deg in range(0, 360, 10):
            major = deg % 90 == 0
            a = math.radians(deg - 90)
            inner = r - (14 if major else 7)
            p.setPen(QPen(QColor(theme.CYAN if major else theme.GRID_MAJ), 2 if major else 1))
            p.drawLine(QPointF(cx + inner * math.cos(a), cy + inner * math.sin(a)),
                       QPointF(cx + r * math.cos(a), cy + r * math.sin(a)))
        # index mark at angle 0 (top): filled when the index pulse has been seen
        p.setPen(QPen(QColor(theme.YELLOW), 1))
        p.setBrush(QBrush(QColor(theme.YELLOW)) if self._index_seen else Qt.NoBrush)
        p.drawEllipse(QPointF(cx, cy - r - 6), 4, 4)
        # needle
        a = math.radians(self.angle_deg - 90)
        p.setPen(QPen(QColor(theme.MAGENTA), 3))
        p.drawLine(QPointF(cx, cy), QPointF(cx + (r - 18) * math.cos(a), cy + (r - 18) * math.sin(a)))
        p.setPen(Qt.NoPen)
        p.setBrush(QBrush(QColor(theme.MAGENTA)))
        p.drawEllipse(QPointF(cx, cy), 5, 5)
        # text: revolutions and angle, velocity in rpm
        p.setPen(QPen(QColor(theme.TEXT)))
        p.setFont(QFont("Consolas", 11, QFont.Bold))
        revs = self._count / float(self._cpr)
        p.drawText(QRectF(cx - r, cy + r * 0.25, 2 * r, 22), Qt.AlignCenter, "%.3f rev" % revs)
        p.setFont(QFont("Consolas", 9))
        p.setPen(QPen(QColor(theme.MUTED)))
        p.drawText(QRectF(cx - r, cy + r * 0.25 + 22, 2 * r, 18), Qt.AlignCenter,
                   "%.1f°   %d cnt   %.1f rpm" % (self.angle_deg, self._count, self._velocity * 60.0 / self._cpr))
        p.end()
