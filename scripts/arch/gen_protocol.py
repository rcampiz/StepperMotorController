#!/usr/bin/env python3
"""
gen_protocol.py -- generate docs/PROTOCOL.md and docs/protocol.json from the L2 parser.

Everything a client needs is derived from the firmware source so it cannot drift:
  * the SCPI command catalogue: dispatch tables in L2_protocol/command_parser.cpp
    (prefix dispatchers, sub-namespace tables, strcmp special cases) plus the
    CTRL:ENC:FILT:<sub> sub-commands;
  * per command: usage strings, minimum argument count, ASCII/JSON replies and error
    messages, read from the handler bodies in L2_protocol/handlers/*.cpp;
  * status codes from harness/pins/dispatch_result.hpp, event types and wire formats from
    event_codec.cpp / async_event.hpp, telemetry fields from status_json.hpp and
    telemetry_types.hpp, framing limits and timing constants from the parser and services.
The hand-written sections (framing rules, timing, the 0x18 out-of-band stop, the checksum
proposal, the planned protocol-2 changes) live in this file as text so they are versioned
with the generator.

Usage: python scripts/arch/gen_protocol.py [--check]   (--check: exit 1 if outputs are stale)
"""
import json
import os
import re
import sys
from collections import OrderedDict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SERVER = os.path.join(ROOT, "server")
L2 = os.path.join(SERVER, "layers", "L2_protocol")
DOCS = os.path.join(ROOT, "docs")

def protocol_version():
    """PROTOCOL_VERSION from harness/pins/protocol_version.hpp (the firmware's single source)."""
    with open(os.path.join(SERVER, "harness", "pins", "protocol_version.hpp"), encoding="utf-8") as fh:
        return int(re.search(r'PROTOCOL_VERSION\s*=\s*(\d+)', fh.read()).group(1))


def read(path):
    with open(path, encoding="utf-8", errors="ignore") as fh:
        return fh.read()


def const(text, name, default=None):
    m = re.search(r'\b' + re.escape(name) + r'\s*=\s*([0-9]+)', text)
    return int(m.group(1)) if m else default


# ---------------------------------------------------------------------------
# 1. Command catalogue from command_parser.cpp
# ---------------------------------------------------------------------------
DISPATCH_RE = re.compile(r'void CommandParser::dispatch(\w+)\(')
ANY_ENTRY_RE = re.compile(r'\{"([^"]+)",\s*&CommandParser::(\w+)\}')
SUBPFX_RE = re.compile(r'strncmp\(suffix,\s*"([^"]+)",\s*\d+\)\s*==\s*0')
STRCMP_RE = re.compile(r'strcmp\(suffix,\s*"([^"]+)"\)\s*==\s*0')
PREFIX_TABLE_RE = re.compile(r'\{"([A-Z]+)",\s*&CommandParser::dispatch(\w+)\}')
ACTION_RE = re.compile(r'(cmd\w+)\(|debugDispatch\("(\w+)"|respondStatus\(m_dispatcher\.(\w+)\(')


def parse_catalogue():
    text = read(os.path.join(L2, "command_parser.cpp"))
    lines = text.splitlines()
    prefix_of = {d: p for p, d in PREFIX_TABLE_RE.findall(text)}   # dispatchMotion -> MOT

    commands = OrderedDict()

    def add(name, handler, note="", inline=""):
        if name not in commands:
            commands[name] = {"name": name, "handler": handler, "note": note, "inline": inline}

    # top-level (no colon) commands handled in process()
    for m in re.finditer(r'strcmp\(cmd\.cmd,\s*"([^"]+)"\)\s*==\s*0\)\s*\n?\s*(\w+)\(', text):
        add(m.group(1), m.group(2), "top-level (no prefix)")

    current = None
    sub = None          # sub-namespace prefix while inside an `if (strncmp(suffix, "X:", n) == 0) {` block
    sub_depth = 0       # brace depth of that block; 0 = not inside
    for i, line in enumerate(lines):
        dm = DISPATCH_RE.search(line)
        if dm:
            current = dm.group(1)
            sub, sub_depth = None, 0
            continue
        if current is None or current not in prefix_of:
            continue
        pfx = prefix_of[current]
        if sub is not None:
            sub_depth += line.count("{") - line.count("}")
            if sub_depth <= 0:
                sub, sub_depth = None, 0
                continue
        sm = SUBPFX_RE.search(line)
        if sm and sub is None:
            sub = sm.group(1)            # e.g. "FAULT:"
            sub_depth = line.count("{") - line.count("}")
            continue
        cm = STRCMP_RE.search(line)
        if cm:
            action = "\n".join(lines[i + 1:i + 5])
            hm = ACTION_RE.search(action)
            handler = next((g for g in (hm.groups() if hm else ()) if g), "inline")
            add(pfx + ":" + cm.group(1), handler, "special case in dispatcher", action)
            continue
        for em in ANY_ENTRY_RE.finditer(line):
            add(pfx + ":" + (sub or "") + em.group(1), em.group(2))

    # CTRL:ENC:FILT:<sub> sub-commands live in cmdEncFilterSub
    enc = read(os.path.join(L2, "handlers", "encoder_handlers.cpp"))
    body = enc[enc.index("void CommandParser::cmdEncFilterSub"):]
    for s in re.findall(r'strcmp\(sub,\s*"([A-Z]+)"\)\s*==\s*0', body):
        add("CTRL:ENC:FILT:" + s, "cmdEncFilterSub", "sub-command")
    return commands


