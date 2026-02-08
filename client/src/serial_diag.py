#!/usr/bin/env python3
"""Minimal serial diagnostic — bypasses GUI and VcpTransport entirely."""

import serial
import serial.tools.list_ports
import time
import sys

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM6"
BAUD = 115200

print(f"=== Serial Diagnostic for {PORT} @ {BAUD} ===\n")

# List all ports
print("Available ports:")
for p in serial.tools.list_ports.comports():
    print(f"  {p.device}: {p.description} [VID:PID={p.vid}:{p.pid}]")
print()

# Open port
print(f"Opening {PORT}...")
ser = serial.Serial(
    port=PORT,
    baudrate=BAUD,
    bytesize=serial.EIGHTBITS,
    parity=serial.PARITY_NONE,
    stopbits=serial.STOPBITS_ONE,
    timeout=2.0,
    write_timeout=2.0,
)
print(f"  Opened: {ser.is_open}")
print(f"  DTR={ser.dtr}, RTS={ser.rts}, CTS={ser.cts}, DSR={ser.dsr}")

# Try asserting DTR and RTS
ser.dtr = True
ser.rts = True
print(f"  After assert: DTR={ser.dtr}, RTS={ser.rts}, CTS={ser.cts}, DSR={ser.dsr}")
print()

# Step 1: Read any available data (banner?)
print("Step 1: Reading any available data (2s timeout)...")
time.sleep(0.3)
data = ser.read(1024)
if data:
    print(f"  Got {len(data)} bytes: {data!r}")
    print(f"  Decoded: {data.decode('utf-8', errors='replace')}")
else:
    print("  Nothing received (timeout)")
print()

# Step 2: Check in_waiting
waiting = ser.in_waiting
print(f"Step 2: Bytes waiting in buffer: {waiting}")
if waiting > 0:
    data = ser.read(waiting)
    print(f"  Read: {data!r}")
print()

# Step 3: Send a PING and read response
print("Step 3: Sending 'PING 1\\r\\n'...")
n = ser.write(b"PING 1\r\n")
print(f"  Wrote {n} bytes")
ser.flush()
print("  Flushed TX buffer")

print("  Reading response (2s timeout)...")
time.sleep(0.1)  # Brief delay for firmware to process
response = b""
start = time.time()
while (time.time() - start) < 2.0:
    waiting = ser.in_waiting
    if waiting > 0:
        chunk = ser.read(waiting)
        response += chunk
        print(f"  +{len(chunk)} bytes: {chunk!r}")
        if b"\n" in response:
            break
    else:
        b = ser.read(1)
        if b:
            response += b
            print(f"  +1 byte: {b!r}")
            if b == b"\n":
                break

if response:
    print(f"\n  Total: {len(response)} bytes")
    print(f"  Raw: {response!r}")
    print(f"  Decoded: {response.decode('utf-8', errors='replace')}")
else:
    print("\n  NO RESPONSE — firmware did not echo or reply")
print()

# Step 4: Try raw byte read
print("Step 4: Raw single-byte read (2s timeout)...")
ser.timeout = 2.0
b = ser.read(1)
if b:
    print(f"  Got: {b!r}")
else:
    print("  Nothing — read(1) timed out after 2s")

ser.close()
print("\nPort closed. Done.")
