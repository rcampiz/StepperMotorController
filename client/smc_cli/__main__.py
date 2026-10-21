"""
smc: the command-line frontend (Raspberry Pi and bench). Same core as the GUI.

    smc ports                                   list serial ports (ST-LINK ones marked)
    smc info      (--port P | --sim)            identity, version, status
    smc send CMD  (--port P | --sim)            one command line, validated against PROTOCOL.md
    smc telemetry (--port P | --sim) [-n N]     print pushed telemetry (JSON lines)
    smc estop     (--port P | --sim)            fast-path ESTOP and its confirmation
    smc test list
    smc test run NAME (--port P | --sim) [-p key=value ...] [--out results.jsonl]
    smc motor list                              motor profiles (client files + ~/.smc/motors)
    smc motor show NAME [--protocol 2]          one profile and the commands it sends
    smc motor apply NAME (--port P | --sim) [--save]   configure the board from a profile
    smc driver caps (--port P | --sim)          what the motor driver can do (DRV:CAPS?)
    smc driver show (--port P | --sim)          its settings in physical units (DRV:CFG:PHYS?)
    smc fleet check  --config fleet.json        open four boards, verify corners, show a frame
    smc fleet estop  --config fleet.json        stop the whole base (all writes, then confirms)
    smc fleet drive  --config fleet.json --vx 0.2 [--vy 0] [--wz 0] [--seconds 2]

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
from smc_core import driver as D  # noqa: E402
from smc_core import runner  # noqa: E402
from smc_core.board import Board, BoardConfig, Liveness  # noqa: E402
from smc_core.codec import parse_idn  # noqa: E402
from smc_core.version import full_version  # noqa: E402


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
        from smc_core.transport import open_transport
        transport = open_transport(args.port, args.baud)
        name = args.port or "auto"
    live = Liveness(max_age_s=0.25)
    live.touch()
    cfg = BoardConfig(heartbeat=heartbeat, heartbeat_timeout_ms=args.hb_timeout,
                      telemetry_period_ms=args.tlm_ms, telemetry_compact=args.tlm_compact,
                      fast_baud=getattr(args, "link", 0) or 0)
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


def _find_nod_drive():
    """The NUCLEO's ST-LINK mass-storage drive (MBED.HTM + DETAILS.TXT at its root)."""
    import string
    for letter in string.ascii_uppercase:
        root = f"{letter}:\\"
        if os.path.isfile(os.path.join(root, "MBED.HTM")) and os.path.isfile(os.path.join(root, "DETAILS.TXT")):
            return root
    return None


def cmd_flash(args) -> int:
    """Program a .bin through the NUCLEO drive with the session held open. The Board re-runs
    its setup when the !RESET line arrives, so the board is never left to boot into a port
    that is opened afterwards (which was seen to come up silent on the bench)."""
    import shutil
    drive = args.drive or _find_nod_drive()
    if drive is None:
        raise SystemExit("no NUCLEO drive found (MBED.HTM + DETAILS.TXT at the root); pass --drive")
    if not os.path.isfile(args.bin):
        raise SystemExit(f"not a file: {args.bin}")
    board = _open(args, heartbeat=True)
    resets, sessions = [], []
    board.subscribe("reset", lambda info, *a: resets.append(info))
    board.subscribe("session", lambda ident, *a: sessions.append(ident))   # the setup re-run after !RESET
    try:
        t0 = time.monotonic()
        shutil.copy(args.bin, drive)
        t_copy = time.monotonic() - t0
        end = time.monotonic() + args.flash_timeout
        while time.monotonic() < end:
            board._cli_liveness.touch()
            if resets and sessions and board.session_ready:
                break
            time.sleep(0.05)
        if not resets:
            raise SystemExit(f"no !RESET within {args.flash_timeout:.0f}s after the copy ({t_copy:.1f}s)")
        info = resets[-1]
        _out({"drive": drive, "bin": args.bin, "copy_s": round(t_copy, 1), "boot_s": round(time.monotonic() - t0, 1),
              "build": info.build, "fw": info.fw, "proto": info.proto, "rst": info.rst, "boot": f"{info.boot:08X}",
              "serial": board.identity.serial if board.identity else None, "session": board.session_ready})
        return 0 if board.session_ready else 2
    finally:
        board.close()


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
        _out({"identity": ident.__dict__, "version": ver.lines, "status": st.data,
              "client": full_version()})
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


