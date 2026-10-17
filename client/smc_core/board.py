"""
Board: one connection to one controller. All host-side threading lives here.

    reader thread   reads lines, classifies them (framing.classify) and routes them:
                    replies to the request in flight, telemetry/events/reset/joystick
                    to subscribers, everything stamped with the board clock.
    worker thread   owns the command flow: session setup (and re-setup after a board
                    reset), coalesced "latest" requests, the FIFO of requests and jobs,
                    the dead-man heartbeat, the ESTOP confirmation, reconnect.

Frontends (GUI, CLI, scenarios) call the public methods from any single thread and
receive results through callbacks (run on a board thread: keep them short, or hand
them to your own loop; smc_gui.bridge turns them into Qt signals). No Qt here.

Safety rules implemented here (CLIENT_REVIEW changes B):
- Liveness is an input: no heartbeat leaves unless `liveness()` is fresh, so a
  frozen frontend stops feeding the firmware watchdog.
- ESTOP fast path: purge unsent output, write 0x18 then SYST:ESTOP under the
  transport write lock, cancel the running job within one chunk, drop the queue,
  then confirm by reading the latch and Hi-Z back. estop_write()/estop_confirm()
  are separate so a Fleet can write every board before waiting for any.
- Event sequence numbers are tracked; a gap is reported (and a status resync
  queued), with the event types on both sides so a lost FAULT is visible.
- Telemetry is pushed by the firmware (SYST:TLM:RATE), not polled.
"""

import itertools
import logging
import queue
import struct
import threading
import time
import zlib
from collections import deque
from dataclasses import dataclass, field
from typing import Any, Callable, Deque, Dict, List, Optional, Tuple, Union

from . import commands as C
from .codec import (Event, Identity, ProtocolError, ResetInfo, Response, parse_event,
                    parse_heartbeat_ack, parse_idn, parse_joystick, parse_reset,
                    parse_response, parse_telemetry)
from .framing import FrameKind, classify
from .protocol_spec import COMMANDS, LIMITS, OOB_ESTOP_BYTE, PROTOCOL_VERSION
from .transport.base import ITransport, TransportError

log = logging.getLogger("smc_core.board")

Clock = Callable[[], float]
MULTILINE_END = "OK "          # ASCII: "OK <n>" closes a multi-line reply


# ---------------------------------------------------------------------------
# Plain data handed to frontends
# ---------------------------------------------------------------------------
def _reply_name(req: C.Request) -> str:
    """The `command` field the firmware puts in the JSON reply to `req` (23 of them differ
    from the request name: SYST:HB -> HEARTBEAT, *IDN? -> IDN, ...)."""
    spec = COMMANDS.get(req.name)
    return spec.json_command if spec is not None and spec.json_command else req.name


@dataclass
class BoardConfig:
    heartbeat: bool = True
    heartbeat_period_s: float = 0.100         # 10 Hz: 5 periods inside the default watchdog
    heartbeat_timeout_ms: int = 500           # firmware watchdog (S-5; see docs/review/LINK_BUDGET.md)
    heartbeat_misses_for_loss: int = 3
    telemetry_period_ms: int = 200            # SYST:TLM:RATE; 0 = no telemetry (5 Hz: link budget)
    # Commands that write internal flash stall the MCU ~1-2 s (128 KB sector erase). The
    # board widens the watchdog to this value around them and restores it after; it never
    # disarms it. 0 = do not widen.
    persist_watchdog_ms: int = LIMITS["heartbeat_timeout_max_ms"]
    events: bool = True                       # SYST:EVT:EN at session start
    clear_fault_on_connect: bool = False      # the GUI's historical behaviour
    reply_timeout_ms: int = 2000
    oob_estop: bool = True                    # send 0x18 before SYST:ESTOP (ignored before Phase 4)
    # The status snapshot is refreshed by the Motor task every 50 ms, so the first read
    # after an ESTOP can still show the bridges on. Re-read for this long before declaring
    # the stop unconfirmed (found on the virtual Nucleo).
    estop_confirm_window_s: float = 0.3
    stream_chunk: int = 256                   # bytes per drained write in uploads
    reconnect_s: float = 0.0                  # >0: reopen the transport after link loss
    flight_recorder_lines: int = 500


