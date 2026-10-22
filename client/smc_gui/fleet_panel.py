"""
Fleet page: four boards with the wheel roles FL / FR / RL / RR, driven as one base.

What it does:
  - opens the four boards from addresses (COM ports, tcp:// virtual Nucleos) with the
    expected serials, through smc_core.fleet (the same code the Pi coordinator runs);
  - drives a body twist (vx, vy, wz) with sliders, a STOP, the base E-STOP and re-arm;
  - runs a mission script the way the Pi would, step by step;
  - logs every line sent to every board, so the Pi's code can be checked against it;
  - can start four virtual Nucleos on this PC when sim/ has been built;
  - test mode: whatever controllers are connected (1 to 4, any wheel slots) plus virtual
    Nucleos in the slots set to "virtual", driven as one base by the pad (a wired Xbox
    controller, e.g. the PowerA 1414135-02): each real motor turns as its wheel would.
"""

import json
import os
import socket
import subprocess
import threading
import time
from typing import Dict, List, Optional, Tuple

from PySide6.QtCore import QObject, QTimer, Qt, Signal, Slot
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QDoubleSpinBox, QFileDialog, QGridLayout, QGroupBox, QHBoxLayout,
    QLabel, QLineEdit, QPlainTextEdit, QPushButton, QSlider, QVBoxLayout, QWidget,
)

from smc_core import commands as C
from smc_core import kinematics as K
from smc_core.board import BoardConfig
from smc_core.fleet import Fleet, FleetConfig, FleetError, ROLES
from smc_core.transport import open_transport
from smc_core.transport.tee import TeeTransport
from smc_core.drive_map import BaseDriver
from smc_gui import theme
from smc_gui.controller_panel import ControllerPanel

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
VNUCLEO_EXE = os.path.join(ROOT, "build-vnucleo", "vnucleo.exe" if os.name == "nt" else "vnucleo")
_HEARTBEAT = C.heartbeat(0).name            # the log hides these unless asked
VIRTUAL = "virtual"                          # a wheel slot test mode fills with a virtual Nucleo


class FleetBridge(QObject):
    """The fleet on worker threads, reported to the GUI thread through signals."""
    opened = Signal(dict)                   # role -> serial
    open_failed = Signal(str)
    closed = Signal()
    fault = Signal(str, str)                # role, why (the base was stopped)
    done = Signal(str, object)              # tag, result or exception (estop, rearm, enable)
    tx = Signal(float, str, str)            # t, role, line sent

    def __init__(self, parent=None):
        super().__init__(parent)
        self.fleet: Optional[Fleet] = None

    def _factory(self, role: str):
        def make(address: str):
            return TeeTransport(open_transport(address),
                                lambda t, data, role=role: self.tx.emit(t, role, data.decode("ascii", "replace").rstrip("\r\n")))
        return make

    def open(self, config: FleetConfig) -> None:
        def run():
            try:
                # one factory per role, so the log knows which board a line went to
                roles = list(ROLES)
                fleet = Fleet.__new__(Fleet)
                factories = {r: self._factory(r) for r in roles}
                Fleet.__init__(fleet, config,
                               transport_factory=lambda address, _it=iter(roles): factories[next(_it)](address))
                fleet.subscribe("fault", lambda role, why: self.fault.emit(role, why))
                fleet.open(timeout=8.0)
            except Exception as e:                     # noqa: BLE001
                self.open_failed.emit(str(e))
                return
            self.fleet = fleet
            self.opened.emit({r: (b.identity.serial if b.identity else "?") for r, b in fleet.boards.items()})
        threading.Thread(target=run, name="fleet-open", daemon=True).start()

    def close(self) -> None:
        fleet, self.fleet = self.fleet, None
        if fleet is None:
            return
        def run():
            try:
                fleet.close(estop=False)
            finally:
                self.closed.emit()
        threading.Thread(target=run, name="fleet-close", daemon=True).start()

    def action(self, tag: str) -> None:
        fleet = self.fleet
        if fleet is None:
            return
        def run():
            try:
                if tag == "estop":
                    result = fleet.estop()
                elif tag == "rearm":
                    result = fleet.rearm()
                elif tag == "enable":
                    result = fleet.enable()
                else:
                    result = None
            except Exception as e:                     # noqa: BLE001
                result = e
            self.done.emit(tag, result)
        threading.Thread(target=run, name="fleet-" + tag, daemon=True).start()