# ---------------------------------------------------------------------------
# 2. Per-handler details from handlers/*.cpp
# ---------------------------------------------------------------------------
def handler_bodies():
    bodies = {}
    hdir = os.path.join(L2, "handlers")
    sources = [os.path.join(hdir, f) for f in sorted(os.listdir(hdir)) if f.endswith(".cpp")]
    sources.append(os.path.join(L2, "command_parser.cpp"))
    for path in sources:
        text = read(path)
        for m in re.finditer(r'void CommandParser::(cmd\w+)\(([^)]*)\)\s*\{', text):
            start = m.end()
            depth, i = 1, start
            while i < len(text) and depth:
                c = text[i]
                depth += (c == "{") - (c == "}")
                i += 1
            bodies[m.group(1)] = (os.path.basename(path), text[start:i - 1])
    return bodies


def describe(handler, bodies, sub=None, inline_text=""):
    if handler in bodies:
        fname, body = bodies[handler]
    else:
        fname, body = "", inline_text
    if sub is not None and handler in bodies:
        # cmdEncFilterSub: keep only this sub-command's branch
        parts = re.split(r'(?=if \(strcmp\(sub, ")', body)
        body = next((b for b in parts if b.startswith('if (strcmp(sub, "%s")' % sub)), body)
    # a Usage string ends at the closing quote or the end of the line (comments carry them too)
    usage = sorted(set(u.strip() for u in re.findall(r'Usage:\s*([^"\n]*)', body) if u.strip()))
    argc = [int(n) for n in re.findall(r'argCount\s*<\s*(\d+)', body)]
    ascii_ok = sorted(set(re.findall(r'respondOk\("([^"]*)"\)', body)))
    ascii_ok += ["<status> or " + m for m in sorted(set(re.findall(r'respondStatus\([^,]+,\s*"([^"]*)"\)', body)))]
    if re.search(r'respondOk\((buf|msg|line|out)\b', body):
        ascii_ok.append("<formatted text>")
    if re.search(r'm_transport\.println\(', body) and not re.search(r'println\(""\)', body):
        ascii_ok.append("<plain lines>")
    if "debugDispatch(" in body:
        ascii_ok.append("<debug service output>")
    begins = re.findall(r'm_sink\.begin\([^,]+,\s*(?:"([^"]+)"|(\w+))', body)
    if begins:   # F12: terminated multi-line reply
        ascii_ok = [a for a in ascii_ok if a != "<plain lines>"]
        ascii_ok.append("<lines> then OK <n>")
    json_cmds = []
    for name, var in begins:
        extra = sorted(set(re.findall(r'\\"(\w+)\\":', body)))
        json_cmds.append({"command": name or "<%s>" % var,
                          "keys": sorted(set(extra + ["count", "lines"]))})
    for jm in re.finditer(r'respondJsonOk\("([^"]+)",\s*(?:"((?:[^"\\]|\\.)*)"|(\w+))', body):
        keys = sorted(set(re.findall(r'\\"(\w+)\\":', jm.group(2) or "")))
        json_cmds.append({"command": jm.group(1), "keys": keys})
    if json_cmds and not any(j["keys"] for j in json_cmds):
        keys = sorted(set(re.findall(r'\\"(\w+)\\":', body)))
        for j in json_cmds:
            j["keys"] = keys
    errors = sorted(set(e for e in re.findall(r'respondErr\("([^"]*)"\)', body) if not e.startswith("Usage")))
    multiline = any(a.startswith("<lines>") for a in ascii_ok)
    binary = '"OK READY' in body
    # writes flash (motor config: internal sector 7, 128 KB, erase ~1-2 s with the CPU stalled)
    persists = bool(re.search(r'configSaveToFlash\(|configFactoryReset\(|\bsetDeviceId\(|'
                              r'\bsetRole\(', body))
    return {"file": fname, "usage": usage, "min_args": min(argc) if argc else 0,
            "ascii_ok": ascii_ok, "json": json_cmds, "errors": errors,
            "multiline": multiline, "binary": binary, "persists": persists}


