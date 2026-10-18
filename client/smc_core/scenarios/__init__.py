"""
Named test scenarios (CLIENT_REVIEW section 9, rule 4): data in, data out.

A scenario is a function (ctx: ScenarioContext, params: dict) -> None that records
steps, checks and measurements on the context. It runs as a Board job (on the
board's worker thread, serialised with every other request, heartbeat fed between
calls), so the GUI Tests panel and the CLI run exactly the same code and get the
same ScenarioResult. See runner.py.
"""

import json
import time
from dataclasses import asdict, dataclass, field
from typing import Any, Callable, Dict, List, Optional

from .. import commands as C
from ..board import JobContext
from ..codec import Response

REGISTRY: Dict[str, "ScenarioSpec"] = {}


@dataclass
class ScenarioSpec:
    name: str
    fn: Callable[["ScenarioContext", Dict[str, Any]], None]
    description: str
    params: Dict[str, Any]
    moves_motor: bool


def scenario(name: str, description: str, params: Optional[Dict[str, Any]] = None,
             moves_motor: bool = False) -> Callable:
    def deco(fn):
        REGISTRY[name] = ScenarioSpec(name, fn, description, dict(params or {}), moves_motor)
        return fn
    return deco


@dataclass
class Check:
    name: str
    ok: bool
    detail: str = ""


@dataclass
class ScenarioResult:
    name: str
    board: str
    ok: bool
    params: Dict[str, Any] = field(default_factory=dict)
    checks: List[Check] = field(default_factory=list)
    measurements: Dict[str, Any] = field(default_factory=dict)
    steps: List[Dict[str, Any]] = field(default_factory=list)   # every request and its outcome
    tx: List[str] = field(default_factory=list)                 # command lines sent (no heartbeats)
    error: Optional[str] = None
    started_at: float = 0.0
    duration_s: float = 0.0

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    def to_json(self) -> str:
        return json.dumps(self.to_dict(), separators=(",", ":"), default=str)

    @property
    def summary(self) -> str:
        failed = [c.name for c in self.checks if not c.ok]
        if self.error:
            return f"{self.name}: ERROR {self.error}"
        return f"{self.name}: {'PASS' if self.ok else 'FAIL'}" + (f" ({', '.join(failed)})" if failed else "")


class ScenarioFailed(Exception):
    """Raised by ctx.require(); ends the scenario with ok=False."""


class ScenarioContext:
    def __init__(self, job: JobContext, result: ScenarioResult):
        self.job = job
        self.board = job.board
        self.result = result
        self._t0 = time.monotonic()

    # --- actions ---------------------------------------------------------
    def call(self, req: C.Request) -> Response:
        resp = self.job.call(req)
        self.result.steps.append({"t": round(time.monotonic() - self._t0, 4), "request": req.line,
                                  "ok": resp.success, "code": resp.error_code})
        return resp

    def sleep(self, seconds: float) -> None:
        self.job.sleep(seconds)

    def hold(self, seconds: float) -> None:
        """Deliberately starve the heartbeat for `seconds` (watchdog scenarios)."""
        self.result.steps.append({"t": round(time.monotonic() - self._t0, 4),
                                  "request": f"<hold {seconds:.3f}s without heartbeat>",
                                  "ok": True, "code": None})
        self.job.hold(seconds)

    def estop(self):
        r = self.job.estop()
        self.result.steps.append({"t": round(time.monotonic() - self._t0, 4),
                                  "request": "<ESTOP fast path>", "ok": r.confirmed, "code": None})
        return r

    # --- bookkeeping -----------------------------------------------------
    def check(self, name: str, ok: bool, detail: str = "") -> bool:
        self.result.checks.append(Check(name, bool(ok), detail))
        return bool(ok)

    def require(self, name: str, ok: bool, detail: str = "") -> None:
        if not self.check(name, ok, detail):
            raise ScenarioFailed(f"{name}: {detail}")

    def measure(self, key: str, value: Any) -> None:
        self.result.measurements[key] = value


# Import the built-in scenarios so they register themselves
from . import basic, motion, encoder  # noqa: E402,F401
