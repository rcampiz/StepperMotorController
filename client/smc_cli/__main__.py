"""
smc: the command-line frontend (Raspberry Pi and bench). Same core as the GUI.

    smc ports                                   list serial ports (ST-LINK ones marked)
    smc info      (--port P | --sim)            identity, version, status
    smc send CMD  (--port P | --sim)            one command line, validated against PROTOCOL.md
    smc telemetry (--port P | --sim) [-n N]     print pushed telemetry (JSON lines)
    smc estop     (--port P | --sim)            fast-path ESTOP and its confirmation
    smc test list
    smc test run NAME (--port P | --sim) [-p key=value ...] [--out results.jsonl]

The CLI is a frontend like the GUI (CLIENT_REVIEW section 9): it builds Requests
with smc_core.commands, runs scenarios through smc_core.runner and prints plain
data. Its wait loops are its control loop: they touch the board's liveness, so a
hung CLI stops feeding the firmware watchdog.
"""

import argparse
import json
import os
import sys
import time
from typing import Any, Dict, List, Optional

_CLIENT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _CLIENT not in sys.path:
    sys.path.insert(0, _CLIENT)

from smc_core import commands as C  # noqa: E402
from smc_core import runner  # noqa: E402
from smc_core.board import Board, BoardConfig, Liveness  # noqa: E402
from smc_core.codec import parse_idn  # noqa: E402


def _out(obj: Any) -> None:
    print(json.dumps(obj, separators=(",", ":"), default=str), flush=True)


def _open(args, heartbeat: bool) -> Board:
    if args.sim:
        from smc_core.sim import FakeBoard
        from smc_core.transport.memory import MemoryTransport
        transport = MemoryTransport(name="sim")
        args._sim = FakeBoard(transport)
        name = "sim"
    else:
        from smc_core.transport.serial import VcpTransport
        transport = VcpTransport(port=args.port, baudrate=args.baud)
        name = args.port or "auto"
    live = Liveness(max_age_s=0.25)
    live.touch()
    cfg = BoardConfig(heartbeat=heartbeat, heartbeat_timeout_ms=args.hb_timeout,
                      telemetry_period_ms=args.tlm_ms)
    board = Board(transport, cfg, liveness=live if heartbeat else None, name=name)
    board._cli_liveness = live
    if not board.open(wait=True, timeout=args.connect_timeout):
        board.close()
        raise SystemExit(f"no session with {name} within {args.connect_timeout}s")
    return board


def _wait(board: Board, ticket, timeout: float = 30.0):
    """The CLI's control loop while it waits: proves liveness every 20 ms."""
    end = time.monotonic() + timeout
    while not ticket.done():
        board._cli_liveness.touch()
        if time.monotonic() > end:
            raise SystemExit("timed out")
        time.sleep(0.02)
    return ticket.wait(0)


def _parse_params(items: List[str]) -> Dict[str, Any]:
    params: Dict[str, Any] = {}
    for item in items or []:
        key, _, raw = item.partition("=")
        try:
            params[key] = json.loads(raw)
        except json.JSONDecodeError:
            params[key] = raw
    return params


# --- commands ---------------------------------------------------------------------
def cmd_ports(args) -> int:
    from smc_core.transport.serial import VcpTransport
    stm = set(VcpTransport.detect_ports())
    for p in VcpTransport.list_all_ports():
        _out({"port": p["device"], "stlink": p["device"] in stm, "description": p["description"],
              "vid_pid": "%04X:%04X" % (p["vid"], p["pid"]) if p["vid"] is not None else None})
    return 0


def cmd_info(args) -> int:
    board = _open(args, heartbeat=False)
    try:
        ident = parse_idn(_wait(board, board.submit(C.idn())))
        ver = _wait(board, board.submit(C.version()))
        st = _wait(board, board.submit(C.status()))
        _out({"identity": ident.__dict__, "version": ver.lines, "status": st.data})
        return 0
    finally:
        board.close()