def _fleet(args):
    from smc_core import kinematics as K
    from smc_core.fleet import Fleet, FleetConfig
    with open(args.config, encoding="utf-8") as fh:
        cfg = json.load(fh)
    geo = K.Geometry(**cfg.get("geometry", {}))
    board = BoardConfig(**cfg.get("board", {}))
    fleet = Fleet(FleetConfig(addresses=cfg["addresses"], serials=cfg.get("serials", {}),
                              geometry=geo, board=board))
    fleet.open()
    fleet.subscribe("fault", lambda role, why: _out({"fault": role, "why": why}))
    return fleet


def _fleet_loop(fleet, seconds: float) -> None:
    """The coordinator's control loop: prove liveness, keep going until `seconds` pass."""
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        fleet.liveness.touch()
        time.sleep(0.02)


def cmd_motor_list(args) -> int:
    from smc_core import motors
    _out({"profiles": [{"name": p.name, "title": p.label, "rated_current_a": p.rated_current_a,
                        "drive_mode": p.drive_mode, "source": p.source} for p in motors.list_profiles()]})
    return 0


def cmd_motor_show(args) -> int:
    from smc_core import motors
    p = motors.load_profile(args.name)
    physical = D.supports_physical(args.protocol)
    _out({"name": p.name, "profile": p.to_dict(), "protocol": args.protocol,
          "commands": [r.line for r in p.requests(physical)]})
    return 0


def cmd_motor_apply(args) -> int:
    from smc_core import motors
    p = motors.load_profile(args.name)
    board = _open(args, heartbeat=False)
    try:
        # protocol 3: the physical commands; a protocol 2 board gets the chip codes
        reqs = p.requests(D.supports_physical(board.protocol))
        if args.save:
            reqs.append(C.cfg_save())
        sent = []
        for req in reqs:
            resp = _wait(board, board.submit(req), timeout=10.0)
            sent.append({"command": req.line, "ok": resp.success, "message": resp.error_message or ""})
            if not resp.success:
                _out({"profile": p.name, "applied": False, "sent": sent})
                return 1
        _out({"profile": p.name, "applied": True, "saved": bool(args.save), "sent": sent})
        return 0
    finally:
        board.close()


def _driver_access(board: Board) -> "D.DriverAccess":
    return D.DriverAccess(lambda req: _wait(board, board.submit(req), timeout=10.0), board.protocol)


def cmd_driver_caps(args) -> int:
    board = _open(args, heartbeat=False)
    try:
        drv = _driver_access(board)
        caps = drv.caps()
        _out({"protocol": board.protocol, "from_board": drv.physical, "caps": caps})
        return 0
    finally:
        board.close()


def cmd_driver_show(args) -> int:
    board = _open(args, heartbeat=False)
    try:
        drv = _driver_access(board)
        _out({"protocol": board.protocol, "settings": drv.settings(), "in_force": drv.resolution()})
        return 0
    finally:
        board.close()


def cmd_fleet_check(args) -> int:
    fleet = _fleet(args)
    try:
        _fleet_loop(fleet, 0.6)
        fr = fleet.frame()
        _out({"boards": {r: {"serial": b.identity.serial, "fw": b.identity.fw,
                             "state": fr.boards.get(r, (0, {}))[1].get("state")}
                         for r, b in fleet.boards.items()},
              "telemetry_spread_ms": round(fr.spread_s * 1000, 1)})
        return 0
    finally:
        fleet.close(estop=False)


def cmd_fleet_estop(args) -> int:
    fleet = _fleet(args)
    try:
        r = fleet.estop()
        _out({"confirmed": r.confirmed, "write_skew_ms": round(r.write_skew_ms, 3),
              "boards": {k: (v.detail if v else None) for k, v in r.results.items()},
              "errors": r.errors})
        return 0 if r.confirmed else 2
    finally:
        fleet.close(estop=False)


