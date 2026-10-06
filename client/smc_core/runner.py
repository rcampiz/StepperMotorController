"""
Scenario runner: one entry point for every frontend (CLIENT_REVIEW section 9, rule 4).

    run(name, board, params)         -> ScenarioResult   (blocking; CLI, tests)
    submit(name, board, params, cb)  -> Ticket           (non-blocking; GUI)
    run_in_job(name, job, params)    -> ScenarioResult   (inside an existing board job)

Results are plain dataclasses and serialise to one JSON line each (write_jsonl).
`result.tx` is the list of command lines the scenario put on the wire, with the
board's own traffic (heartbeats, session setup, resyncs) left out, so two frontends
running the same scenario can be compared line for line.
"""

import json
import time
import traceback
from typing import Any, Callable, Dict, Iterable, List, Optional

from .board import Board, Cancelled, JobContext, Ticket
from .scenarios import REGISTRY, ScenarioContext, ScenarioFailed, ScenarioResult, ScenarioSpec

BOARD_TRAFFIC = {"heartbeat", "setup", "resync"}


def list_scenarios() -> List[ScenarioSpec]:
    return [REGISTRY[k] for k in sorted(REGISTRY)]


def _merge(spec: ScenarioSpec, params: Optional[Dict[str, Any]]) -> Dict[str, Any]:
    merged = dict(spec.params)
    for k, v in (params or {}).items():
        if k not in merged:
            raise KeyError(f"{spec.name}: unknown parameter {k!r} (has {sorted(merged)})")
        merged[k] = v
    return merged


def run_in_job(name: str, job: JobContext, params: Optional[Dict[str, Any]] = None) -> ScenarioResult:
    spec = REGISTRY[name]
    p = _merge(spec, params)
    board = job.board
    result = ScenarioResult(name, board.name, False, params=p, started_at=time.time())
    first_tx = len(board.tx_log)
    t0 = time.monotonic()
    ctx = ScenarioContext(job, result)
    try:
        spec.fn(ctx, p)
        result.ok = all(c.ok for c in result.checks)
    except ScenarioFailed:
        result.ok = False
    except Cancelled as e:
        result.ok, result.error = False, f"cancelled: {e}"
    except Exception as e:  # noqa: BLE001 - a scenario bug is a failed result, not a crash
        result.ok, result.error = False, f"{type(e).__name__}: {e}"
        result.measurements["traceback"] = traceback.format_exc()
    result.duration_s = round(time.monotonic() - t0, 4)
    result.tx = [r.text for r in board.tx_log[first_tx:] if r.kind not in BOARD_TRAFFIC]
    return result


def submit(name: str, board: Board, params: Optional[Dict[str, Any]] = None,
           callback: Optional[Callable[[ScenarioResult], None]] = None) -> Ticket:
    if name not in REGISTRY:
        raise KeyError(f"unknown scenario {name!r}")
    _merge(REGISTRY[name], params)                 # validate before queueing
    return board.run_job(lambda job: run_in_job(name, job, params), callback)


def run(name: str, board: Board, params: Optional[Dict[str, Any]] = None,
        timeout: Optional[float] = None, while_waiting: Optional[Callable[[], None]] = None
        ) -> ScenarioResult:
    """Blocking run. `while_waiting` is called every 20 ms (the CLI touches its liveness
    there, so a hung CLI stops feeding the watchdog)."""
    ticket = submit(name, board, params)
    end = None if timeout is None else time.monotonic() + timeout
    while not ticket.done():
        if while_waiting is not None:
            while_waiting()
        if end is not None and time.monotonic() > end:
            board.cancel_job()
            return ticket.wait(10.0)
        time.sleep(0.02)
    result = ticket.wait(0)
    if isinstance(result, Exception):
        raise result
    return result


def write_jsonl(path: str, results: Iterable[ScenarioResult]) -> None:
    with open(path, "a", encoding="utf-8") as fh:
        for r in results:
            fh.write(r.to_json() + "\n")


def read_jsonl(path: str) -> List[Dict[str, Any]]:
    with open(path, encoding="utf-8") as fh:
        return [json.loads(line) for line in fh if line.strip()]


def normalized_tx(tx: List[str]) -> List[str]:
    """The command stream for cross-frontend comparison (CLIENT_REVIEW changes B):
    board traffic is already excluded; runs of the same polling query (e.g.
    CTRL:SYSID:STATUS? while waiting for DONE) collapse to one line, because how many
    polls fit in a wait depends on timing, not on the frontend."""
    out: List[str] = []
    for line in tx:
        if out and line == out[-1] and line.split()[0].endswith("?"):
            continue
        out.append(line)
    return out


def comparable(result: "ScenarioResult") -> Dict[str, Any]:
    """What must be identical when two frontends run the same scenario."""
    timing = ("confirm_ms", "held_ms", "traceback")
    return {"name": result.name, "ok": result.ok, "error": result.error,
            "checks": [(c.name, c.ok) for c in result.checks],
            "measurements": {k: v for k, v in result.measurements.items() if k not in timing},
            "tx": normalized_tx(result.tx)}