# ---------------------------------------------------------------------------
# 3. Status codes, events, telemetry, limits
# ---------------------------------------------------------------------------
def status_codes():
    text = read(os.path.join(SERVER, "harness", "pins", "dispatch_result.hpp"))
    enum = re.search(r'enum class ServiceStatus[^{]*\{(.*?)\};', text, re.S).group(1)
    names = [n for n in re.findall(r'^\s*(\w+)\s*,?', enum, re.M) if n]
    blocks = re.split(r'inline const char\* status', text)
    strings = dict(re.findall(r'case ServiceStatus::(\w+):\s*return "([^"]*)"', blocks[1] if len(blocks) > 1 else text))
    codes = dict(re.findall(r'case ServiceStatus::(\w+):\s*return "([^"]*)"', blocks[2] if len(blocks) > 2 else ""))
    return [{"status": n, "message": strings.get(n, ""), "code": codes.get(n, n)} for n in names]


def events():
    ae = read(os.path.join(SERVER, "harness", "pins", "async_event.hpp"))
    enum = re.search(r'enum class EventType[^{]*\{(.*?)\};', ae, re.S).group(1)
    types = re.findall(r'^\s*(\w+)\s*=\s*(\d+)', enum, re.M)
    masks = re.findall(r'EVT_MASK_(\w+)\s*=\s*\(1\s*<<\s*(\d+)\)', ae)
    return [{"type": t, "id": int(v)} for t, v in types], [{"mask": m, "bit": int(b)} for m, b in masks]


def telemetry_fields():
    tt = read(os.path.join(SERVER, "harness", "pins", "telemetry_types.hpp"))
    out = OrderedDict()
    for sm in re.finditer(r'struct (\w+Telemetry)\s*\{(.*?)\};', tt, re.S):
        out[sm.group(1)] = re.findall(r'^\s*([\w:<>]+)\s+(\w+);', sm.group(2), re.M)
    sj = read(os.path.join(L2, "handlers", "system_handlers.cpp"))
    fmt = re.search(r'int renderStatusJson\(.*?return snprintf\((.*?)\);', sj, re.S).group(1)
    json_keys = re.findall(r'\\"(\w+)\\":', fmt)
    return out, json_keys


def limits():
    cp = read(os.path.join(L2, "command_parser.hpp"))
    ci = read(os.path.join(L2, "command_parser_internal.hpp"))
    cs = read(os.path.join(SERVER, "layers", "L3_services", "comms", "comms_service", "comms_service.hpp"))
    dh = read(os.path.join(L2, "handlers", "display_handlers.cpp"))
    sh = read(os.path.join(L2, "handlers", "system_handlers.cpp"))
    ms = read(os.path.join(SERVER, "layers", "L3_services", "motion", "motor_service", "motor_service.hpp"))
    uh = read(os.path.join(SERVER, "layers", "L5_board", "uart", "uart_hardware.hpp"))
    lim = {
        "line_max_chars": const(cp, "CMD_BUFFER_SIZE") - 1,
        "max_args": const(cp, "MAX_ARGS"),
        "arg_max_chars": int(re.search(r'char args\[MAX_ARGS\]\[(\d+)\]', cp).group(1)) - 1,
        "rx_ring_bytes": int(re.search(r'RingBuffer<(\d+)> m_rxBuffer', uh).group(1)),
        "telemetry_period_ms": const(cs, "TELEMETRY_PERIOD_MS"),
        "telemetry_period_min_ms": const(cs, "TELEMETRY_PERIOD_MIN_MS"),
        "telemetry_period_max_ms": const(cs, "TELEMETRY_PERIOD_MAX_MS"),
        "heartbeat_timeout_min_ms": const(cs, "HEARTBEAT_TIMEOUT_MIN_MS"),
        "heartbeat_timeout_max_ms": const(cs, "HEARTBEAT_TIMEOUT_MAX_MS"),
        "baud_revert_ms": int(re.search(r'm_baudRevertDeadline = m_dispatcher\.getTickMs\(\) \+ (\d+)', sh).group(1)),
        "binary_byte_timeout_ms": const(dh, "BYTE_TIMEOUT_MS"),
        "binary_chunk_bytes": const(dh, "CHUNK_SIZE"),
        "motor_queue_send_timeout_ms": const(ms, "SEND_TIMEOUT_MS"),
        "step_mode_wait_ms": const(ms, "STEP_MODE_WAIT_MS"),
    }
    for name in ("POS_MIN", "POS_MAX", "SPEED_MIN", "SPEED_MAX", "ACCEL_MIN", "ACCEL_MAX", "MAXSPD_MIN", "MAXSPD_MAX"):
        m = re.search(r'constexpr int32_t ' + name + r'\s*=\s*(-?\d+)', ci)
        if m:
            lim[name.lower()] = int(m.group(1))
    return lim


