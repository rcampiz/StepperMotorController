"""
The one place where smc_core callbacks become Qt signals (CLIENT_REVIEW section 9, rule 2).

Board callbacks run on the board's threads. Emitting a Qt signal from there is safe:
connections to QObjects living on the UI thread are queued, so every slot below runs
on the UI thread. Nothing else in smc_gui touches a board thread.
"""

from typing import Any, Callable, Optional

from PySide6.QtCore import QObject, Signal

from smc_core import commands as C
from smc_core.board import Board, EstopResult, EventGap
from smc_core.codec import Event, Response, ResetInfo, multiline_text


class BoardBridge(QObject):
    connected = Signal(object)              # Identity
    session = Signal(object)                # Identity (also after a board reset)
    telemetry = Signal(dict)                # DIAG:STAT? data dict (pushed)
    event = Signal(str, dict)               # (type, {"status": "XXXX", "detail": ...})
    event_gap = Signal(object)              # EventGap
    board_reset = Signal(object)            # ResetInfo
    heartbeat = Signal(dict)                # ack statistics (link health)
    heartbeat_lost = Signal()
    liveness_stale = Signal(bool)
    estop_result = Signal(object)           # EstopResult
    link_lost = Signal(str)
    error = Signal(str)
    response = Signal(str, str, object)     # (tag, command line, Response or None)
    job_done = Signal(str, object)          # (tag, result or exception)
    progress = Signal(int, int)             # (done, total) from upload jobs

    def __init__(self, board: Board, parent=None):
        super().__init__(parent)
        self.board = board
        board.subscribe("connected", self.connected.emit)
        board.subscribe("session", self.session.emit)
        board.subscribe("telemetry", lambda data, t: self.telemetry.emit(data))
        board.subscribe("event", self._on_event)
        board.subscribe("event_gap", self.event_gap.emit)
        board.subscribe("reset", self.board_reset.emit)
        board.subscribe("heartbeat", self.heartbeat.emit)
        board.subscribe("heartbeat_lost", lambda n: self.heartbeat_lost.emit())
        board.subscribe("liveness_stale", self.liveness_stale.emit)
        board.subscribe("estop", self.estop_result.emit)
        board.subscribe("link_lost", self.link_lost.emit)
        board.subscribe("error", self.error.emit)

    def _on_event(self, ev: Event) -> None:
        data = {"status": "%04X" % ev.status if ev.status is not None else "?"}
        if ev.detail is not None:
            data["detail"] = ev.detail
        if ev.seq is not None:
            data["seq"] = ev.seq
        self.event.emit(ev.type, data)

    # --- requests ---------------------------------------------------------
    def call(self, req: C.Request, tag: str = "GENERIC") -> None:
        """Queue a request; the reply arrives as response(tag, line, Response)."""
        line = req.line

        def done(resp: Response) -> None:
            self.response.emit(tag, line, _with_text(req, resp))
        self.board.submit(req, done)

    def latest(self, key: str, req: C.Request) -> None:
        self.board.set_latest(key, req)

    def job(self, fn: Callable, tag: str) -> None:
        self.board.run_job(fn, lambda result: self.job_done.emit(tag, result))

    def estop(self) -> None:
        self.board.estop()


def _with_text(req: C.Request, resp: Optional[Response]) -> Optional[Response]:
    """Give multi-line replies their text form in `raw` (the raw-data display)."""
    if resp is None or not resp.success or req.reply != C.Reply.LINES:
        return resp
    resp.raw = multiline_text(resp)
    return resp
