"""
Motor profile picker: the profiles that ship with the client and the user's own, applied to
the board as DRV:CFG commands (then saved to flash if asked), and the board's current
configuration saved as a new profile file.
"""

from typing import Optional

from PySide6.QtCore import Signal
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QGroupBox, QHBoxLayout, QInputDialog, QLabel, QPushButton, QVBoxLayout,
)

from smc_core import motors
from smc_gui import theme


class MotorProfilePanel(QGroupBox):
    apply_requested = Signal(list, object)      # [Request], Profile
    export_requested = Signal(str, str)         # name, title: the window exports the board's config

    def __init__(self, parent=None):
        super().__init__("Motor profile", parent)
        self._profiles = []
        self._physical = True           # protocol 3 board: physical commands (set_physical)
        v = QVBoxLayout(self)
        row = QHBoxLayout()
        self._combo = QComboBox()
        self._combo.setMinimumWidth(320)
        self._combo.currentIndexChanged.connect(self._on_pick)
        row.addWidget(self._combo, 1)
        self._apply_btn = QPushButton("Apply profile")
        self._apply_btn.setToolTip("Send this profile's drive settings to the board (DRV:CFG..., APPLY)")
        self._apply_btn.clicked.connect(self._on_apply)
        row.addWidget(self._apply_btn)
        self._save_cb = QCheckBox("and save to flash")
        self._save_cb.setChecked(False)
        row.addWidget(self._save_cb)
        self._export_btn = QPushButton("Save current as...")
        self._export_btn.setToolTip("Keep the board's present configuration as a profile file in ~/.smc/motors")
        self._export_btn.clicked.connect(self._on_export)
        row.addWidget(self._export_btn)
        self._reload_btn = QPushButton("Reload")
        self._reload_btn.clicked.connect(self.reload)
        row.addWidget(self._reload_btn)
        v.addLayout(row)
        self._info = QLabel("")
        self._info.setWordWrap(True)
        self._info.setStyleSheet("color: %s; font-size: 11px;" % theme.MUTED)
        v.addWidget(self._info)
        self.reload()

    def reload(self) -> None:
        current = self.current_profile().name if self.current_profile() else None
        self._profiles = motors.list_profiles()
        self._combo.blockSignals(True)
        self._combo.clear()
        for p in self._profiles:
            self._combo.addItem(p.label, p.name)
        self._combo.blockSignals(False)
        if current:
            self.select(current)
        self._on_pick()

    def select(self, name: str) -> None:
        for i, p in enumerate(self._profiles):
            if p.name == name:
                self._combo.setCurrentIndex(i)
                return

    def current_profile(self) -> Optional[motors.Profile]:
        i = self._combo.currentIndex()
        return self._profiles[i] if 0 <= i < len(self._profiles) else None

    @property
    def save_to_flash(self) -> bool:
        return self._save_cb.isChecked()

    def set_physical(self, physical: bool) -> None:
        """Whether the board takes the physical commands (protocol 3) or the chip codes."""
        self._physical = bool(physical)

    def _on_pick(self, *_):
        p = self.current_profile()
        if p is None:
            self._info.setText("no profiles found")
            return
        bits = ["%s A rated" % p.rated_current_a if p.rated_current_a else None,
                "%d steps/rev" % p.full_steps_per_rev,
                "%d cpr encoder" % p.encoder_cpr if p.encoder_cpr else "no encoder",
                "%s mode" % p.drive_mode]
        if p.tval_a:
            bits.append("current hold %.2f run %.2f acc %.2f dec %.2f A" % (
                p.tval_a.get("hold", 0), p.tval_a.get("run", 0), p.tval_a.get("acc", 0), p.tval_a.get("dec", 0)))
        if p.resonance_bands_rpm:
            bits.append("bands " + ", ".join("%g-%g RPM" % b for b in p.resonance_bands_rpm))
        text = "  |  ".join(b for b in bits if b)
        if p.notes:
            text += "\n" + p.notes
        self._info.setText(text)

    def _on_apply(self):
        p = self.current_profile()
        if p is None:
            return
        reqs = p.requests(self._physical)
        if self.save_to_flash:
            reqs.append(motors.C.cfg_save())
        self.apply_requested.emit(reqs, p)

    def _on_export(self):
        name, ok = QInputDialog.getText(self, "Save current configuration as a profile",
                                        "File name (letters, digits, dashes):", text="my-motor")
        if not ok or not name.strip():
            return
        name = "".join(ch for ch in name.strip() if ch.isalnum() or ch in "-_.")
        title, ok = QInputDialog.getText(self, "Profile title", "Title shown in the picker:", text=name)
        if not ok:
            return
        self.export_requested.emit(name, title.strip() or name)