# ---------------------------------------------------------------------------
# 4. Documents
# ---------------------------------------------------------------------------
PREAMBLE = """# Host <-> board protocol

Generated by `scripts/arch/gen_protocol.py` from the L2 parser at the current commit. Do not edit
by hand: edit the firmware (or the generator's prose) and regenerate. `docs/protocol.json`
carries the same catalogue for code generation and conformance tests. Protocol version:
**{proto}**, read from `harness/pins/protocol_version.hpp` and reported by `*IDN?` (field 5),
`!RESET proto=<n>` and `SYST:VER?`. A client refuses a version it does not know.

## 1. Transport and framing

- Byte stream over the ST-LINK virtual COM port: USART2, 8N1, **115200** baud by default.
  `SYST:BAUD` can select 230400/460800/921600 (with the revert rule of section 4), but the
  stock ST-LINK V2-1 VCP drops bytes above 115200 during bulk transfers (bitmap and flash
  uploads), so clients stay at 115200 on that adapter and offer nothing below it (the
  firmware accepts no rate below 115200). A different USB-serial adapter, or several boards
  on separate CDC links, must be verified per adapter before a client raises the rate.
- A command is one line: printable ASCII (0x20..0x7E), terminated by CR, LF or CRLF.
  Maximum **{line_max} characters** per line, **{max_args} arguments** of at most **{arg_max}
  characters** each, separated by spaces. The command word is upper-cased by the parser;
  arguments are case-sensitive. Backspace (0x08/0x7F) erases the last buffered character.
- **Echo:** in ASCII mode the firmware echoes every printable byte it accepts and prints a
  newline when the line completes; `FMT JSON` switches echo off, `SYST:ECHO ON|OFF` overrides.
- **Receive ring:** {ring} bytes. If the ring overflows while a line is being received, the
  line is discarded when its terminator arrives and the reply is `ERROR RX_OVERFLOW`
  (never a parse of a line with missing bytes). Overflow counters are in
  `DIAG:STAT?` -> `system.rx_overflows` / `system.rx_overflow_lines`.
- Command form: `PREFIX:NAME [args]`. A word without a colon is either `*IDN?`, `FMT`, `FMT?`
  or a flat bring-up command routed to the debug service (most need `DBG:UNLOCK` first).
- Unknown prefix -> `ERROR Unknown namespace prefix`; unknown suffix -> `ERROR Unknown
  <PREFIX> command`.
- The usage strings quoted in the catalogue come from the firmware and still spell the
  pre-SCPI flat names (`MOVE`, `SET_HEARTBEAT`, ...); the command to type is the one in the
  first column.

## 2. Replies

Two formats, selected per session with `FMT ASCII` / `FMT JSON` (default ASCII; reset to
ASCII when the board resets).

| Kind | ASCII | JSON |
|---|---|---|
| success | `OK <message>` (message may be empty) | `{{"status":"ok","command":"<name>","data":{{...}}}}`; `data` is `{{"message":"..."}}` when the handler only has text |
| error | `ERROR <CODE> <message>` (split on the first two spaces) | `{{"status":"error","command":"<name>","code":"<CODE>","message":"<message>"}}` |
| multi-line data (`DBG:*` debug output incl. `DBG:MOTOR` and `CTRL:ENC:DBG?`, `DRV:CFG?`, `CTRL:SYSID:DATA?`, `DBG:TRACE:DUMP`, `SYST:VER?`, `UI:FLASH:DUMP`) | the lines, then `OK <n>` with n = number of lines sent (read until a line matching `^OK \\d+$`; an `ERROR` line can replace the whole reply) | one line `{{"status":"ok","command":"<name>","data":{{...,"lines":["..."],"count":<n>}}}}`; `CTRL:SYSID:DATA?` adds `offset`, `samples`, `total` (ASCII keeps its `SYSID_DATA <offset> <samples> <total>` header line), `DBG:TRACE:DUMP` adds `entries`, `UI:FLASH:DUMP` adds `slot`, `offset`, `len`. `CTRL:ENC?` was already a single-line reply. |
| binary READY (bitmap/flash upload) | `OK READY <bytes>` then the host sends exactly `<bytes>` raw bytes [+ CRC32 LE] then one final OK/ERROR | same |

`command` in JSON is the handler's canonical name (e.g. `GET_STATUS`, `HEARTBEAT`), not
necessarily the SCPI text typed; the catalogue below lists it.

`CODE` is one of: `UNKNOWN_CMD` (prefix or suffix not in the catalogue), `USAGE` (missing or
malformed arguments; the message is the usage string), `RX_OVERFLOW` (the line spanned a
receive-ring overflow), `DBG_LOCKED` (bring-up command before `DBG:UNLOCK`), `MOTOR_BUSY` /
`VERIFY_FAILED` (`DRV:STEP_MODE`), `ERROR` (a validation message without a finer code), or a
`ServiceStatus` name from the table below (the message is the text column):

{status_table}

## 3. Asynchronous lines (may arrive at any time between replies)

| Line | When | ASCII | JSON |
|---|---|---|---|
| Event | motor fault/stall edges, motion done, following supervisor | `!<TYPE> status=0x<hex4> [detail=<text>]` | `{{"kind":"event","type":"<TYPE>","seq":<n>,"ts_ms":<n>,"data":{{"status":<n>[,"detail":"<text>"]}}}}` |
| Telemetry push | every `SYST:TLM:RATE <ms>` ms ({tlm_min}..{tlm_max}, 0 = off; off at boot; the ack returns the clamped value and `min_ms`, the floor at the current baud rate: one worst-case status line may use at most 80 % of the link, 139 ms at 115200, so the Encoder task is never starved by the push) | `TELEM: pos=<i> spd=<u> enc=<i> vel=<i>` | `{{"kind":"telemetry","data":{{...}}}}` where `data` is byte-for-byte the `DIAG:STAT?` `data` object (section 8), so a client receiving pushes never polls |
| Joystick | board joystick in LOCAL UI mode | `EVENT JOY <LEFT\\|RIGHT\\|UP\\|DOWN\\|CENTER> pressed\\|released` | same |
| Banner | power-up and Comms start | 5 lines starting with a blank line and `====` | same |

Event types and `SYST:EVT:EN <mask>` bits:

{event_table}

`seq` (JSON events only; the ASCII event line carries no sequence number) is a per-boot
counter that starts at 1 and increments once per event line *sent*, so a gap between two
received events means the link lost a line. Events that never reach the wire are counted
instead: the motor event queue holds 8 entries and `SYST:EVT:STAT?` reports `sent`,
`lost_critical`, `lost_info`, `last_seq` and `depth` (ASCII: `EVENT_STATUS mask=<n> sent=<n>
lost_critical=<n> lost_info=<n> last_seq=<n> depth=<n>`). A client that must know whether it
missed a fault compares `last_seq` with the last `seq` it received and watches
`lost_critical`; both counters reset at boot (`!RESET`).

## 4. Timing rules

| Rule | Value |
|---|---|
| Heartbeat watchdog | `SYST:HB:TIMEOUT <ms>` arms it ({hb_min}..{hb_max} ms, 0 = off); the host must send `SYST:HB <seq>` more often than that. On timeout the firmware performs an emergency stop (latched: motion refused until `SYST:FAULT:CLEAR`). The ack carries `remaining_ms`. |
| Baud switch | after `SYST:BAUD <rate>` the firmware switches at once and **reverts to 115200 if no valid command arrives within {baud_revert} ms** at the new rate. |
| Binary transfers | after `OK READY`, each byte must arrive within {byte_timeout} ms or the transfer aborts with `ERROR Timeout at byte i/n`; data is consumed in {chunk}-byte chunks. The Comms task is inside the handler for the whole transfer: no other command (including `SYST:ESTOP`) is parsed until it ends. |
| Motor command queue | motion commands are queued to the Motor task with a {send_timeout} ms bound; `ERROR Queue full` if it does not fit. |
| Step-mode change | `DRV:STEP_MODE <m>` is executed by the Motor task; the reply waits up to {step_wait} ms for the readback. |
| ESTOP latch | `SYST:ESTOP` (and the watchdog) latch: Move/GoTo/Run/Home/Mark are refused (`ERROR Fault`) until `SYST:FAULT:CLEAR`. |

## 5. Out-of-band emergency stop (Phase 4 step 1, since 2026-10-17)

Byte **0x18 (CAN)** is reserved. It is recognised in the USART2 interrupt before the byte
reaches the ring (even when the ring is full or the Comms task is inside a handler), drives
the driver's STBY/RESET line low in the interrupt itself (bridges off within microseconds;
the chip's registers reset), and latches the stop. The Motor task then sends `HardHiZ` over
SPI and stops polling the chip; the pin stays low until `SYST:FAULT:CLEAR`, on which the
Motor task releases it, waits 2 ms and re-initialises the driver with the saved config. The
byte is never echoed or queued and produces no reply by itself; a `SYST:ESTOP` line sent
after it (the client's fast path) answers with `oob_pin_us` (0x18 arrival to pin low) and
`oob_hiz_us` (arrival to the SPI HardHiZ) beside `latency_us`, and the host confirms with
`DIAG:STAT?` (`state` = `ESTOP`, `motor.hi_z` = true). **Raw binary payloads** (`UI:DISP:BITMAP`,
`:RLE`, `UI:FLASH:UPLOAD`, `:RLE`) may contain 0x18, so the byte is ignored for the duration
of those receive loops; the base64 variants keep it active, which is why protocol 2 prefers
them.

## 6. Per-line checksum (proposal, protocol 2)

Optional NMEA-style suffix `*XX` where XX is the hex XOR of every byte between the start
of the line and the `*`. A board in multi-board mode (`DEV:ROLE` set) **requires** the
suffix on motion and safety commands and replies `ERROR CHECKSUM` otherwise; on the bench
it is optional. Replies never carry a checksum (the host can request `DIAG:STAT?` to
verify state instead).

## 7. Protocol 2: what changed since protocol 1 (one version bump, 2026-10-06)

1. `!RESET boot=<id> fw=<semver> proto=<n> build=<hash> rst=<cause>` is the first line at boot and on
   Comms start; the boot id is repeated in every heartbeat ack (`boot`).
2. `*IDN?` -> `SMC,NUCLEO-F401RE-IHM03A1,<serial>,<fw>,<proto>` from `protocol_version.hpp`.
3. Echo off by default in JSON mode; `SYST:ECHO ON|OFF`, `SYST:ECHO?`.
4. Error codes in both formats (`ERROR <CODE> <message>`; JSON `code`).
5. JSON-escaped strings everywhere (messages, command names, event details, lines).
6. JSON telemetry push with the `DIAG:STAT?` data object: `SYST:TLM:RATE <ms>` (0 = off),
   `SYST:TLM:RATE?`.
7. Multi-line replies end with `OK <n>` (ASCII) or are one envelope with a `lines` array
   (JSON); `CTRL:SYSID:DATA?` carries `offset`, `samples`, `total` and no longer sends `END`;
   `UI:FLASH:DUMP` no longer ends with `OK Dump complete`; a locked or unknown debug
   command always answers with an `ERROR` line.
8. Dropped-event counters in `SYST:EVT:STAT?` (`lost_critical`, `lost_info`) and the `seq`
   semantics of section 3.
9. `SYST:HELP?` generated from the dispatch tables (one line per command, terminated).
10. Usage strings (and `SYST:HELP?`) spell the SCPI command names.
11. `DIAG:STAT?.system` carries `rx_overflows` / `rx_overflow_lines`; a line that spanned an
    RX overflow is refused with `ERROR RX_OVERFLOW`.

Reserved for a later version (they land with Phase 4, fast ESTOP):

- Base64 flash upload (`UI:FLASH:UPLOAD:B64 <slot> <offset> <len> <b64>`) and
  `UI:DISP:BITMAP:RLE:B64`; the raw binary variants are removed (0x18 out-of-band ESTOP rule).
- `SYST:HB:TIMEOUT 0` refused unless the motor is in Hi-Z (`ERROR INVALID_STATE`).
- Per-line checksum (`CHECKSUM` error code).

## 8. `DIAG:STAT?` JSON fields

Top level `data`: {status_keys}.

Telemetry structures behind it (`harness/pins/telemetry_types.hpp`):

{telemetry_structs}

## 9. Command catalogue (generated)

Columns: command as typed; minimum arguments and the usage string(s) from the handler;
what a successful reply contains (ASCII message literals, `<status>` = the service status
message, JSON command name and keys); error messages the handler can produce besides the
usage error. "Handler" is the firmware function, for cross-reference.

"""