def cmd_fleet_drive(args) -> int:
    from smc_core import kinematics as K
    fleet = _fleet(args)
    try:
        fleet.rearm()
        scale = fleet.drive(K.Twist(args.vx, args.vy, args.wz))
        _fleet_loop(fleet, args.seconds)
        t = fleet.measured_twist()
        fleet.stop()
        _fleet_loop(fleet, 0.3)
        _out({"scale": scale, "measured": t.__dict__ if t else None})
        return 0
    finally:
        fleet.close()


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(prog="smc", description=__doc__.split("\n\n")[0])
    ap.add_argument("--version", action="version", version=f"smc {full_version()}")
    sub = ap.add_subparsers(dest="cmd", required=True)

    def board_args(p):
        g = p.add_mutually_exclusive_group(required=True)
        g.add_argument("--port", help="serial port (COM6, /dev/ttyACM0) or tcp://HOST:PORT "
                                      "(virtual Nucleo)")
        g.add_argument("--sim", action="store_true", help="in-process fake board")
        p.add_argument("--baud", type=int, default=115200, help="rate the port opens at (the board boots at 115200)")
        p.add_argument("--link", type=int, default=0,
                       help="move the serial link to this rate after connecting (460800, 1000000, 2000000; "
                            "the board is handed back at 115200 on exit)")
        p.add_argument("--hb-timeout", type=int, default=BoardConfig.heartbeat_timeout_ms,
                       help="firmware watchdog ms")
        p.add_argument("--tlm-ms", type=int, default=BoardConfig.telemetry_period_ms,
                       help="telemetry push period ms")
        p.add_argument("--tlm-compact", action="store_true",
                       help="compact telemetry pushes (live fields only, ~170 B; allows 50 ms)")
        p.add_argument("--connect-timeout", type=float, default=5.0)

    sub.add_parser("ports").set_defaults(fn=cmd_ports)
    p = sub.add_parser("info"); board_args(p); p.set_defaults(fn=cmd_info)
    p = sub.add_parser("send"); board_args(p); p.add_argument("line", nargs="+")
    p.set_defaults(fn=cmd_send)
    p = sub.add_parser("telemetry"); board_args(p)
    p.add_argument("-n", type=int, default=0, help="stop after N lines (0 = until Ctrl-C)")
    p.set_defaults(fn=cmd_telemetry)
    p = sub.add_parser("estop"); board_args(p); p.set_defaults(fn=cmd_estop)
    p = sub.add_parser("flash", help="program a .bin through the NUCLEO drive with the session held open")
    board_args(p); p.add_argument("bin"); p.add_argument("--drive", help="mass-storage drive (default: auto-detect)")
    p.add_argument("--flash-timeout", type=float, default=40.0, help="seconds to wait for !RESET")
    p.set_defaults(fn=cmd_flash)
    t = sub.add_parser("test").add_subparsers(dest="tcmd", required=True)
    t.add_parser("list").set_defaults(fn=cmd_test_list)
    p = t.add_parser("run"); board_args(p)
    p.add_argument("names", nargs="+", help="scenario name(s)")
    p.add_argument("-p", "--param", action="append", help="key=value (JSON value), one scenario only")
    p.add_argument("--out", help="append results to this JSON-lines file")
    p.add_argument("--full", action="store_true", help="print the whole result")
    p.add_argument("--timeout", type=float, default=120.0)
    p.set_defaults(fn=cmd_test_run)
    m = sub.add_parser("motor").add_subparsers(dest="mcmd", required=True)
    m.add_parser("list").set_defaults(fn=cmd_motor_list)
    p = m.add_parser("show"); p.add_argument("name")
    p.add_argument("--protocol", type=int, default=D.PHYSICAL_PROTOCOL,
                   help="the board's protocol: 3 physical commands (default), 2 chip codes")
    p.set_defaults(fn=cmd_motor_show)
    p = m.add_parser("apply"); board_args(p); p.add_argument("name")
    p.add_argument("--save", action="store_true", help="DRV:CFG:SAVE afterwards")
    p.set_defaults(fn=cmd_motor_apply)
    dr = sub.add_parser("driver").add_subparsers(dest="dcmd", required=True)
    p = dr.add_parser("caps"); board_args(p); p.set_defaults(fn=cmd_driver_caps)
    p = dr.add_parser("show"); board_args(p); p.set_defaults(fn=cmd_driver_show)
    fl = sub.add_parser("fleet").add_subparsers(dest="fcmd", required=True)
    for name, fn in (("check", cmd_fleet_check), ("estop", cmd_fleet_estop), ("drive", cmd_fleet_drive)):
        p = fl.add_parser(name)
        p.add_argument("--config", required=True, help="fleet JSON (docs/multi_board/fleet.example.json)")
        if name == "drive":
            p.add_argument("--vx", type=float, default=0.0, help="m/s forward")
            p.add_argument("--vy", type=float, default=0.0, help="m/s left")
            p.add_argument("--wz", type=float, default=0.0, help="rad/s counter-clockwise")
            p.add_argument("--seconds", type=float, default=2.0)
        p.set_defaults(fn=fn)
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