@dataclass
class EstopResult:
    confirmed: bool
    detail: str
    dropped: int                 # queued requests discarded
    t_write: float               # board clock when the stop bytes were written
    t_confirmed: Optional[float] = None
    status: Dict[str, Any] = field(default_factory=dict)
    reads: int = 0               # status reads it took (snapshot refresh lag)


@dataclass
class EventGap:
    expected: int
    got: int
    missing: int
    before_type: Optional[str]   # type of the last event received before the gap
    after_type: str              # type of the event that revealed the gap
    critical: bool               # a FAULT/STALL may have been lost


@dataclass
class HeartbeatStats:
    sent: int = 0
    acked: int = 0
    missed: int = 0
    suppressed: int = 0          # not sent because the frontend's liveness was stale
    last_rtt_ms: float = 0.0
    avg_rtt_ms: float = 0.0
    remaining_ms: int = 0


@dataclass
class Record:
    t: float
    direction: str               # "tx" | "rx"
    kind: str                    # tx: cmd/heartbeat/estop/setup/resync/job/binary; rx: frame kind
    text: str


class Cancelled(ProtocolError):
    """The running job was cancelled (by an ESTOP, or by cancel_job())."""


class Liveness:
    """Dead-man token. The frontend calls touch() from its own loop (UI timer, control loop)."""

    def __init__(self, max_age_s: float = 0.2, clock: Clock = time.monotonic):
        self.max_age_s = max_age_s
        self._clock = clock
        self._stamp = -1e18

    def touch(self) -> None:
        self._stamp = self._clock()

    def __call__(self) -> bool:
        return (self._clock() - self._stamp) <= self.max_age_s


class Ticket:
    """Result of a submitted request or job."""

    def __init__(self, callback: Optional[Callable[[Any], None]] = None):
        self._done = threading.Event()
        self._result: Any = None
        self._callback = callback

    def _finish(self, result: Any) -> None:
        self._result = result
        self._done.set()
        if self._callback is not None:
            try:
                self._callback(result)
            except Exception:  # noqa: BLE001 - a frontend bug must not kill the worker
                log.exception("callback failed")

    def done(self) -> bool:
        return self._done.is_set()

    def wait(self, timeout: Optional[float] = None) -> Any:
        if not self._done.wait(timeout):
            raise TimeoutError("request still pending")
        return self._result


class JobContext:
    """Handed to a job running on the worker thread."""

    def __init__(self, board: "Board"):
        self.board = board

    def call(self, req: C.Request) -> Response:
        self.board._check_cancel()
        self.board._service_heartbeat()
        return self.board._execute(req, "job")

    def cancelled(self) -> bool:
        return self.board._cancel.is_set() or self.board._job_cancel.is_set()

    def hold(self, seconds: float) -> None:
        """Block the worker WITHOUT feeding the heartbeat (heartbeat-loss scenarios)."""
        time.sleep(seconds)

    def estop(self) -> "EstopResult":
        """ESTOP from inside a job: write now, drop the queue, confirm inline."""
        b = self.board
        t_write = b.estop_write()
        dropped = b._drop_queued()
        b._cancel.clear()
        return b._confirm_estop(t_write, dropped)

    def sleep(self, seconds: float) -> None:
        """Wait while keeping the heartbeat flowing; raises Cancelled on ESTOP."""
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.board._check_cancel()
            self.board._service_heartbeat()
            time.sleep(min(0.01, max(0.0, end - time.monotonic())))
        self.board._check_cancel()


_Item = Tuple[str, Any, Ticket, str]          # ("req" | "job", payload, ticket, tx kind)