def md_escape(s):
    return s.replace("|", "\\|")


def build(commands, bodies):
    details = OrderedDict()
    for name, c in commands.items():
        sub = name.rsplit(":", 1)[1] if c.get("note") == "sub-command" else None
        d = describe(c["handler"], bodies, sub=sub, inline_text=c.get("inline", ""))
        d.update({k: v for k, v in c.items() if k != "inline"})
        details[name] = d

    codes = status_codes()
    ev_types, ev_masks = events()
    tstructs, status_keys = telemetry_fields()
    lim = limits()

    status_table = "| ServiceStatus | CODE | message |\n|---|---|---|\n" + "\n".join(
        "| `%s` | `%s` | %s |" % (c["status"], c["code"], md_escape(c["message"]) or "(not mapped)") for c in codes)
    alias = {"FAULT_CLEAR": "FAULT_CLR", "STALL_CLEAR": "STALL_CLR"}
    bit_of = {m["mask"]: m["bit"] for m in ev_masks}
    event_table = "| Type | id | `SYST:EVT:EN` mask bit |\n|---|---|---|\n" + "\n".join(
        "| `%s` | %d | %s |" % (t["type"], t["id"], bit_of.get(alias.get(t["type"], t["type"]), ""))
        for t in ev_types)
    telemetry_structs = "\n".join(
        "- `%s`: " % name + ", ".join("`%s` (%s)" % (f, t) for t, f in fields) for name, fields in tstructs.items())

    md = PREAMBLE.format(
        proto=protocol_version(), line_max=lim["line_max_chars"], max_args=lim["max_args"],
        arg_max=lim["arg_max_chars"], ring=lim["rx_ring_bytes"], status_table=status_table,
        tlm_min=lim["telemetry_period_min_ms"], tlm_max=lim["telemetry_period_max_ms"],
        event_table=event_table,
        hb_min=lim["heartbeat_timeout_min_ms"], hb_max=lim["heartbeat_timeout_max_ms"],
        baud_revert=lim["baud_revert_ms"], byte_timeout=lim["binary_byte_timeout_ms"],
        chunk=lim["binary_chunk_bytes"], send_timeout=lim["motor_queue_send_timeout_ms"],
        step_wait=lim["step_mode_wait_ms"], status_keys=", ".join("`%s`" % k for k in status_keys),
        telemetry_structs=telemetry_structs)

    groups = OrderedDict()
    for name in details:
        pfx = name.split(":")[0] if ":" in name else "(top level)"
        groups.setdefault(pfx, []).append(name)
    for pfx, names in groups.items():
        md += "### %s\n\n| Command | Min args | Usage | Reply | Errors | Handler |\n|---|---|---|---|---|---|\n" % pfx
        for name in names:
            d = details[name]
            usage = "<br>".join(md_escape(u) for u in d["usage"]) or "—"
            reply = []
            if d["ascii_ok"]:
                reply.append("ASCII: " + ", ".join(
                    ("`%s`" if m.startswith("<lines>") else "`OK %s`") % md_escape(m) for m in d["ascii_ok"]))
            for j in d["json"]:
                reply.append("JSON `%s`" % j["command"] + (" keys: " + ", ".join("`%s`" % k for k in j["keys"]) if j["keys"] else ""))
            errors = "<br>".join("`%s`" % md_escape(e) for e in d["errors"]) or "—"
            note = " (%s)" % d["note"] if d.get("note") else ""
            min_args = d["min_args"] if d["min_args"] is not None else "?"
            md += "| `%s`%s | %s | %s | %s | %s | `%s` %s |\n" % (
                name, note, min_args, usage, "; ".join(reply) or "`OK <status>`", errors,
                d["handler"], "(%s)" % d["file"] if d["file"] else "")
        md += "\n"

    js = {
        "protocol_version": protocol_version(),
        "generated_by": "scripts/arch/gen_protocol.py",
        "limits": lim,
        "status_codes": codes,
        "events": {"types": ev_types, "masks": ev_masks},
        "status_json_keys": status_keys,
        "telemetry_structs": {k: [{"type": t, "name": f} for t, f in v] for k, v in tstructs.items()},
        "commands": [
            {"name": n, "prefix": n.split(":")[0] if ":" in n else "", "handler": d["handler"],
             "file": d["file"], "min_args": d["min_args"], "usage": d["usage"],
             "ascii_ok": d["ascii_ok"], "json": d["json"], "errors": d["errors"], "note": d.get("note", ""),
             "multiline": d["multiline"], "binary": d["binary"], "persists": d["persists"]}
            for n, d in details.items()],
        "oob_estop_byte": 0x18,
    }
    return md, json.dumps(js, indent=2)


