#!/usr/bin/env python3
"""Test raw serial commands WITHOUT VcpTransport to isolate timeout reconfiguration issue."""

import serial
import time
import sys

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM6"
BAUD = 115200

print(f"=== Raw Command Test for {PORT} @ {BAUD} ===\n")

ser = serial.Serial(
    port=PORT,
    baudrate=BAUD,
    bytesize=serial.EIGHTBITS,
    parity=serial.PARITY_NONE,
    stopbits=serial.STOPBITS_ONE,
    timeout=2.0,       # Fixed 2s timeout, never changed
    write_timeout=2.0,
)

def read_line(timeout_sec=2.0):
    """Read one line without changing serial.timeout."""
    buf = b""
    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        waiting = ser.in_waiting
        if waiting > 0:
            chunk = ser.read(waiting)
            buf += chunk
            if b"\n" in buf:
                line, _, rest = buf.partition(b"\n")
                # Push rest back? No, just return it
                return line.rstrip(b"\r").decode("utf-8", errors="replace"), rest
        else:
            b = ser.read(1)
            if b:
                buf += b
                if b == b"\n":
                    return buf.rstrip(b"\r\n").decode("utf-8", errors="replace"), b""
    if buf:
        return buf.decode("utf-8", errors="replace"), b""
    return None, b""


def send_and_read(cmd, expect_echo=True):
    """Send a command and read echo + response."""
    ser.reset_input_buffer()
    time.sleep(0.02)

    data = (cmd + "\r\n").encode()
    print(f"TX: {cmd}")
    ser.write(data)
    ser.flush()

    if expect_echo:
        echo, leftover = read_line()
        print(f"RX echo: {echo!r}")
    else:
        leftover = b""

    resp, leftover2 = read_line()
    print(f"RX resp: {resp!r}")
    return resp


# Drain any stale data
time.sleep(0.3)
stale = ser.read(ser.in_waiting) if ser.in_waiting else b""
if stale:
    print(f"Drained stale: {stale!r}\n")

# Test 1: PING (ASCII mode — firmware may be in JSON from previous test)
print("=== Test 1: PING 1 ===")
resp = send_and_read("PING 1")
print(f"  Length: {len(resp) if resp else 0}")
print()

# Test 2: SET_FORMAT JSON
print("=== Test 2: SET_FORMAT JSON ===")
resp = send_and_read("SET_FORMAT JSON")
print(f"  Length: {len(resp) if resp else 0}")
print()

# Test 3: GET_STATUS (now in JSON mode)
print("=== Test 3: GET_STATUS ===")
resp = send_and_read("GET_STATUS")
print(f"  Length: {len(resp) if resp else 0}")
print()

# Test 4: PING 42 (JSON mode)
print("=== Test 4: PING 42 ===")
resp = send_and_read("PING 42")
print(f"  Length: {len(resp) if resp else 0}")
print()

# Test 5: SET_FORMAT ASCII (restore)
print("=== Test 5: SET_FORMAT ASCII ===")
resp = send_and_read("SET_FORMAT ASCII")
print(f"  Length: {len(resp) if resp else 0}")
print()

ser.close()
print("Done.")