class Board:
    def __init__(self, transport: ITransport, config: Optional[BoardConfig] = None, *,
                 liveness: Optional[Callable[[], bool]] = None, clock: Clock = time.monotonic,
                 name: str = "board"):
        self.config = config or BoardConfig()
        if self.config.heartbeat and liveness is None:
            raise ValueError("heartbeat enabled but no liveness source: the frontend must prove "
                             "it is alive (pass liveness=Liveness() and touch() it)")
        self.transport = transport
        self.name = name
        self._liveness = liveness or (lambda: False)
        self._clock = clock
        self._subs: Dict[str, List[Callable]] = {}

        self._queue: "queue.Queue[_Item]" = queue.Queue()
        self._latest: Dict[str, C.Request] = {}
        self._latest_lock = threading.Lock()
        self._wake = threading.Event()
        self._cancel = threading.Event()          # ESTOP: abort everything
        self._job_cancel = threading.Event()      # cancel_job(): abort the running job only
        self._job_running = False
        self._running = False
        self._link_up = False
        self._reader: Optional[threading.Thread] = None
        self._worker: Optional[threading.Thread] = None
        self._worker_ident: Optional[int] = None

        # reply routing (reader -> worker)
        self._replies: "queue.Queue[Tuple[float, Any]]" = queue.Queue()
        self._in_flight: Optional[C.Request] = None
        self._stale_names: Dict[str, int] = {}
        self._route_lock = threading.Lock()

        # session state
        self.identity: Optional[Identity] = None
        self.reset_info: Optional[ResetInfo] = None
        self.boot_id: Optional[int] = None
        self.last_status: Dict[str, Any] = {}
        self._setup_needed = threading.Event()
        self._session_ready = threading.Event()
        self._hb_seq = 0
        self._hb_next = 0.0
        self._hb_consecutive_misses = 0
        self._hb_loss_signalled = False
        self._liveness_stale = False
        self.hb = HeartbeatStats()
        self._last_seq: Optional[int] = None
        self._last_event_type: Optional[str] = None
        self.gaps: List[EventGap] = []
        self._estop_pending: Optional[Tuple[float, Ticket]] = None
        self._estop_lock = threading.Lock()
        self._estop_dropped = 0
        self._connected_emitted = False

        # flight recorder and link accounting
        self.recorder: Deque[Record] = deque(maxlen=self.config.flight_recorder_lines)
        self.tx_log: List[Record] = []           # every TX line (for identical-output tests)
        self._bytes: Dict[Tuple[str, str], int] = {}
        self._bytes_t0 = clock()

    # ------------------------------------------------------------------
    # Subscriptions
    # ------------------------------------------------------------------
    EVENTS = ("connected", "session", "telemetry", "event", "event_gap", "reset", "heartbeat",
              "heartbeat_lost", "liveness_stale", "estop", "joystick", "link_lost",
              "unsolicited", "error")

    def subscribe(self, kind: str, fn: Callable) -> None:
        if kind not in self.EVENTS:
            raise ValueError(f"unknown board event {kind!r}")
        self._subs.setdefault(kind, []).append(fn)

    def _emit(self, kind: str, *args) -> None:
        for fn in self._subs.get(kind, ()):
            try:
                fn(*args)
            except Exception:  # noqa: BLE001
                log.exception("%s: subscriber for %s failed", self.name, kind)

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------
    def open(self, wait: bool = False, timeout: float = 5.0) -> bool:
        """Open the transport, start the threads, run session setup. Returns at once
        unless wait=True (then True when the session is ready)."""
        if not self.transport.is_connected():
            self.transport.open()
        self._running = True
        self._link_up = True
        self._session_ready.clear()
        self._setup_needed.set()
        self._reader = threading.Thread(target=self._read_loop, name=f"{self.name}-rx", daemon=True)
        self._worker = threading.Thread(target=self._work_loop, name=f"{self.name}-worker", daemon=True)
        self._reader.start()
        self._worker.start()
        if wait:
            return self._session_ready.wait(timeout)
        return True

    def close(self, estop: bool = False) -> None:
        """Stop the threads and close the port. The firmware watchdog is left armed."""
        if estop and self._link_up:
            try:
                self.estop_write()
            except TransportError:
                pass
        self._running = False
        self._cancel.set()
        self._wake.set()
        for t in (self._worker, self._reader):
            if t is not None and t is not threading.current_thread():
                t.join(timeout=3.0)
        try:
            self.transport.close()
        except Exception:  # noqa: BLE001
            pass
        self._link_up = False

    @property
    def connected(self) -> bool:
        return self._running and self._link_up

    @property
    def session_ready(self) -> bool:
        return self._session_ready.is_set()

    # ------------------------------------------------------------------
    # Frontend API
    # ------------------------------------------------------------------
    def submit(self, req: C.Request, callback: Optional[Callable[[Response], None]] = None) -> Ticket:
        t = Ticket(callback)
        self._queue.put(("req", req, t, "cmd"))
        self._wake.set()
        return t

    def request(self, req: C.Request, timeout: Optional[float] = None) -> Response:
        """Blocking call. On the worker thread (inside a job) it executes inline."""
        if threading.get_ident() == self._worker_ident:
            return self._execute(req, "job")
        t = self.submit(req)
        wait = timeout if timeout is not None else (req.timeout_ms or self.config.reply_timeout_ms) / 1000.0 + 5.0
        return t.wait(wait)

    def set_latest(self, key: str, req: C.Request) -> None:
        """Coalesced request: only the newest per key is sent, ahead of the FIFO
        (live speed fader, display indicator)."""
        with self._latest_lock:
            self._latest[key] = req
        self._wake.set()

    def cancel_job(self) -> None:
        """Cancel the running job at its next call()/sleep() (no ESTOP, queue kept)."""
        if self._job_running:
            self._job_cancel.set()
        self._wake.set()

    def run_job(self, fn: Callable[[JobContext], Any],
                callback: Optional[Callable[[Any], None]] = None) -> Ticket:
        """Run fn(ctx) on the worker thread, serialised with every other request."""
        t = Ticket(callback)
        self._queue.put(("job", fn, t, "job"))
        self._wake.set()
        return t

    # --- ESTOP ---------------------------------------------------------
    ESTOP_LINE = b"SYST:ESTOP\r\n"

    def estop_write(self) -> float:
        """Write the stop bytes now (any thread). Returns the board-clock write time."""
        self._cancel.set()
        payload = (bytes([OOB_ESTOP_BYTE]) if self.config.oob_estop else b"") + self.ESTOP_LINE
        with self._route_lock:
            self._stale_names["SYST:ESTOP"] = self._stale_names.get("SYST:ESTOP", 0) + 1
        self.transport.send_priority(payload)
        t = self._clock()
        self._record("tx", "estop", "SYST:ESTOP" if not self.config.oob_estop else "<0x18> SYST:ESTOP")
        self._count("tx", "estop", len(payload))
        return t

    def estop_confirm(self, t_write: float,
                      callback: Optional[Callable[[EstopResult], None]] = None) -> Ticket:
        """Queue the read-back that confirms the latch and Hi-Z (worker side)."""
        ticket = Ticket(callback)
        with self._estop_lock:
            self._estop_pending = (t_write, ticket)
        self._wake.set()
        return ticket

    def estop(self, callback: Optional[Callable[[EstopResult], None]] = None) -> Ticket:
        """Fast path: write, cancel, drop the queue, confirm."""
        return self.estop_confirm(self.estop_write(), callback)

    @staticmethod
    def estop_confirmed(status: Dict[str, Any]) -> Tuple[bool, str]:
        state = str(status.get("state", "?"))
        motor = status.get("motor", {}) or {}
        hiz = motor.get("hi_z", motor.get("hiz"))
        ok = state in ("ESTOP", "FAULT") and bool(hiz)
        return ok, f"state={state} hiZ={hiz}"

    # --- accounting ----------------------------------------------------
    def link_budget(self) -> Dict[str, float]:
        """Bytes per second by direction and kind since open (or the last reset_budget)."""
        dt = max(1e-6, self._clock() - self._bytes_t0)
        return {f"{d}:{k}": n / dt for (d, k), n in sorted(self._bytes.items())}

    def reset_budget(self) -> None:
        self._bytes.clear()
        self._bytes_t0 = self._clock()

    def flight_recorder(self) -> List[Record]:
        return list(self.recorder)

    # ------------------------------------------------------------------
    # Reader thread
    # ------------------------------------------------------------------
    def _read_loop(self) -> None:
        while self._running:
            if not self._link_up:
                time.sleep(0.02)
                continue
            try:
                line = self.transport.recv_line(timeout_ms=50)
            except TransportError as e:
                self._link_lost(str(e))
                continue
            if line is None:
                continue
            now = self._clock()
            self._count("rx", "line", len(line) + 2)
            inflight = self._in_flight
            frame = classify(line, inflight.line if inflight else None)
            self._record("rx", frame.kind.value, frame.text)
            try:
                self._route(frame, now)
            except ProtocolError as e:
                log.warning("%s: %s", self.name, e)
                self._emit("error", str(e))

    def _route(self, frame, now: float) -> None:
        k = frame.kind
        if k == FrameKind.RESPONSE:
            if self._is_stale(frame):
                return
            inflight = self._in_flight
            if (inflight is not None and inflight.reply == C.Reply.SINGLE and frame.obj is None
                    and not frame.text.startswith(("OK", "ERROR", "{"))):
                # A single reply is an OK/ERROR line or one JSON object. Anything else is the
                # tail of a line that was already in flight when the port opened (a telemetry
                # push cut in half, seen on COM5); taking it as the reply would shift every
                # later reply by one request.
                self._record("rx", "junk", frame.text)
                self._emit("unsolicited", frame.text)
                return
            if inflight is not None:
                self._replies.put((now, frame))
            else:
                self._emit("unsolicited", frame.text)
        elif k == FrameKind.TELEMETRY:
            data = parse_telemetry(frame)
            self.last_status = data
            self._emit("telemetry", data, now)
        elif k == FrameKind.EVENT:
            self._on_event(parse_event(frame), now)
        elif k == FrameKind.RESET:
            self._on_reset(parse_reset(frame))
        elif k == FrameKind.JOYSTICK:
            self._emit("joystick", parse_joystick(frame))

    def _is_stale(self, frame) -> bool:
        """A JSON reply that belongs to an ESTOP write or a timed-out request, not to
        the request in flight (one-in-flight protocol; replies arrive in order)."""
        cmd = frame.obj.get("command") if frame.obj else None
        if not cmd:
            return False
        with self._route_lock:
            inflight = self._in_flight
            if inflight is not None and cmd in (inflight.name, _reply_name(inflight)):
                return False
            alias = "SYST:ESTOP" if cmd in ("SYST:ESTOP", "ESTOP") else cmd
            n = self._stale_names.get(alias, 0)
            if n <= 0:
                return False
            if n == 1:
                del self._stale_names[alias]
            else:
                self._stale_names[alias] = n - 1
        self._record("rx", "stale", frame.text)
        return True

    def _on_event(self, ev: Event, now: float) -> None:
        if ev.seq is not None:
            last = self._last_seq
            if last is not None and ev.seq != last + 1:
                if ev.seq > last + 1:
                    gap = EventGap(last + 1, ev.seq, ev.seq - last - 1, self._last_event_type,
                                   ev.type, True)
                    self.gaps.append(gap)
                    log.warning("%s: %d event(s) lost before seq %d", self.name, gap.missing, ev.seq)
                    self._emit("event_gap", gap)
                    self._queue.put(("req", C.status().tagged("resync"), Ticket(self._resync_done),
                                     "resync"))
                    self._queue.put(("req", C.events_status().tagged("resync"), Ticket(), "resync"))
                    self._wake.set()
                else:
                    log.warning("%s: event seq went backwards (%d -> %d) without !RESET",
                                self.name, last, ev.seq)
            self._last_seq = ev.seq
        self._last_event_type = ev.type
        self._emit("event", ev)

    def _resync_done(self, resp: Response) -> None:
        if resp.success and resp.data:
            self.last_status = resp.data
            self._emit("telemetry", resp.data, self._clock())

    def _on_reset(self, info: ResetInfo) -> None:
        first = self.reset_info is None and self.boot_id is None
        self.reset_info = info
        changed = self.boot_id is not None and info.boot != self.boot_id
        self.boot_id = info.boot
        self._last_seq = None
        self._last_event_type = None
        if changed or not first:
            log.warning("%s: board reset (boot %08X), re-running session setup", self.name, info.boot)
        self._emit("reset", info)
        self._session_ready.clear()
        self._setup_needed.set()
        self._wake.set()

    # ------------------------------------------------------------------
    # Worker thread
    # ------------------------------------------------------------------
    def _work_loop(self) -> None:
        self._worker_ident = threading.get_ident()
        while self._running:
            try:
                if not self._link_up:
                    self._try_reconnect()
                    continue
                self._service_estop()
                if self._setup_needed.is_set():
                    self._run_setup()
                    continue
                self._service_latest()
                self._service_queue_one()
                self._service_heartbeat()
            except TransportError as e:
                self._link_lost(str(e))
            except Exception:  # noqa: BLE001
                log.exception("%s: worker iteration failed", self.name)
            self._wake.wait(0.005)
            self._wake.clear()

    def _try_reconnect(self) -> None:
        if self.config.reconnect_s <= 0:
            time.sleep(0.05)
            return
        time.sleep(self.config.reconnect_s)
        try:
            self.transport.close()
            if self.transport.open():
                log.info("%s: link restored", self.name)
                self._link_up = True
                self._setup_needed.set()
        except Exception as e:  # noqa: BLE001
            log.debug("%s: reconnect failed: %s", self.name, e)

    def _link_lost(self, why: str) -> None:
        if not self._link_up:
            return
        self._link_up = False
        self._session_ready.clear()
        log.error("%s: link lost: %s", self.name, why)
        self._replies.put((self._clock(), None))
        self._emit("link_lost", why)

    def _check_cancel(self) -> None:
        if self._cancel.is_set():
            raise Cancelled("cancelled by ESTOP")
        if self._job_cancel.is_set():
            raise Cancelled("job cancelled")

    def _drop_queued(self) -> int:
        dropped = 0
        while True:
            try:
                kind, _, t, _ = self._queue.get_nowait()
            except queue.Empty:
                break
            dropped += 1
            t._finish(Response(False, "", {}, "CANCELLED", "dropped by ESTOP") if kind == "req"
                      else Cancelled("dropped by ESTOP"))
        with self._latest_lock:
            dropped += len(self._latest)
            self._latest.clear()
        return dropped

    def _service_estop(self) -> None:
        """After estop_write(): drop everything queued at once; when a confirmation
        was requested, read the latch and Hi-Z back."""
        if self._cancel.is_set():
            self._estop_dropped += self._drop_queued()
            self._cancel.clear()
        with self._estop_lock:
            pending, self._estop_pending = self._estop_pending, None
        if pending is None:
            return
        t_write, ticket = pending
        dropped, self._estop_dropped = self._estop_dropped, 0
        ticket._finish(self._confirm_estop(t_write, dropped))

    def _confirm_estop(self, t_write: float, dropped: int) -> EstopResult:
        deadline = time.monotonic() + self.config.estop_confirm_window_s
        reads = 0
        while True:
            resp = self._execute(C.status().tagged("estop-confirm"), "estop")
            reads += 1
            if resp.success:
                self.last_status = resp.data
                ok, detail = self.estop_confirmed(resp.data)
            else:
                ok, detail = False, f"no status read-back: {resp.error_message}"
            if ok or time.monotonic() >= deadline or not self._link_up:
                break
            time.sleep(0.02)
        result = EstopResult(ok, detail, dropped, t_write, self._clock(), resp.data or {}, reads)
        log.warning("%s: ESTOP %s (%s), %d queued dropped", self.name,
                    "confirmed" if ok else "NOT CONFIRMED", detail, dropped)
        self._emit("estop", result)
        return result

    def _run_setup(self) -> None:
        """Session setup: JSON mode, identity, events, telemetry push, watchdog."""
        self._setup_needed.clear()
        try:
            self.transport.flush_input()
        except TransportError:
            raise
        except Exception:  # noqa: BLE001
            pass
        resp = None
        for _ in range(3):
            resp = self._execute(C.fmt(True), "setup")
            if resp.success:
                break
            time.sleep(0.2)
        if resp is None or not resp.success:
            self._emit("error", f"no reply to FMT JSON ({resp.error_message if resp else '?'})")
            self._setup_needed.set()
            time.sleep(0.5)
            return
        idn = self._execute(C.idn(), "setup")
        if idn.success:
            try:
                self.identity = parse_idn(idn)
            except ProtocolError as e:
                self._emit("error", str(e))
        if self.identity is not None and self.identity.proto != PROTOCOL_VERSION:
            self._emit("error", f"board speaks protocol {self.identity.proto}, "
                                f"this client protocol {PROTOCOL_VERSION}")
        if self.config.clear_fault_on_connect:
            self._execute(C.fault_clear(), "setup")
        if self.config.events:
            self._execute(C.events_enable(), "setup")
        if self.config.telemetry_period_ms > 0:
            self._execute(C.telemetry_rate(self.config.telemetry_period_ms), "setup")
        if self.config.heartbeat:
            self._hb_next = 0.0
            self._service_heartbeat()                       # first feed, then arm
            self._execute(C.heartbeat_timeout(self.config.heartbeat_timeout_ms), "setup")
        self._session_ready.set()
        self._emit("session", self.identity)
        if not self._connected_emitted:
            self._connected_emitted = True
            self._emit("connected", self.identity)

    def _service_latest(self) -> None:
        with self._latest_lock:
            items = list(self._latest.items())
            self._latest.clear()
        for _, req in items:
            try:
                self._execute(req, "cmd")
            except Cancelled:
                return

    def _service_queue_one(self) -> None:
        try:
            kind, payload, ticket, txkind = self._queue.get_nowait()
        except queue.Empty:
            return
        if kind == "req":
            try:
                result = self._execute(payload, txkind)
            except Cancelled as e:
                result = Response(False, payload.name, {}, "CANCELLED", str(e))
            ticket._finish(result)
            return
        self._job_cancel.clear()
        self._job_running = True
        try:
            result = payload(JobContext(self))
        except Cancelled as e:
            result = e
        except Exception as e:  # noqa: BLE001 - R8: errors reach the frontend
            log.exception("%s: job failed", self.name)
            result = e
        finally:
            self._job_running = False
            self._job_cancel.clear()
        ticket._finish(result)

    def _service_heartbeat(self) -> None:
        if not self.config.heartbeat or not self._link_up:
            return
        now = self._clock()
        if now < self._hb_next:
            return
        self._hb_next = now + self.config.heartbeat_period_s
        if not self._liveness():
            self.hb.suppressed += 1
            if not self._liveness_stale:
                self._liveness_stale = True
                log.warning("%s: frontend liveness stale, heartbeat suppressed", self.name)
                self._emit("liveness_stale", True)
            return
        if self._liveness_stale:
            self._liveness_stale = False
            self._emit("liveness_stale", False)
        self._hb_seq += 1
        t0 = self._clock()
        resp = self._execute(C.heartbeat(self._hb_seq), "heartbeat")
        self.hb.sent += 1
        if not resp.success:
            self.hb.missed += 1
            self._hb_consecutive_misses += 1
            if (self._hb_consecutive_misses >= self.config.heartbeat_misses_for_loss
                    and not self._hb_loss_signalled):
                self._hb_loss_signalled = True
                self._emit("heartbeat_lost", self._hb_consecutive_misses)
            return
        rtt = (self._clock() - t0) * 1000.0
        self.hb.acked += 1
        self._hb_consecutive_misses = 0
        self._hb_loss_signalled = False
        self.hb.last_rtt_ms = rtt
        self.hb.avg_rtt_ms = rtt if self.hb.avg_rtt_ms == 0 else 0.3 * rtt + 0.7 * self.hb.avg_rtt_ms
        try:
            ack = parse_heartbeat_ack(resp)
        except (ValueError, TypeError):
            ack = None
        if ack is not None:
            self.hb.remaining_ms = ack.remaining_ms
            if ack.boot is not None:
                if self.boot_id is not None and ack.boot != self.boot_id:
                    self._on_reset(ResetInfo(ack.boot, "?", 0, "?", raw="(boot id changed in ack)"))
                self.boot_id = ack.boot
        self._emit("heartbeat", {
            "seq": self._hb_seq, "mcu_tick": ack.mcu_tick if ack else 0,
            "remaining_ms": ack.remaining_ms if ack else 0, "rtt_ms": rtt,
            "sent": self.hb.sent, "acked": self.hb.acked, "missed": self.hb.missed,
            "avg_rtt_ms": self.hb.avg_rtt_ms, "interval_ms": self.config.heartbeat_period_s * 1000.0,
            "suppressed": self.hb.suppressed, "timeout_ms": self.config.heartbeat_timeout_ms})

    # ------------------------------------------------------------------
    # One request on the wire (worker thread only)
    # ------------------------------------------------------------------
    def _execute(self, req: C.Request, txkind: str) -> Response:
        spec = COMMANDS.get(req.name)
        if (spec is not None and spec.persists and self.config.heartbeat
                and 0 < self.config.heartbeat_timeout_ms < self.config.persist_watchdog_ms
                and self._session_ready.is_set()):
            return self._execute_widened(req, txkind)
        return self._execute_one(req, txkind)

    def _execute_widened(self, req: C.Request, txkind: str) -> Response:
        """Watchdog widened around a flash write (never disarmed), then restored and fed."""
        self._execute_one(C.heartbeat_timeout(self.config.persist_watchdog_ms), "watchdog")
        try:
            return self._execute_one(req, txkind)
        finally:
            self._execute_one(C.heartbeat_timeout(self.config.heartbeat_timeout_ms), "watchdog")

    def _execute_one(self, req: C.Request, txkind: str) -> Response:
        if not self._link_up:
            return Response(False, req.name, {}, "LINK", "link down")
        timeout = (req.timeout_ms or self.config.reply_timeout_ms) / 1000.0
        while not self._replies.empty():                       # late replies from before
            try:
                self._replies.get_nowait()
            except queue.Empty:
                break
        with self._route_lock:
            self._in_flight = req
        try:
            self.transport.send_line(req.line)
            self._record("tx", txkind, req.line)
            self._count("tx", txkind, len(req.line) + 2)
            first = self._wait_reply(req, timeout)
            if first is None:
                return self._timed_out(req)
            if req.reply == C.Reply.READY:
                return self._stream(req, first, timeout)
            if req.reply == C.Reply.LINES and first.obj is None:
                return self._collect_lines(req, first, timeout)
            return parse_response(first.text, req.name, first.obj)
        finally:
            with self._route_lock:
                self._in_flight = None

    def _wait_reply(self, req: C.Request, timeout: float):
        try:
            _, frame = self._replies.get(timeout=timeout)
        except queue.Empty:
            return None
        return frame

    def _timed_out(self, req: C.Request) -> Response:
        with self._route_lock:
            name = _reply_name(req)
            self._stale_names[name] = self._stale_names.get(name, 0) + 1
        return Response(False, req.name, {}, "TIMEOUT", f"no reply to {req.line}")

    def _collect_lines(self, req: C.Request, first, timeout: float) -> Response:
        """ASCII multi-line reply (only before FMT JSON took effect): lines until OK <n>."""
        lines: List[str] = []
        frame = first
        while True:
            text = frame.text
            if text.startswith("ERROR"):
                return parse_response(text, req.name)
            if text.startswith(MULTILINE_END) and text[3:].isdigit():
                return Response(True, req.name, {"lines": lines, "count": len(lines)},
                                raw="\n".join(lines))
            lines.append(text)
            frame = self._wait_reply(req, timeout)
            if frame is None:
                return self._timed_out(req)

    def _stream(self, req: C.Request, first, timeout: float) -> Response:
        """OK READY <n>, then the payload in drained chunks, then the final reply."""
        ready = parse_response(first.text, req.name, first.obj)
        if not ready.success:
            return ready
        n = ready.data.get("ready_bytes")
        payload = req.payload or b""
        if n is None:
            return Response(False, req.name, {}, "PROTOCOL", f"expected READY, got {first.text}")
        if len(payload) < n:
            return Response(False, req.name, {}, "PROTOCOL",
                            f"payload {len(payload)} bytes, board expects {n}")
        sent = 0
        chunk = max(1, self.config.stream_chunk)
        while sent < n:
            self._check_cancel()
            part = payload[sent:sent + min(chunk, n - sent)]
            self.transport.send(part)
            self.transport.drain_output(timeout_ms=500)
            sent += len(part)
            self._count("tx", "binary", len(part))
        if req.crc:
            self.transport.send(struct.pack("<I", zlib.crc32(payload[:n]) & 0xFFFFFFFF))
            self._count("tx", "binary", 4)
        self._record("tx", "binary", f"<{n} bytes{' + CRC32' if req.crc else ''}>")
        final = self._wait_reply(req, timeout)
        if final is None:
            return self._timed_out(req)
        return parse_response(final.text, req.name, final.obj)

    # ------------------------------------------------------------------
    def _record(self, direction: str, kind: str, text: str) -> None:
        r = Record(self._clock(), direction, kind, text)
        self.recorder.append(r)
        if direction == "tx":
            self.tx_log.append(r)

    def _count(self, direction: str, kind: str, n: int) -> None:
        key = (direction, kind)
        self._bytes[key] = self._bytes.get(key, 0) + n
