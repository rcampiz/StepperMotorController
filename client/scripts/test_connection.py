#!/usr/bin/env python3
"""Test the VcpTransport connection sequence (banner + PING probe + SET_FORMAT + GET_STATUS)."""

import sys
import logging
import time

logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s.%(msecs)03d [%(name)s] %(levelname)s: %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("test")

from transport import VcpTransport
from protocol import MotorClient, ResponseFormat

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM6"

transport = VcpTransport(port=PORT, baudrate=115200)
client = MotorClient(transport)
client.connect()

log.info("Port opened, reading banner...")
time.sleep(0.5)
banner_ok = False
for _ in range(10):
    line = transport.recv_line(timeout_ms=200)
    if line is None:
        break
    log.info("Banner: %s", line)
    banner_ok = True

if not banner_ok:
    log.warning("No banner - probing with PING...")
    transport.flush_input()
    transport.send_line("PING 0")
    # Read echo line
    probe = transport.recv_line(timeout_ms=2000)
    if probe is not None:
        log.info("Probe line 1: %r", probe)
        # Read PONG response line
        pong = transport.recv_line(timeout_ms=2000)
        if pong:
            log.info("Probe line 2: %r", pong)
    else:
        log.error("Firmware not responding!")
        client.disconnect()
        sys.exit(1)

# Fully drain any remaining bytes with a generous pause
time.sleep(0.1)
transport.flush_input()
time.sleep(0.05)

log.info("=== Testing SET_FORMAT JSON ===")
resp = client.set_format(ResponseFormat.JSON)
log.info("SET_FORMAT result: success=%s raw=%r", resp.success, resp.raw)

log.info("=== Testing GET_STATUS ===")
resp = client.get_status()
log.info("GET_STATUS result: success=%s", resp.success)
for key, val in resp.data.items():
    log.info("  %s = %s", key, val)

log.info("=== Testing PING 42 ===")
resp = client.ping(42)
log.info("PING result: success=%s data=%s", resp.success, resp.data)

# Test a few more commands
log.info("=== Testing MOTOR_DEBUG (multiline) ===")
resp = client.send_command_multiline("MOTOR_DEBUG")
log.info("MOTOR_DEBUG: success=%s lines=%d", resp.success,
         len(resp.data.get("lines", [])))
for line in resp.data.get("lines", []):
    log.info("  %s", line)

client.disconnect()
log.info("Done - all commands succeeded!")
