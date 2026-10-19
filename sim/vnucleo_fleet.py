#!/usr/bin/env python3
"""
Start one or more virtual Nucleo boards (sim/vnucleo), e.g. four for a mecanum base.

    python sim/vnucleo_fleet.py                  # one board on tcp://127.0.0.1:5761
    python sim/vnucleo_fleet.py --count 4        # FL, FR, RL, RR on ports 5761..5764
    python sim/vnucleo_fleet.py --count 4 --state sim_state   # keep config/identity on disk
    python sim/vnucleo_fleet.py --driver stepdir  # a generic STEP/DIR driver instead of the powerSTEP01

Each board gets serial VN00000<n>. Ctrl-C stops them all. To test "one board lost",
kill a single process (its PID is printed) and watch the client.
Build first:  cmake --preset vnucleo && cmake --build --preset vnucleo   (from sim/)
"""

import argparse
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def binary_path() -> str:
    exe = "vnucleo.exe" if os.name == "nt" else "vnucleo"
    return os.path.join(ROOT, "build-vnucleo", exe)


def start(count: int = 1, base_port: int = 5761, bind: str = "127.0.0.1", state: str = "",
          quiet: bool = False, driver: str = "powerstep"):
    exe = binary_path()
    if not os.path.exists(exe):
        raise SystemExit(f"{exe} not found: build it with `cmake --preset vnucleo && "
                         f"cmake --build --preset vnucleo` in sim/")
    if state:
        os.makedirs(state, exist_ok=True)
    procs = []
    for i in range(count):
        args = [exe, "--port", str(base_port + i), "--bind", bind, "--serial", "VN%06d" % (i + 1),
                "--driver", driver]
        if state:
            args += ["--state", state]
        out = subprocess.DEVNULL if quiet else None
        procs.append(subprocess.Popen(args, stdout=out, stderr=out))
    return procs


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--count", type=int, default=1)
    ap.add_argument("--base-port", type=int, default=5761)
    ap.add_argument("--bind", default="127.0.0.1")
    ap.add_argument("--state", default="", help="directory for persistent config and identity")
    ap.add_argument("--driver", default="powerstep", choices=("powerstep", "stepdir"),
                    help="motor driver board behind the firmware (default: the powerSTEP01)")
    a = ap.parse_args()
    procs = start(a.count, a.base_port, a.bind, a.state, driver=a.driver)
    time.sleep(0.5)
    for i, p in enumerate(procs):
        print(f"  board {i + 1}: tcp://{a.bind}:{a.base_port + i}  pid {p.pid}")
    print("Ctrl-C stops all boards.")
    try:
        while all(p.poll() is None for p in procs):
            time.sleep(0.5)
        print("a board exited; stopping the rest")
    except KeyboardInterrupt:
        pass
    finally:
        for p in procs:
            if p.poll() is None:
                p.terminate()
    return 0


if __name__ == "__main__":
    sys.exit(main())