def help_table(commands, bodies):
    """One C string literal per command for SYST:HELP? (F8): `<SCPI name> <args>` where the
    args come from the handler's first Usage string (everything after its command token)."""
    out = ["// GENERATED by scripts/arch/gen_protocol.py from the dispatch tables (SYST:HELP?). Do not edit;",
           "// rerun the generator (the arch-check target fails when this file is stale)."]
    for name, c in commands.items():
        d = describe(c["handler"], bodies, inline_text=c.get("inline", ""))
        line = name
        if d["usage"]:
            u = max(d["usage"], key=len).replace("\\n", " ").strip()
            args = u.split(None, 1)[1] if len(u.split(None, 1)) > 1 else ""
            if args:
                line += " " + args
        out.append('"%s",' % line.replace("\\", "\\\\").replace('"', '\\"'))
    return "\n".join(out) + "\n"


def client_spec(js_text):
    """smc_core/protocol_spec.py: the catalogue as Python data for the host core."""
    js = json.loads(js_text)
    codes = sorted({c["code"] for c in js["status_codes"] if c.get("code")} |
                   {"USAGE", "UNKNOWN_CMD", "RX_OVERFLOW", "DBG_LOCKED", "VERIFY_FAILED", "ERROR"})
    out = ['"""',
           "GENERATED by scripts/arch/gen_protocol.py from the L2 parser tables (same source as",
           "docs/PROTOCOL.md and docs/protocol.json). Do not edit: rerun the generator; the",
           "arch-check target and the client tests fail when this file is stale.",
           '"""',
           "",
           "from dataclasses import dataclass",
           "from typing import Dict, Tuple",
           "",
           "PROTOCOL_VERSION = %d" % js["protocol_version"],
           "OOB_ESTOP_BYTE = 0x%02X" % js["oob_estop_byte"],
           "",
           "",
           "@dataclass(frozen=True)",
           "class CommandSpec:",
           "    name: str             # SCPI name as typed on the wire",
           "    handler: str          # firmware handler (for traceability)",
           "    min_args: int         # from the handler's argCount checks (a heuristic)",
           "    usage: str            # longest Usage string, SCPI spelling",
           "    json_command: str     # `command` field of the JSON reply ('' when the reply is text)",
           "    multiline: bool       # reply is lines + OK <n> (ASCII) / one envelope with `lines` (JSON)",
           "    binary: bool          # OK READY <n> handshake, then raw bytes",
           "    persists: bool = False  # writes flash: the motor config sector erase blocks the MCU ~1-2 s",
           "",
           "",
           "COMMANDS: Dict[str, CommandSpec] = {"]
    for c in js["commands"]:
        usage = max(c["usage"], key=len).replace("\\n", " ").strip() if c["usage"] else ""
        jc = c["json"][0]["command"] if c["json"] else ""
        out.append("    %r: CommandSpec(%r, %r, %d, %r, %r, %r, %r, %r)," % (
            c["name"], c["name"], c["handler"], c["min_args"], usage, jc, c["multiline"], c["binary"],
            c["persists"]))
    out.append("}")
    out.append("")
    out.append("ERROR_CODES: Tuple[str, ...] = (")
    out += ["    %r," % c for c in codes]
    out.append(")")
    out.append("")
    out.append("EVENT_TYPES: Tuple[str, ...] = (" + ", ".join("%r" % t["type"] for t in js["events"]["types"]) + ",)")
    out.append("EVENT_MASK_BITS: Dict[str, int] = {" + ", ".join("%r: %d" % (m["mask"], m["bit"]) for m in js["events"]["masks"]) + "}")
    out.append("STATUS_JSON_KEYS: Tuple[str, ...] = (")
    for i in range(0, len(js["status_json_keys"]), 6):
        out.append("    " + " ".join("%r," % k for k in js["status_json_keys"][i:i + 6]))
    out.append(")")
    out.append("LIMITS: Dict[str, int] = {")
    out += ["    %r: %r," % (k, v) for k, v in js["limits"].items()]
    out.append("}")
    return "\n".join(out) + "\n"


def main():
    commands = parse_catalogue()
    bodies = handler_bodies()
    md, js = build(commands, bodies)
    inc = help_table(commands, bodies)
    inc_path = os.path.join(SERVER, "layers", "L2_protocol", "help_table.inc")
    spec = client_spec(js)
    spec_path = os.path.join(ROOT, "client", "smc_core", "protocol_spec.py")
    md_path = os.path.join(DOCS, "PROTOCOL.md")
    js_path = os.path.join(DOCS, "protocol.json")
    if "--check" in sys.argv:
        stale = [p for p, new in ((md_path, md), (js_path, js), (inc_path, inc), (spec_path, spec))
                 if not os.path.exists(p) or read(p) != new]
        if stale:
            print("stale: " + ", ".join(os.path.relpath(p, ROOT) for p in stale))
            return 1
        print("protocol docs up to date")
        return 0
    with open(md_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(md)
    with open(js_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(js)
    with open(inc_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(inc)
    with open(spec_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(spec)
    print("wrote docs/PROTOCOL.md, docs/protocol.json, L2_protocol/help_table.inc and "
          "client/smc_core/protocol_spec.py: %d commands" % len(commands))
    return 0


if __name__ == "__main__":
    sys.exit(main())
