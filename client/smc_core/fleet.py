"""
Fleet: four boards driven as one mecanum base (docs/MULTI_BOARD.md).

A Fleet owns one Board per wheel role (FL, FR, RL, RR). It adds no threads: it composes
the Boards' own, and every method is callable from the coordinator's single loop.

Safety (MULTI_BOARD.md sections 3 and 4):
- estop(): purge + write the stop bytes on EVERY board first, then confirm each; the
  result reports per-board confirmation and the write skew across boards.
- One board lost (link, heartbeat, a FAULT/STALL event, or a reboot) stops the whole
  base: the remaining boards are ESTOPped. A base with one dead wheel must not keep
  driving. rearm() is the operator's way back.
- One Liveness for all boards: the coordinator loop proves it is alive; if it hangs,
  every board's watchdog stops its wheel.

Identity: each role is bound to a board serial (from *IDN?, i.e. the MCU UID) in the
fleet config, not to the role stored on the board, so a port swap is detected and
DEV:ROLE (broken on current firmware, finding 1) is not needed.
"""

import time
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Tuple

from . import commands as C
from . import kinematics as K
from .board import Board, BoardConfig, EstopResult, Liveness
from .codec import Event, Response
from .transport import open_transport

ROLES = K.ROLES
FAULT_EVENTS = ("FAULT", "STALL")


@dataclass
class FleetConfig:
    addresses: Dict[str, str]                       # role -> COM6 | /dev/ttyACM0 | tcp://...
    serials: Dict[str, str] = field(default_factory=dict)   # role -> expected serial (optional)
    geometry: K.Geometry = field(default_factory=K.Geometry)
    board: BoardConfig = field(default_factory=BoardConfig)
    stop_all_on_board_loss: bool = True


@dataclass
class FleetEstop:
    results: Dict[str, Optional[EstopResult]]
    write_skew_ms: float                            # last write - first write across boards
    errors: Dict[str, str] = field(default_factory=dict)

    @property
    def confirmed(self) -> bool:
        return all(r is not None and r.confirmed for r in self.results.values())


@dataclass
class ClockSync:
    offset_us: float                                # board tick (us) - host time (us)
    rtt_ms: float                                   # of the best sample (uncertainty ~ rtt/2)


@dataclass
class FleetFrame:
    t_host: float                                   # coordinator clock when taken
    boards: Dict[str, Tuple[float, dict]]           # role -> (age in s, DIAG:STAT? data)

    @property
    def max_age_s(self) -> float:
        return max((a for a, _ in self.boards.values()), default=float("inf"))

    @property
    def spread_s(self) -> float:
        ages = [a for a, _ in self.boards.values()]
        return (max(ages) - min(ages)) if ages else 0.0


class FleetError(RuntimeError):
    pass