def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class FleetPanel(QWidget):
    #: which of the fleet's boards the window's other tabs show (board, role), or none
    view_board = Signal(object, str)
    view_none = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._bridge = FleetBridge(self)
        self._bridge.opened.connect(self._on_opened)
        self._bridge.open_failed.connect(self._on_open_failed)
        self._bridge.closed.connect(self._on_closed)
        self._bridge.fault.connect(self._on_fault)
        self._bridge.done.connect(self._on_done)
        self._bridge.tx.connect(self._on_tx)
        self._virtual: List[subprocess.Popen] = []
        self._virtual_addrs: set = set()              # addresses of the virtual Nucleos this page started
        self._mission: List[dict] = []
        self._mission_step_end = 0.0
        self._log_lines: List[str] = []
        self.opened = False
        self.last_estop = None
        self.test_mode = False
        self._test_banner = ""
        #: Set by the main window: called with a port test mode is about to open, so a
        #: window link holding that port lets it go first
        self.release_port = None
        self._setup_ui()
        self._tick = QTimer(self)
        self._tick.setInterval(50)
        self._tick.timeout.connect(self._on_tick)
        self._tick.start()
        self._frames = 0

    # --- UI ----------------------------------------------------------------------------
    def _setup_ui(self):
        root = QVBoxLayout(self)
        root.setContentsMargins(6, 6, 6, 6)

        # Boards
        boards = QGroupBox("Boards")
        g = QGridLayout(boards)
        for c, h in enumerate(["Wheel", "Address", "Expected serial", "Serial", "State", "Speed", "Age",
                               "Avoider", "Run A", "Ripple"]):
            lbl = QLabel(h); lbl.setStyleSheet("color: %s; font-size: 10px;" % theme.MUTED)
            g.addWidget(lbl, 0, c)
        self._addr: Dict[str, QComboBox] = {}
        self._want: Dict[str, QLineEdit] = {}
        self._serial: Dict[str, QLabel] = {}
        self._state: Dict[str, QLabel] = {}
        self._speed: Dict[str, QLabel] = {}
        self._age: Dict[str, QLabel] = {}
        self._res_on: Dict[str, QCheckBox] = {}
        self._res_run: Dict[str, QLabel] = {}
        self._res_rip: Dict[str, QLabel] = {}
        self._res_status: Dict[str, dict] = {}          # CTRL:RES? replies (worker thread writes)
        for r, role in enumerate(ROLES, start=1):
            name = QLabel(role); name.setStyleSheet("color: %s; font-weight: bold;" % theme.CYAN)
            g.addWidget(name, r, 0)
            cb = QComboBox(); cb.setEditable(True); cb.setMinimumWidth(170)
            self._addr[role] = cb; g.addWidget(cb, r, 1)
            le = QLineEdit(); le.setPlaceholderText("any"); le.setMaximumWidth(110)
            self._want[role] = le; g.addWidget(le, r, 2)
            for col, store in ((3, self._serial), (4, self._state), (5, self._speed), (6, self._age),
                               (8, self._res_run), (9, self._res_rip)):
                lbl = QLabel("--"); lbl.setStyleSheet("font-family: Consolas; color: %s;" % theme.TEXT)
                store[role] = lbl; g.addWidget(lbl, r, col)
            ck = QCheckBox(); ck.setEnabled(False)
            ck.setToolTip("This wheel's resonance avoider (A/B switch, not saved); its state and run "
                          "current beside it; Ripple: the resonance as a number (counts rms, ~1 quiet)")
            ck.clicked.connect(lambda on, role=role: self._on_res_toggle(role, on))
            self._res_on[role] = ck; g.addWidget(ck, r, 7)
        btns = QHBoxLayout()
        self._open_btn = QPushButton("Open fleet"); self._open_btn.clicked.connect(self._on_open_clicked)
        self._close_btn = QPushButton("Close"); self._close_btn.clicked.connect(self._on_close_clicked); self._close_btn.setEnabled(False)
        self._ports_btn = QPushButton("Refresh ports"); self._ports_btn.clicked.connect(self.refresh_ports)
        self._virtual_btn = QPushButton("Virtual x4"); self._virtual_btn.setToolTip(
            "Start four virtual Nucleos on this PC (sim/ must be built) and fill the addresses")
        self._virtual_btn.clicked.connect(self._on_virtual_clicked)
        self._test_btn = QPushButton("Test mode")
        self._test_btn.setToolTip(
            "Test the base with the controllers you have: slots with a COM port keep their real board, "
            "slots set to 'virtual' get a virtual Nucleo; opens the base and arms the controller")
        self._test_btn.clicked.connect(self._on_test_clicked)
        self._load_btn = QPushButton("Load config..."); self._load_btn.clicked.connect(self._on_load)
        self._save_btn = QPushButton("Save config..."); self._save_btn.clicked.connect(self._on_save)
        for b in (self._open_btn, self._close_btn, self._test_btn, self._ports_btn, self._virtual_btn,
                  self._load_btn, self._save_btn):
            btns.addWidget(b)
        btns.addStretch()
        btns.addWidget(QLabel("Show in the tabs:"))
        self._view = QComboBox()
        self._view.addItems(list(ROLES) + ["none"])
        self._view.setToolTip("Which board the Dashboard, Encoder, Graph and the other tabs show while the "
                              "fleet is open (motion stays here)")
        self._view.currentTextChanged.connect(self._on_view_changed)
        btns.addWidget(self._view)
        self._status = QLabel("closed"); self._status.setStyleSheet("color: %s;" % theme.MUTED)
        btns.addWidget(self._status)
        g.addLayout(btns, 5, 0, 1, 10)
        root.addWidget(boards)

        row = QHBoxLayout()
        # Geometry
        geo = QGroupBox("Base geometry")
        gg = QGridLayout(geo)
        self._radius = self._spin(gg, 0, "Wheel radius", 0.01, 0.5, 0.048, " m", 3)
        self._lx = self._spin(gg, 1, "Half wheelbase", 0.01, 1.0, 0.15, " m", 3)
        self._ly = self._spin(gg, 2, "Half track", 0.01, 1.0, 0.17, " m", 3)
        self._max_sps = self._spin(gg, 3, "Max motor speed", 50, 15000, 1334, " st/s", 0)   # 400 RPM
        for sb in (self._radius, self._lx, self._ly, self._max_sps):   # the controller's Max RPM follows
            sb.valueChanged.connect(self._apply_max_rpm)
        self._inv: Dict[str, QCheckBox] = {}
        inv_row = QHBoxLayout()
        inv_row.addWidget(QLabel("Inverted:"))
        for role in ROLES:
            cb = QCheckBox(role); cb.setChecked(role in ("FR", "RR")); self._inv[role] = cb; inv_row.addWidget(cb)
        gg.addLayout(inv_row, 4, 0, 1, 2)
        row.addWidget(geo, 1)

        # Drive
        drive = QGroupBox("Drive the base")
        dg = QGridLayout(drive)
        self._vx = self._axis(dg, 0, "vx forward", 1.0, " m/s")
        self._vy = self._axis(dg, 1, "vy left", 1.0, " m/s")
        self._wz = self._axis(dg, 2, "wz turn", 3.0, " rad/s")
        self._drive_on = QCheckBox("Drive (send the twist while on)")
        self._drive_on.toggled.connect(self._on_drive_toggled)
        dg.addWidget(self._drive_on, 3, 0, 1, 3)
        self._measured = QLabel("measured: --"); self._measured.setStyleSheet("font-family: Consolas; color: %s;" % theme.CYAN)
        dg.addWidget(self._measured, 4, 0, 1, 3)
        db = QHBoxLayout()
        self._stop_btn = QPushButton("STOP"); self._stop_btn.setStyleSheet("background: %s; color: black; font-weight: bold;" % theme.YELLOW)
        self._stop_btn.clicked.connect(self.stop)
        self._estop_btn = QPushButton("E-STOP BASE"); self._estop_btn.setStyleSheet("background: %s; color: white; font-weight: bold;" % theme.RED)
        self._estop_btn.clicked.connect(lambda: self._bridge.action("estop"))
        self._rearm_btn = QPushButton("Re-arm"); self._rearm_btn.clicked.connect(lambda: self._bridge.action("rearm"))
        self._enable_btn = QPushButton("Enable all"); self._enable_btn.clicked.connect(lambda: self._bridge.action("enable"))
        for b in (self._stop_btn, self._estop_btn, self._rearm_btn, self._enable_btn):
            db.addWidget(b)
        dg.addLayout(db, 5, 0, 1, 3)
        row.addWidget(drive, 2)
        root.addLayout(row)

        # Mission + log
        bottom = QHBoxLayout()
        mission = QGroupBox("Mission (what the Pi would send): one step per line, JSON")
        mv = QVBoxLayout(mission)
        self._mission_edit = QPlainTextEdit()
        self._mission_edit.setPlaceholderText('{"vx": 0.2, "seconds": 2}\n{"vy": 0.2, "seconds": 1}\n{"wz": 1.0, "seconds": 1.5}\n{"stop": true}')
        self._mission_edit.setPlainText('{"vx": 0.2, "seconds": 2}\n{"vy": 0.2, "seconds": 1}\n{"wz": 1.0, "seconds": 1.5}\n{"stop": true}')
        mv.addWidget(self._mission_edit)
        mb = QHBoxLayout()
        self._run_btn = QPushButton("Run mission"); self._run_btn.clicked.connect(self.run_mission)
        self._abort_btn = QPushButton("Abort"); self._abort_btn.clicked.connect(self.abort_mission)
        self._mission_status = QLabel("idle"); self._mission_status.setStyleSheet("color: %s;" % theme.MUTED)
        mb.addWidget(self._run_btn); mb.addWidget(self._abort_btn); mb.addWidget(self._mission_status); mb.addStretch()
        mv.addLayout(mb)
        bottom.addWidget(mission, 1)

        log = QGroupBox("Lines sent to the boards")
        lv = QVBoxLayout(log)
        self._log = QPlainTextEdit(); self._log.setReadOnly(True); self._log.setMaximumBlockCount(2000)
        self._log.setStyleSheet("font-family: Consolas; font-size: 11px;")
        lv.addWidget(self._log)
        lb = QHBoxLayout()
        self._log_hb = QCheckBox("show heartbeats"); self._log_hb.setChecked(False)
        clear = QPushButton("Clear"); clear.clicked.connect(self._clear_log)
        save = QPushButton("Save log..."); save.clicked.connect(self._save_log)
        lb.addWidget(self._log_hb); lb.addWidget(clear); lb.addWidget(save); lb.addStretch()
        lv.addLayout(lb)
        bottom.addWidget(log, 1)
        root.addLayout(bottom, 1)

        # The pad drives the base: left stick vx/vy, right stick turn
        self.controller = ControllerPanel("base")
        self.controller.state_changed.connect(self._on_controller_state)
        self.controller.max_rpm_changed.connect(self._apply_max_rpm)
        root.addWidget(self.controller)
        self._base_driver: Optional[BaseDriver] = None

        self.refresh_ports()
        self._set_open_state(False)

    def _spin(self, grid, row, label, lo, hi, val, suffix, decimals):
        grid.addWidget(QLabel(label + ":"), row, 0)
        sb = QDoubleSpinBox(); sb.setRange(lo, hi); sb.setDecimals(decimals); sb.setValue(val); sb.setSuffix(suffix)
        grid.addWidget(sb, row, 1)
        return sb

    def _axis(self, grid, row, label, span, unit):
        grid.addWidget(QLabel(label + ":"), row, 0)
        sl = QSlider(Qt.Horizontal); sl.setRange(-1000, 1000); sl.setValue(0)
        grid.addWidget(sl, row, 1)
        val = QLabel("0.00" + unit); val.setMinimumWidth(80); val.setStyleSheet("font-family: Consolas;")
        grid.addWidget(val, row, 2)
        sl.valueChanged.connect(lambda v, val=val, span=span, unit=unit: val.setText("%.2f%s" % (v / 1000.0 * span, unit)))
        sl._span = span
        return sl

    # --- config ------------------------------------------------------------------------
    def refresh_ports(self):
        try:
            from smc_core.transport.serial import VcpTransport
            ports = [p["device"] for p in VcpTransport.list_all_ports()]
        except Exception:                                  # noqa: BLE001
            ports = []
        for role, cb in self._addr.items():
            current = cb.currentText()
            cb.clear()
            cb.addItems([VIRTUAL] + ports)            # "virtual": test mode fills the slot
            cb.setCurrentText(current if current else VIRTUAL)

    def set_addresses(self, addresses: Dict[str, str], serials: Optional[Dict[str, str]] = None):
        for role, addr in addresses.items():
            if role in self._addr:
                self._addr[role].setCurrentText(addr)
        for role, serial in (serials or {}).items():
            if role in self._want:
                self._want[role].setText(serial)

    def geometry(self) -> K.Geometry:
        return K.Geometry(wheel_radius_m=self._radius.value(), half_wheelbase_m=self._lx.value(),
                          half_track_m=self._ly.value(), max_motor_sps=self._max_sps.value(),
                          inverted={r: cb.isChecked() for r, cb in self._inv.items()})

    def config(self) -> FleetConfig:
        return FleetConfig(addresses={r: cb.currentText().strip() for r, cb in self._addr.items()},
                           serials={r: le.text().strip() for r, le in self._want.items() if le.text().strip()},
                           geometry=self.geometry(), board=BoardConfig(clear_fault_on_connect=True,
                                                                      fast_baud=1000000))

    def config_dict(self) -> dict:
        cfg = self.config()
        g = cfg.geometry
        return {"addresses": cfg.addresses, "serials": cfg.serials,
                "geometry": {"wheel_radius_m": g.wheel_radius_m, "half_wheelbase_m": g.half_wheelbase_m,
                             "half_track_m": g.half_track_m, "max_motor_sps": g.max_motor_sps,
                             "inverted": g.inverted}}

    def apply_config_dict(self, d: dict) -> None:
        self.set_addresses(d.get("addresses", {}), d.get("serials", {}))
        g = d.get("geometry", {})
        if "wheel_radius_m" in g: self._radius.setValue(float(g["wheel_radius_m"]))
        if "half_wheelbase_m" in g: self._lx.setValue(float(g["half_wheelbase_m"]))
        if "half_track_m" in g: self._ly.setValue(float(g["half_track_m"]))
        if "max_motor_sps" in g: self._max_sps.setValue(float(g["max_motor_sps"]))
        for role, inv in g.get("inverted", {}).items():
            if role in self._inv: self._inv[role].setChecked(bool(inv))

    def _on_load(self):
        path, _ = QFileDialog.getOpenFileName(self, "Load fleet config", "", "JSON (*.json)")
        if path:
            with open(path, encoding="utf-8") as fh:
                self.apply_config_dict(json.load(fh))

    def _on_save(self):
        path, _ = QFileDialog.getSaveFileName(self, "Save fleet config", "fleet.json", "JSON (*.json)")
        if path:
            with open(path, "w", encoding="utf-8") as fh:
                json.dump(self.config_dict(), fh, indent=2)

    # --- virtual boards ----------------------------------------------------------------
    def start_virtual(self, count: int = 4, roles: Optional[List[str]] = None) -> bool:
        """Virtual Nucleos for `roles` (default: the first `count` wheel slots), addresses filled."""
        roles = list(roles) if roles is not None else list(ROLES[:count])
        if not os.path.exists(VNUCLEO_EXE):
            self._status.setText("sim/ not built (cmake --preset vnucleo)")
            return False
        self.stop_virtual()
        addresses, serials = {}, {}
        for role in roles:
            port = _free_port()
            serial = "VN%06d" % (ROLES.index(role) + 1)
            p = subprocess.Popen([VNUCLEO_EXE, "--port", str(port), "--serial", serial],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            self._virtual.append(p)
            addresses[role] = "tcp://127.0.0.1:%d" % port
            self._virtual_addrs.add(addresses[role])
            serials[role] = serial
            end = time.monotonic() + 5.0
            while time.monotonic() < end:
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                        break
                except OSError:
                    time.sleep(0.05)
        time.sleep(0.1)
        self.set_addresses(addresses, serials)
        self._status.setText("%d virtual Nucleo%s running" % (len(roles), "" if len(roles) == 1 else "s"))
        return True

    def stop_virtual(self):
        for p in self._virtual:
            if p.poll() is None:
                p.kill()
        self._virtual = []

    def _on_virtual_clicked(self):
        self.start_virtual(4)

    # --- open / close ------------------------------------------------------------------
    def _set_open_state(self, opened: bool):
        self.opened = opened
        self._open_btn.setEnabled(not opened)
        self._test_btn.setEnabled(not opened)
        self._close_btn.setEnabled(opened)
        for w in (self._stop_btn, self._estop_btn, self._rearm_btn, self._enable_btn, self._run_btn, self._drive_on):
            w.setEnabled(opened)
        for cb in self._addr.values():
            cb.setEnabled(not opened)

    # --- test mode: the controllers you have, virtual Nucleos for the rest -------------------
    def start_test_mode(self) -> bool:
        """Open the base with 1 to 4 real controllers: the slots with an address keep their
        board, the slots set to "virtual" get a virtual Nucleo. The pad drives the whole base,
        so each real motor turns as its wheel would; Close ends test mode."""
        if self.opened:
            return False
        real = {r: cb.currentText().strip() for r, cb in self._addr.items()
                if not self._is_virtual(cb.currentText())}
        ports = [a.upper() for a in real.values()]
        dup = sorted({a for a in ports if ports.count(a) > 1})
        if dup:
            self._status.setText("one board per slot: %s is in more than one" % ", ".join(dup))
            return False
        for address in real.values():
            if self.release_port is not None:
                self.release_port(address)        # the main window's link lets the port go
        virtual = [r for r in ROLES if r not in real]
        if virtual and not self.start_virtual(roles=virtual):
            return False
        self.test_mode = True
        self._test_banner = "%d real (%s), %d virtual" % (len(real), ", ".join(sorted(real)) or "none", len(virtual))
        self._status.setText("TEST MODE: " + self._test_banner)
        self._status.setStyleSheet("color: %s; font-weight: bold;" % theme.YELLOW)
        self._test_btn.setEnabled(False)
        self._on_open_clicked()
        return True

    def _is_virtual(self, address: str) -> bool:
        """A slot test mode fills: "virtual", empty, or a virtual Nucleo this page started before."""
        a = address.strip()
        return a.lower() in ("", VIRTUAL) or a in self._virtual_addrs

    def _on_test_clicked(self):
        self.start_test_mode()

    def _end_test_mode(self):
        if not self.test_mode:
            return
        self.test_mode = False
        self.controller.set_driving(False)
        self.stop_virtual()
        self._status.setStyleSheet("color: %s;" % theme.MUTED)

    def _on_open_clicked(self):
        cfg = self.config()
        missing = [r for r, a in cfg.addresses.items() if not a or a.lower() == VIRTUAL]
        if missing:
            self._status.setText("no board for %s (Test mode fills 'virtual' slots)" % ", ".join(missing))
            self._test_btn.setEnabled(True)
            self._end_test_mode()
            return
        self._status.setText("opening...")
        self._open_btn.setEnabled(False)
        self._bridge.open(cfg)

    def open_fleet(self):
        self._on_open_clicked()

    def _on_close_clicked(self):
        self._drive_on.setChecked(False)
        self.abort_mission()
        self.view_none.emit()                    # the tabs let go before the boards close
        self._bridge.close()

    # --- which board the window's other tabs show ------------------------------------------
    def view_role(self) -> Optional[str]:
        role = self._view.currentText()
        return role if role in ROLES else None

    def set_view_role(self, role: Optional[str]) -> None:
        self._view.setCurrentText(role if role in ROLES else "none")

    def _show_in_tabs(self) -> None:
        fleet = self._bridge.fleet
        role = self.view_role()
        if fleet is None:
            return
        if role is None or role not in fleet.boards:
            self.view_none.emit()
        else:
            self.view_board.emit(fleet.boards[role], role)

    def _on_view_changed(self, _text: str) -> None:
        self._show_in_tabs()

    def close_fleet(self):
        self._on_close_clicked()

    def _on_controller_state(self, state):
        if self._base_driver is None:
            return
        self._base_driver.tick(state)
        if state is not None and self._base_driver.last is not None:
            self.set_twist(self._base_driver.last)

    def _apply_max_rpm(self, *_):
        """The controller's Max RPM, under the Base geometry's motor-speed ceiling: full stick and
        full trigger run the motors there, and the fleet caps every motor at it (live)."""
        if self._base_driver is None:
            return
        g = self.geometry()
        ceiling = g.max_motor_sps * 60.0 / g.full_steps_per_rev
        self._base_driver.set_max_rpm(min(self.controller.max_rpm, ceiling), g)

    @Slot(dict)
    def _on_opened(self, serials: dict):
        self._base_driver = BaseDriver(self._bridge.fleet)
        self._base_driver.on_action = self._bridge.action
        self._apply_max_rpm()
        for role, serial in serials.items():
            self._serial[role].setText(serial)
        if self.test_mode:
            # the pad drives the base from here: sticks move it, B stops, Y enables, START E-STOPs
            self.controller.set_driving(True)
            self._status.setText("TEST MODE (open): %s; the controller drives the base" % self._test_banner)
            self._status.setStyleSheet("color: %s; font-weight: bold;" % theme.YELLOW)
        else:
            self._status.setText("open: four boards")
        self._set_open_state(True)
        self._show_in_tabs()                     # the other tabs show the chosen board (FL at first)

    @Slot(str)
    def _on_open_failed(self, why: str):
        self._end_test_mode()                          # the virtual boards go with it
        self._status.setText("open failed: " + why)
        self._set_open_state(False)

    @Slot()
    def _on_closed(self):
        self._base_driver = None
        self._end_test_mode()
        self._status.setText("closed")
        for role in ROLES:
            for store in (self._serial, self._state, self._speed, self._age):
                store[role].setText("--")
        self._set_open_state(False)

    @Slot(str, str)
    def _on_fault(self, role: str, why: str):
        self._status.setText("BASE STOPPED: %s %s (re-arm to continue)" % (role, why))
        self._status.setStyleSheet("color: %s; font-weight: bold;" % theme.RED)
        self._drive_on.setChecked(False)
        self.abort_mission()

    @Slot(str, object)
    def _on_done(self, tag: str, result):
        if isinstance(result, Exception):
            self._status.setText("%s failed: %s" % (tag, result))
            return
        if tag == "estop":
            self.last_estop = result
            self._status.setText("E-STOP %s, write skew %.2f ms" % ("confirmed" if result.confirmed else "NOT confirmed",
                                                                      result.write_skew_ms))
            self._drive_on.setChecked(False)
            self.abort_mission()
        elif tag == "rearm":
            self._status.setText("re-armed"); self._status.setStyleSheet("color: %s;" % theme.MUTED)
        elif tag == "enable":
            self._status.setText("all enabled")

    # --- driving -----------------------------------------------------------------------
    def twist(self) -> K.Twist:
        return K.Twist(vx=self._vx.value() / 1000.0 * self._vx._span,
                       vy=self._vy.value() / 1000.0 * self._vy._span,
                       wz=self._wz.value() / 1000.0 * self._wz._span)

    def set_twist(self, t: K.Twist):
        self._vx.setValue(int(round(t.vx / self._vx._span * 1000)))
        self._vy.setValue(int(round(t.vy / self._vy._span * 1000)))
        self._wz.setValue(int(round(t.wz / self._wz._span * 1000)))

    def drive(self, t: K.Twist) -> float:
        fleet = self._bridge.fleet
        if fleet is None:
            return 0.0
        return fleet.drive(t)

    def stop(self):
        self._drive_on.setChecked(False)
        self.abort_mission()
        self.set_twist(K.Twist())
        fleet = self._bridge.fleet
        if fleet is not None:
            fleet.stop()

    def _on_drive_toggled(self, on: bool):
        if not on and self._bridge.fleet is not None and not self._mission:
            self._bridge.fleet.stop()

    # --- mission -----------------------------------------------------------------------
    def run_mission(self):
        steps = []
        for line in self._mission_edit.toPlainText().splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            steps.append(json.loads(line))
        if not steps or self._bridge.fleet is None:
            return
        self._mission = steps
        self._mission_step_end = 0.0
        self._mission_status.setText("running %d steps" % len(steps))

    def abort_mission(self):
        if self._mission:
            self._mission = []
            self._mission_status.setText("aborted")
            fleet = self._bridge.fleet
            if fleet is not None:
                fleet.stop()

    def _mission_tick(self, now: float):
        if not self._mission:
            return
        if now < self._mission_step_end:
            return
        step = self._mission.pop(0)
        fleet = self._bridge.fleet
        if fleet is None:
            self._mission = []
            return
        if step.get("stop"):
            fleet.stop()
            self.set_twist(K.Twist())
        else:
            t = K.Twist(vx=float(step.get("vx", 0)), vy=float(step.get("vy", 0)), wz=float(step.get("wz", 0)))
            self.set_twist(t)
            fleet.drive(t)
        self._mission_step_end = now + float(step.get("seconds", 1.0))
        self._mission_status.setText("%d steps left" % len(self._mission) if self._mission else "done")
        if not self._mission:
            fleet.stop()

    # --- periodic ------------------------------------------------------------------------
    def _on_tick(self):
        fleet = self._bridge.fleet
        if fleet is None:
            return
        fleet.liveness.touch()                     # the coordinator loop is alive
        now = time.monotonic()
        self._mission_tick(now)
        if self._drive_on.isChecked() and not self._mission:
            fleet.drive(self.twist())
        self._frames += 1
        if self._frames % 4:
            return
        fr = fleet.frame()
        if self._frames % 20 == 0:
            self._poll_res(fleet)
        self._show_res(fleet)
        for role in ROLES:
            if role in fr.boards:
                age, d = fr.boards[role]
                rip = (d.get("encoder") or {}).get("rip")
                if isinstance(rip, (int, float)):
                    self._res_rip[role].setText("%.1f" % rip)
                    self._res_rip[role].setStyleSheet("font-family: Consolas; color: %s;" % (
                        theme.GREEN if rip < 2.0 else theme.YELLOW if rip < 3.5 else theme.RED))
                m = d.get("motor", {})
                state = d.get("state", "?")
                self._state[role].setText(state)
                self._state[role].setStyleSheet("font-family: Consolas; color: %s;" % (
                    theme.RED if state in ("ESTOP", "FAULT") else theme.GREEN if state == "RUNNING" else theme.TEXT))
                self._speed[role].setText("%s%d st/s" % ("-" if not m.get("direction", 1) else "", int(m.get("speed", 0))))
                self._age[role].setText("%.0f ms" % (age * 1000))
        mt = fleet.measured_twist()
        if mt is not None:
            self._measured.setText("measured: vx %.2f  vy %.2f  wz %.2f   (spread %.0f ms)" % (
                mt.vx, mt.vy, mt.wz, fr.spread_s * 1000))

    # --- resonance avoider, per wheel ---------------------------------------------------------
    def _poll_res(self, fleet) -> None:
        """Ask every board for its avoider's state (the reply lands on a worker thread: stored,
        shown on the next tick)."""
        for role, b in fleet.boards.items():
            def got(resp, role=role):
                if resp is not None and resp.success:
                    self._res_status[role] = dict(resp.data)
            try:
                b.submit(C.res_query(), got)
            except Exception:                            # noqa: BLE001 - a board gone: skip it
                pass

    def _show_res(self, fleet) -> None:
        from smc_core.drive_map import bands_from_res
        for role, st in list(self._res_status.items()):
            ck = self._res_on[role]
            ck.setEnabled(float(st.get("f1a_hz") or 0) > 0)
            ck.setChecked(bool(st.get("enabled")))
            state = str(st.get("state", "off"))
            self._res_run[role].setText("%.2f %s" % (float(st.get("run_a", 0)), state[:5]))
            fleet.bands[role] = bands_from_res(st)       # fast crossing follows the switch

    def _on_res_toggle(self, role: str, on: bool) -> None:
        fleet = self._bridge.fleet
        if fleet is not None and role in fleet.boards:
            fleet.boards[role].submit(C.res_config(bool(on)))
            self._poll_res(fleet)

    # --- log -----------------------------------------------------------------------------
    @Slot(float, str, str)
    def _on_tx(self, t: float, role: str, line: str):
        if line.startswith(_HEARTBEAT) and not self._log_hb.isChecked():
            return
        text = "%10.3f  %s  %s" % (t, role, line)
        self._log_lines.append(text)
        if len(self._log_lines) > 20000:
            del self._log_lines[:10000]
        self._log.appendPlainText(text)

    def sent_lines(self) -> List[str]:
        return list(self._log_lines)

    def _clear_log(self):
        self._log_lines = []
        self._log.clear()

    def _save_log(self):
        path, _ = QFileDialog.getSaveFileName(self, "Save command log", "fleet_commands.log", "Log (*.log *.txt)")
        if path:
            with open(path, "w", encoding="utf-8") as fh:
                fh.write("\n".join(self._log_lines) + "\n")

    def shutdown(self):
        self._tick.stop()
        self.view_none.emit()
        fleet = self._bridge.fleet
        if fleet is not None:
            self._bridge.fleet = None
            try:
                fleet.close(estop=False)
            except Exception:                            # noqa: BLE001
                pass
        self.stop_virtual()