def cmd_send(args) -> int:
    req = C.from_line(" ".join(args.line))
    board = _open(args, heartbeat=False)
    try:
        resp = _wait(board, board.submit(req), timeout=(req.timeout_ms or 2000) / 1000 + 5)
        _out({"ok": resp.success, "command": resp.command, "data": resp.data,
              "code": resp.error_code, "message": resp.error_message})
        return 0 if resp.success else 1
    finally:
        board.close()


def cmd_telemetry(args) -> int:
    board = _open(args, heartbeat=True)
    count = [0]

    def show(data, t):
        count[0] += 1
        _out({"t": round(t, 4), "board": board.name, "data": data})
    board.subscribe("telemetry", show)
    try:
        while args.n <= 0 or count[0] < args.n:
            board._cli_liveness.touch()
            time.sleep(0.02)
        return 0
    except KeyboardInterrupt:
        return 0
    finally:
        board.close()


def cmd_estop(args) -> int:
    board = _open(args, heartbeat=False)
    try:
        r = _wait(board, board.estop(), timeout=5.0)
        _out({"confirmed": r.confirmed, "detail": r.detail, "dropped": r.dropped,
              "confirm_ms": round((r.t_confirmed - r.t_write) * 1000.0, 2)})
        return 0 if r.confirmed else 2
    finally:
        board.close()


def cmd_test_list(args) -> int:
    for s in runner.list_scenarios():
        _out({"name": s.name, "moves_motor": s.moves_motor, "params": s.params,
              "description": s.description})
    return 0


def cmd_test_run(args) -> int:
    params = _parse_params(args.param)
    board = _open(args, heartbeat=True)
    try:
        results = []
        for name in args.names:
            r = runner.run(name, board, params if len(args.names) == 1 else None,
                           timeout=args.timeout, while_waiting=board._cli_liveness.touch)
            results.append(r)
            _out(r.to_dict() if args.full else {"name": r.name, "ok": r.ok, "summary": r.summary,
                                                "measurements": {k: v for k, v in r.measurements.items()
                                                                 if k not in ("samples", "traceback")}})
        if args.out:
            runner.write_jsonl(args.out, results)
        return 0 if all(r.ok for r in results) else 1
    finally:
        board.close()


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(prog="smc", description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)

    def board_args(p):
        g = p.add_mutually_exclusive_group(required=True)
        g.add_argument("--port", help="serial port (COM6, /dev/ttyACM0)")
        g.add_argument("--sim", action="store_true", help="in-process fake board")
        p.add_argument("--baud", type=int, default=115200)
        p.add_argument("--hb-timeout", type=int, default=2000, help="firmware watchdog ms")
        p.add_argument("--tlm-ms", type=int, default=100, help="telemetry push period ms")
        p.add_argument("--connect-timeout", type=float, default=5.0)

    sub.add_parser("ports").set_defaults(fn=cmd_ports)
    p = sub.add_parser("info"); board_args(p); p.set_defaults(fn=cmd_info)
    p = sub.add_parser("send"); board_args(p); p.add_argument("line", nargs="+")
    p.set_defaults(fn=cmd_send)
    p = sub.add_parser("telemetry"); board_args(p)
    p.add_argument("-n", type=int, default=0, help="stop after N lines (0 = until Ctrl-C)")
    p.set_defaults(fn=cmd_telemetry)
    p = sub.add_parser("estop"); board_args(p); p.set_defaults(fn=cmd_estop)
    t = sub.add_parser("test").add_subparsers(dest="tcmd", required=True)
    t.add_parser("list").set_defaults(fn=cmd_test_list)
    p = t.add_parser("run"); board_args(p)
    p.add_argument("names", nargs="+", help="scenario name(s)")
    p.add_argument("-p", "--param", action="append", help="key=value (JSON value), one scenario only")
    p.add_argument("--out", help="append results to this JSON-lines file")
    p.add_argument("--full", action="store_true", help="print the whole result")
    p.add_argument("--timeout", type=float, default=120.0)
    p.set_defaults(fn=cmd_test_run)
    return ap


def main(argv: Optional[List[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.fn(args)
    finally:
        sim = getattr(args, "_sim", None)
        if sim is not None:
            sim.stop()


if __name__ == "__main__":
    sys.exit(main())
