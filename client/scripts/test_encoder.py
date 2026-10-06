#!/usr/bin/env python3
"""Test encoder data after respondOk JSON fix."""

import serial
import time
import json
import sys

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM6"
BAUD = 115200

print(f"=== Encoder Test for {PORT} @ {BAUD} ===\n")

ser = serial.Serial(
    port=PORT,
    baudrate=BAUD,
    bytesize=serial.EIGHTBITS,
    parity=serial.PARITY_NONE,
    stopbits=serial.STOPBITS_ONE,
    timeout=2.0,
    write_timeout=2.0,
)


def read_line(timeout_sec=2.0):
    buf = b""
    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        waiting = ser.in_waiting
        if waiting > 0:
            chunk = ser.read(waiting)
            buf += chunk
            if b"\n" in buf:
                line, _, rest = buf.partition(b"\n")
                return line.rstrip(b"\r").decode("utf-8", errors="replace")
        else:
            b = ser.read(1)
            if b:
                buf += b
                if b == b"\n":
                    return buf.rstrip(b"\r\n").decode("utf-8", errors="replace")
    if buf:
        return buf.decode("utf-8", errors="replace")
    return None


def send_cmd(cmd):
    ser.reset_input_buffer()
    time.sleep(0.02)
    ser.write((cmd + "\r\n").encode())
    ser.flush()
    echo = read_line()
    resp = read_line()
    return resp


# Drain boot banner
time.sleep(0.5)
if ser.in_waiting:
    stale = ser.read(ser.in_waiting)
    print(f"Drained: {stale!r}\n")

# Switch to JSON
print("=== SET_FORMAT JSON ===")
resp = send_cmd("SET_FORMAT JSON")
print(f"  {resp}\n")

# Check encoder status via GET_STATUS
print("=== GET_STATUS (encoder fields) ===")
resp = send_cmd("GET_STATUS")
try:
    data = json.loads(resp)["data"]
    print(f"  encoder_status: {data.get('encoder_status', '?')}")
    print(f"  encoder: {data.get('encoder', {})}")
    print(f"  mode: {data.get('mode', '?')}")
except Exception as e:
    print(f"  Parse error: {e}")
    print(f"  Raw: {resp}")

# ENC command (should now have JSON data thanks to fix)
print("\n=== ENC (JSON mode) ===")
resp = send_cmd("ENC")
print(f"  {resp}")
try:
    data = json.loads(resp)
    print(f"  Parsed data: {data.get('data', {})}")
except Exception as e:
    print(f"  Parse error: {e}")

# ENC_DEBUG for TIM2 register dump
print("\n=== ENC_DEBUG (TIM2 registers) ===")
resp = send_cmd("ENC_DEBUG")
print(f"  {resp}")

# Read ENC a few times to see if count changes
print("\n=== ENC x3 (checking for count changes) ===")
for i in range(3):
    time.sleep(0.5)
    resp = send_cmd("ENC")
    try:
        enc = json.loads(resp).get("data", {})
        print(f"  [{i+1}] count={enc.get('count', '?')} vel={enc.get('velocity', '?')}")
    except:
        print(f"  [{i+1}] {resp}")

# Restore ASCII
send_cmd("SET_FORMAT ASCII")
ser.close()
print("\nDone.")