class Fleet:
    def __init__(self, config: FleetConfig, liveness: Optional[Liveness] = None,
                 transport_factory: Callable[[str], object] = open_transport,
                 clock: Callable[[], float] = time.monotonic):
        missing = [r for r in ROLES if r not in config.addresses]
        if missing:
            raise FleetError(f"no address for {missing}")
        self.config = config
        self.liveness = liveness or Liveness(max_age_s=0.3)
        self._clock = clock
        self.boards: Dict[str, Board] = {
            role: Board(transport_factory(config.addresses[role]), config.board,
                        liveness=self.liveness, clock=clock, name=role)
            for role in ROLES}
        self._telemetry: Dict[str, Tuple[float, dict]] = {}
        self.faults: List[Tuple[str, str]] = []     # (role, why) that stopped the base
        self._stopping = False
        self._opened = False                        # resets before open() are the boot itself
        # The LCDs show the base's motion while it drives (arrow: linear, ring: turn)
        self.show_motion = True
        self.lcd_full_scale = K.Twist(vx=0.5, vy=0.5, wz=2.0)   # 100 % on the indicator
        self._lcd_taken: set = set()                 # roles whose LCD is in REMOTE mode for us
        self._lcd_last = None
        self._listeners: Dict[str, List[Callable]] = {}
        for role, b in self.boards.items():
            b.subscribe("telemetry", lambda data, t, role=role: self._on_telemetry(role, data, t))
            b.subscribe("link_lost", lambda why, role=role: self._board_lost(role, f"link lost: {why}"))
            b.subscribe("heartbeat_lost", lambda n, role=role: self._board_lost(role, "heartbeat lost"))
            b.subscribe("event", lambda ev, role=role: self._on_event(role, ev))
            # a board that rebooted mid-drive has stopped and lost its driver setup
            b.subscribe("reset", lambda info, role=role: self._on_reset(role, info))

    # --- lifecycle ---------------------------------------------------------------
    def open(self, timeout: float = 5.0) -> None:
        """Open every board, then check identities: every serial distinct and, when the
        config names one, the expected board on each role (a swapped cable fails here)."""
        self.liveness.touch()
        for b in self.boards.values():
            b.open()
        end = time.monotonic() + timeout
        while not all(b.session_ready for b in self.boards.values()):
            self.liveness.touch()
            if time.monotonic() > end:
                slow = [r for r, b in self.boards.items() if not b.session_ready]
                self.close()
                raise FleetError(f"no session with {slow}")
            time.sleep(0.01)
        serials = {r: b.identity.serial if b.identity else "?" for r, b in self.boards.items()}
        if len(set(serials.values())) != len(serials):
            self.close()
            raise FleetError(f"two roles reach the same board: {serials}")
        wrong = {r: (serials[r], want) for r, want in self.config.serials.items()
                 if serials.get(r) != want}
        if wrong:
            self.close()
            raise FleetError("board on the wrong corner (role: (found, expected)): %s" % wrong)
        self._opened = True

    def close(self, estop: bool = True) -> None:
        self.release_lcds()
        for b in self.boards.values():
            b.close(estop=estop)

    # --- the LCDs: the base's motion as the arrow (linear) and the ring (turn) ----------------
    def _show_motion(self, twist: K.Twist) -> None:
        if not self.show_motion:
            return
        ind = K.indicator(twist, self.lcd_full_scale)
        if ind == self._lcd_last:
            return
        self._lcd_last = ind
        req = C.disp_indicator(*ind)
        for role, b in self.boards.items():
            if role in self._lcd_taken:
                b.set_latest("indicator", req)
            else:
                # Taking the LCD: the mode switch and the first indicator in order (a coalesced
                # request could reach the board while it is still LOCAL and be refused)
                b.submit(C.ui_mode(remote=True))
                b.submit(req)
                self._lcd_taken.add(role)

    def _lcds_dropped(self, roles=None) -> None:
        """Those LCDs left REMOTE mode (an E-STOP page, a reset): take them again on the next move."""
        for role in (roles if roles is not None else list(self._lcd_taken)):
            self._lcd_taken.discard(role)
        self._lcd_last = None

    def release_lcds(self) -> None:
        """Give every LCD this fleet took back to its own menu (best effort)."""
        for role in list(self._lcd_taken):
            try:
                self.boards[role].request(C.ui_mode(remote=False), timeout=1.0)
            except Exception:                                           # noqa: BLE001
                pass
        self._lcds_dropped()

    def subscribe(self, kind: str, fn: Callable) -> None:
        """kind: "fault" (role, why) when the base was stopped because of one board."""
        self._listeners.setdefault(kind, []).append(fn)

    def _emit(self, kind: str, *args) -> None:
        for fn in self._listeners.get(kind, ()):
            fn(*args)

    # --- safety ------------------------------------------------------------------------
    def estop(self, confirm_timeout: float = 2.0) -> FleetEstop:
        """Write the stop on every board, then confirm each. Callable from any thread."""
        writes: Dict[str, float] = {}
        errors: Dict[str, str] = {}
        for role, b in self.boards.items():             # 1) every write first
            try:
                writes[role] = b.estop_write()
            except Exception as e:  # noqa: BLE001 - one dead port must not stop the others
                errors[role] = str(e)
        tickets = {role: self.boards[role].estop_confirm(t) for role, t in writes.items()}
        results: Dict[str, Optional[EstopResult]] = {r: None for r in ROLES}
        end = time.monotonic() + confirm_timeout
        for role, tk in tickets.items():                # 2) then every confirmation
            try:
                results[role] = tk.wait(max(0.0, end - time.monotonic()))
            except TimeoutError:
                errors[role] = "no confirmation"
        skew = (max(writes.values()) - min(writes.values())) * 1000.0 if writes else 0.0
        return FleetEstop(results, skew, errors)

    def _board_lost(self, role: str, why: str) -> None:
        if not self.config.stop_all_on_board_loss or self._stopping:
            return
        self._stopping = True
        self.faults.append((role, why))
        for other, b in self.boards.items():
            if other != role and b.connected:
                try:
                    b.estop()                            # write now, confirm on its worker
                except Exception:  # noqa: BLE001
                    pass
        self._emit("fault", role, why)

    def _on_reset(self, role: str, info) -> None:
        self._lcds_dropped([role])                  # it rebooted into LOCAL: retake its LCD
        if self.boards[role].session_ready or self._opened:
            self._board_lost(role, f"board reset (boot {info.boot:08X})")

    def _on_event(self, role: str, ev: Event) -> None:
        if ev.type in FAULT_EVENTS:
            self._board_lost(role, f"{ev.type} {ev.detail or ''}".strip())

    def rearm(self) -> Dict[str, Response]:
        """After a base stop: clear every latch (operator action)."""
        self._stopping = False
        self._lcds_dropped()            # the E-STOP pages took the LCDs: the next move retakes them
        return {r: b.request(C.fault_clear()) for r, b in self.boards.items()}

    # --- driving --------------------------------------------------------------------------
    def drive(self, twist: K.Twist) -> float:
        """Run every wheel for a body twist (coalesced: only the newest twist is sent).
        Returns the scale applied to stay under the motor speed limit."""
        cmds, scale = K.motor_commands(twist, self.config.geometry)
        for role in ROLES:
            sps, direction = cmds[role]
            req = C.mot_run(sps, direction) if sps > 0 else C.mot_stop()
            self.boards[role].set_latest("drive", req)
        self._show_motion(twist)
        return scale

    def stop(self) -> None:
        for b in self.boards.values():
            b.set_latest("drive", C.mot_stop())
        self._show_motion(K.Twist())

    def enable(self) -> Dict[str, Response]:
        return self._all(C.mot_enable())

    def _all(self, req: C.Request) -> Dict[str, Response]:
        tickets = {r: b.submit(req) for r, b in self.boards.items()}
        return {r: self._wait(t) for r, t in tickets.items()}

    def _wait(self, ticket, timeout: float = 5.0):
        end = time.monotonic() + timeout
        while not ticket.done():
            self.liveness.touch()
            if time.monotonic() > end:
                raise FleetError("timed out")
            time.sleep(0.005)
        return ticket.wait(0)

    # --- synchronised start -------------------------------------------------------------
    def sync_clocks(self, samples: int = 8) -> Dict[str, ClockSync]:
        """Board microsecond tick vs the coordinator clock, from DIAG:PING round trips
        (best of `samples`, NTP-style: the board's rx/tx ticks sit in the middle)."""
        def job(ctx):
            best = None
            for i in range(samples):
                t0 = self._clock()
                resp = ctx.call(C.ping(i))
                t1 = self._clock()
                if not resp.success:
                    continue
                board_us = (int(resp.data["rx_tick"]) + int(resp.data["tx_tick"])) / 2.0
                host_us = (t0 + t1) / 2.0 * 1e6
                cand = ClockSync(board_us - host_us, (t1 - t0) * 1000.0)
                if best is None or cand.rtt_ms < best.rtt_ms:
                    best = cand
            return best
        tickets = {r: b.run_job(job) for r, b in self.boards.items()}
        out = {r: self._wait(t) for r, t in tickets.items()}
        if any(v is None or isinstance(v, Exception) for v in out.values()):
            raise FleetError(f"clock sync failed: {out}")
        return out

    def start_together(self, requests: Dict[str, C.Request], delay_ms: float = 150.0,
                       timed: bool = True) -> Dict[str, Response]:
        """Queue one motion per wheel, arm, then start. timed=True uses SYNC:START:AT on a
        common instant (needs sync_clocks); timed=False sends SYNC:START to each port in
        turn. The firmware accepts a start up to 2 s ahead and runs it on its Motor task,
        so delay_ms no longer competes with the watchdog (MULTI_BOARD.md section 2)."""
        for role, req in requests.items():
            if req.name != "SYNC:QUEUE":
                raise FleetError(f"{role}: start_together needs SYNC:QUEUE requests, got {req.name}")
        self._all(C.sync_clear())
        for role in ROLES:
            if role in requests:
                self._wait(self.boards[role].submit(requests[role]))
        self._all(C.sync_arm())
        if not timed:
            tickets = {r: self.boards[r].submit(C.sync_start()) for r in requests}
            return {r: self._wait(t) for r, t in tickets.items()}
        sync = self.sync_clocks()
        at_host = self._clock() + delay_ms / 1000.0
        tickets = {}
        for role in requests:
            tick = int(round(at_host * 1e6 + sync[role].offset_us)) & 0xFFFFFFFF
            tickets[role] = self.boards[role].submit(C.sync_start_at(tick))
        return {r: self._wait(t, timeout=5.0 + delay_ms / 1000.0) for r, t in tickets.items()}

    # --- telemetry --------------------------------------------------------------------------
    def _on_telemetry(self, role: str, data: dict, t: float) -> None:
        self._telemetry[role] = (t, data)

    def frame(self) -> FleetFrame:
        """The newest telemetry of every board on the coordinator clock. Boards push on
        their own schedule, so each entry carries its age; `spread_s` is how far apart in
        time the four samples are (merge policy: MULTI_BOARD.md section 6)."""
        now = self._clock()
        return FleetFrame(now, {r: (now - t, d) for r, (t, d) in self._telemetry.items()})

    def measured_twist(self) -> Optional[K.Twist]:
        """Body twist from the four motors' reported speeds (signed by the DIR bit)."""
        fr = self.frame()
        if len(fr.boards) < 4:
            return None
        sps = {}
        for role, (_, d) in fr.boards.items():
            m = d.get("motor", {})
            speed = float(m.get("speed", 0))
            sps[role] = speed if m.get("direction", 1) else -speed
        return K.twist_from_motor_sps(sps, self.config.geometry)
