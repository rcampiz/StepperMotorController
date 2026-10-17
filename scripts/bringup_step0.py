#!/usr/bin/env python3
"""
bringup_step0.py -- the serial part of the Phase 4 Step 0 bring-up list
(docs/bringup/PHASE4_STEP0_BASELINE.md section 3, items 2 to 7) on the ST-LINK VCP.

  python scripts/bringup_step0.py COM5 [--no-motor] [--no-debug]

Prints every command, its reply and a PASS/FAIL verdict, then a results block to paste
into the baseline document. --no-motor skips the RUN/ESTOP item (3, 4); --no-debug skips
item 6 (DBG:UNLOCK, SPI_STOP/START, LCD_DISABLE: LCD_DISABLE blanks the LCD until a reset,
so read the Task Monitor (item 8) before running it, or reset afterwards).
Item 1 (boot banner) is captured when flashing; item 8 (Task Monitor) is on the LCD.
"""

import argparse
import json
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pip install pyserial")

RESULTS = []


def send(port, line, wait=0.4, expect=None, label=None):
    port.reset_input_buffer()
    port.write((line + "\n").encode())
    port.flush()
    time.sleep(wait)
    reply = port.read(port.in_waiting or 1).decode(errors="replace")
    # drop the echo line
    lines = [l for l in reply.splitlines() if l.strip() and l.strip() != line]
    text = "\n".join(lines)
    ok = None
    if expect is not None:
        ok = bool(re.search(expect, text))
        RESULTS.append((label or line, ok, text.replace("\n", " | ")[:160]))
    tag = "" if ok is None else ("  PASS" if ok else "  FAIL")
    print(f"> {line:<28} < {text.replace(chr(10), ' | ')[:150]}{tag}")
    return text


def status_json(port):
    text = send(port, "DIAG:STAT?", wait=0.5)
    for l in text.splitlines():
        if l.startswith("{"):
            return json.loads(l).get("data", {})
    return {}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--no-motor", action="store_true")
    ap.add_argument("--no-debug", action="store_true")
    a = ap.parse_args()
    with serial.Serial(a.port, a.baud, timeout=0.2) as port:
        time.sleep(0.3)
        send(port, "FMT ASCII")
        print("\n# item 2: ping and status")
        send(port, "DIAG:PING 1", expect=r"PONG 1 ", label="2 DIAG:PING")
        send(port, "DIAG:STAT?", expect=r"^STATUS ", label="2 DIAG:STAT? ASCII")
        send(port, "SYST:VER?", wait=0.6)
        send(port, "FMT JSON", expect=r'"format":"JSON"', label="2 FMT JSON")
        st = status_json(port)
        RESULTS.append(("2 DIAG:STAT? JSON", bool(st), f"state={st.get('state')} hiZ={st.get('motor', {}).get('hi_z')} uvlo={st.get('motor', {}).get('uvlo')} enc={st.get('encoder_status')}"))
        print(f"  state={st.get('state')} motor={st.get('motor')}")

        if not a.no_motor:
            print("\n# items 3 and 4: run, ESTOP, refused, clear, run, stop")
            send(port, "SYST:FAULT:CLEAR")
            send(port, "MOT:RUN 300 1", wait=1.0, expect=r'"status":"ok"', label="3 MOT:RUN accepted")
            st = status_json(port)
            # BUSY is low only while a command executes; a constant-speed RUN shows speed > 0
            # and bridges on (not Hi-Z).
            speed, hiz = st.get("motor", {}).get("speed", 0), st.get("motor", {}).get("hi_z")
            RESULTS.append(("3 running: speed > 0 & not hiZ", speed > 0 and not hiz, f"speed={speed} hi_z={hiz} busy={st.get('motor', {}).get('busy')}"))
            t0 = time.perf_counter()
            send(port, "SYST:ESTOP", expect=r'"status":"ok"', label="4 SYST:ESTOP ok")
            t_ack = time.perf_counter() - t0
            st = status_json(port)
            hiz, state = st.get("motor", {}).get("hi_z"), st.get("state")
            RESULTS.append(("4 after ESTOP: hiZ", bool(hiz), f"state={state} hi_z={hiz} (host saw the ack {t_ack*1000:.0f} ms after the write incl. 0.4 s wait)"))
            send(port, "MOT:RUN 300 1", expect=r'"status":"error"', label="4 RUN refused while latched")
            send(port, "SYST:FAULT:CLEAR", expect=r'"status":"ok"', label="4 SYST:FAULT:CLEAR ok")
            send(port, "MOT:RUN 300 1", wait=0.8, expect=r'"status":"ok"', label="4 RUN after CLEAR ok")
            send(port, "MOT:STOP", wait=0.8)
            send(port, "MOT:DIS")

        print("\n# item 5: step-mode readback 4, 7, restore")
        cur = send(port, "DRV:STEP_MODE?")
        m = re.search(r'"step_mode":(\d+)', cur)
        original = int(m.group(1)) if m else None
        for mode in (4, 7):
            send(port, f"DRV:CFG:STEPMODE {mode}", wait=0.6)
            send(port, "DRV:CFG:APPLY", wait=2.5)              # writes flash: ~1-2 s
            rb = send(port, "DRV:STEP_MODE?", expect=rf'"step_mode":{mode}\b', label=f"5 step mode {mode} readback")
        if original is not None:
            send(port, f"DRV:CFG:STEPMODE {original}", wait=0.6)
            send(port, "DRV:CFG:APPLY", wait=2.5)
            send(port, "DRV:STEP_MODE?", expect=rf'"step_mode":{original}\b', label=f"5 step mode restored to {original}")

        print("\n# item 7: encoder")
        send(port, "CTRL:ENC:STAT?")
        enc = send(port, "CTRL:ENC?", expect=r'"count":', label="7 CTRL:ENC? replies")
        st = status_json(port)
        e = st.get("encoder", {})
        RESULTS.append(("7 encoder snapshot", "count" in e, f"count={e.get('count')} vel={e.get('velocity')} index_seen={e.get('index_seen')} rev={e.get('revolutions')} period_us={e.get('index_period_us')}"))
        print("  (turn the shaft through the index now if you want index_seen; re-run item 7 then)")

        if not a.no_debug:
            print("\n# item 6: debug gate, SPI pause/resume, LCD disable")
            send(port, "DBG:UNLOCK", expect=r'"status":"ok"', label="6 DBG:UNLOCK")
            send(port, "SPI_STOP", wait=0.6, expect=r"paused", label="6 SPI_STOP pauses the Motor task")
            s1 = status_json(port).get("tick"); time.sleep(0.4); s2 = status_json(port).get("tick")
            print(f"  ticks while paused: {s1} -> {s2} (telemetry tick still advances; motor telemetry should not)")
            send(port, "SPI_START", wait=0.6, expect=r"resumed", label="6 SPI_START resumes")
            send(port, "LCD_DISABLE", wait=1.5, expect=r"reinitialized", label="6 LCD_DISABLE (display off, driver reinit)")
            send(port, "DBG:LOCK", expect=r'"status":"ok"', label="6 DBG:LOCK")
            send(port, "SPI_STOP", expect=r'"status":"error"', label="6 locked again: SPI_STOP refused")

        send(port, "FMT ASCII")

    print("\n==== RESULTS (paste into PHASE4_STEP0_BASELINE.md section 5) ====")
    for label, ok, detail in RESULTS:
        print(f"| {label} | {'PASS' if ok else 'FAIL'} | {detail} |")
    return 0 if all(ok for _, ok, _ in RESULTS) else 1


if __name__ == "__main__":
    sys.exit(main())
