#!/usr/bin/env python3
"""
estop_baseline.py -- drive the three Phase 4 Step 0 ESTOP scenarios over the ST-LINK VCP.

The real latency is measured with a logic analyzer (see docs/bringup/PHASE4_STEP0_BASELINE.md);
this script only sequences the commands and prints host-side timestamps for cross-checking.

Usage:
  python scripts/estop_baseline.py COM5 [--baud 115200] [--case a|b|c|all]

Case a: idle, send SYST:ESTOP.
Case b: MOT:RUN 300 1, wait 1 s, send SYST:ESTOP.
Case c: UI:MODE REMOTE, UI:DISP:BITMAP 0 0 240 320, stream pixel bytes and inject "ESTOP\\n"
        after 8 KB (it is consumed as pixel data today), keep streaming to the end, then
        report when the firmware answered.
Every case ends with SYST:FAULT:CLEAR and MOT:STOP so the next case starts clean.
"""
import argparse
import sys
import time

try:
    import serial  # pyserial
except ImportError:
    sys.exit("pip install pyserial")


def send(port, line, wait=0.3):
    t0 = time.perf_counter()
    port.write((line + "\n").encode())
    port.flush()
    time.sleep(wait)
    reply = port.read(port.in_waiting or 1).decode(errors="replace").strip()
    print(f"{t0:12.6f}  > {line:<28} < {reply[:120]}")
    return reply


def case_a(port):
    print("--- (a) idle")
    send(port, "SYST:ESTOP")
    send(port, "DIAG:STAT?")
    send(port, "SYST:FAULT:CLEAR")


def case_b(port):
    print("--- (b) during RUN")
    send(port, "MOT:RUN 300 1", wait=1.0)
    send(port, "SYST:ESTOP")
    send(port, "DIAG:STAT?")
    send(port, "SYST:FAULT:CLEAR")
    send(port, "MOT:STOP")


def case_c(port, baud):
    print("--- (c) during a full-screen bitmap stream")
    send(port, "UI:MODE REMOTE")
    total = 240 * 320 * 2
    port.write(b"DISP_BITMAP 0 0 240 320\n")
    port.flush()
    time.sleep(0.2)
    print("  firmware:", port.read(port.in_waiting or 1).decode(errors="replace").strip())
    chunk = bytes([0x1F, 0x00]) * 32            # 64 bytes of one colour
    sent = 0
    t_start = time.perf_counter()
    t_estop = None
    while sent < total:
        port.write(chunk)
        sent += len(chunk)
        if t_estop is None and sent >= 8192:
            t_estop = time.perf_counter()
            port.write(b"ESTOP\n")               # becomes 6 pixel bytes today
            sent += 6
            print(f"  ESTOP injected at +{t_estop - t_start:.3f} s ({sent} bytes into the stream)")
    port.flush()
    t_end = time.perf_counter()
    time.sleep(0.5)
    reply = port.read(port.in_waiting or 1).decode(errors="replace").strip()
    print(f"  stream finished at +{t_end - t_start:.3f} s (expected ~{total * 10 / baud:.1f} s at {baud} baud)")
    print("  firmware:", reply[:200])
    send(port, "SYST:ESTOP")                           # now it is parsed
    send(port, "DIAG:STAT?")
    send(port, "SYST:FAULT:CLEAR")
    send(port, "UI:MODE LOCAL")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--case", default="all", choices=["a", "b", "c", "all"])
    args = ap.parse_args()
    with serial.Serial(args.port, args.baud, timeout=0.2) as port:
        time.sleep(0.3)
        port.reset_input_buffer()
        send(port, "DIAG:PING 1")
        if args.case in ("a", "all"):
            case_a(port)
        if args.case in ("b", "all"):
            case_b(port)
        if args.case in ("c", "all"):
            case_c(port, args.baud)


if __name__ == "__main__":
    main()
