# Server review (firmware), items not already in `docs/diagram/03_open_issues.md`

Reviewed at commit `f7fdc17`, 2026-10-06. Lighter pass than the client review: the
architecture-level issues (SPI1 bitmap hold, serial Comms loop, unlocked controller calls,
stack estimate, hardware untested, two ESTOP holders, heap in SPIManager, …) are in
`03_open_issues.md` and are not repeated. Nothing was changed. Severity as in the client
review: **S1** safety/data loss, **S2** wrong behaviour, **S3** robustness, **S4** cosmetic.

| # | Sev | Finding | Location | Proposed fix |
|---|---|---|---|---|
| F1 | **S1** | **Long command handlers block everything on the Comms task** beyond the bitmap case already listed: `UI:FLASH:ERASE_ALL` (client allows 60 s), `UI:FLASH:UPLOAD[:RLE]` (full slot, ~13 s at 115200), `UI:FLASH:TEST`, `UI:FLASH:DUMP`, `DBG:TRACE:DUMP`. While one runs: no ESTOP parse, no heartbeat ack, and the 256-byte USART2 ring (`uart_hardware.hpp:189`) overflows after 22 ms of host traffic, so the next commands are silently corrupted (`RingBuffer::m_overflowCount` is never reported). | `L2_protocol/handlers/display_handlers.cpp` (flash handlers), `L5_board/uart/uart_hardware.hpp:34-75` | Phase 4 steps 1–3 cover the stop path; additionally: report `overflowCount` in `DIAG:STAT?.system`, chunk the erase (per-sector with a progress line), and move flash writes to the Display task via a job queue. |
| F2 | **S2** | **The host can disarm the watchdog at any time** (`SYST:HB:TIMEOUT 0`), and the client does exactly that during uploads. A watchdog that the protected party can switch off is not a watchdog. | `L2_protocol/handlers/safety_handlers.cpp:~58` `cmdSetHeartbeat`, `CommsService::setHeartbeatTimeout` | Refuse `0` while the motor is not in Hi-Z, or require `DBG:UNLOCK` for it; keep the 100–5000 ms clamp. |
| F3 | **S2** | **`DBG:MOTOR` and `CTRL:ENC:DBG?` are gated by `DBG:UNLOCK`** although both are read-only register dumps the GUI uses for its normal refresh; the gate (Phase 2, Q6) broke the client's driver panel. | `command_parser.cpp:~440` `dispatchDebug`, `L3_services/debug/debug_service.cpp:debugDispatch` | Exempt read-only commands (`MOTOR_DEBUG`, `ENC_DEBUG`, `PS01_DIAG` reads) from the lock; keep the lock for SPI_STOP/START, LCD_DISABLE, register writes. |
| F4 | **S2** | **`DIAG:STAT?` JSON may be truncated.** The format string is ~1.9 KB of source (`system_handlers.cpp:115-170`) rendered into `char buf[1024]`; `snprintf`'s return value is not checked, so an over-long render produces a JSON line without its closing braces and the client reports a parse error. Depends on field widths (negative 32-bit positions, large counts). | `system_handlers.cpp:115` | Check the return value; if `>= sizeof(buf)` respond with an error, and either raise the buffer (Comms stack is 2048 words) or split the snapshot into two commands. Add a host test that renders worst-case values. |
| F5 | S3 | **No JSON escaping.** `respondJsonOk/Err`, the event codec and the heartbeat/ping replies splice `msg`, `command` and `detail` into JSON with `%s` and no escaping; a message containing `"` or `\` yields invalid JSON. Today's messages are safe by inspection, which is fragile. | `command_parser.cpp:545-575`, `event_codec.cpp:100-130` | A small `jsonEscape(const char*, char*, size)` helper used by every `%s` inside a JSON literal; a host test that round-trips every `respondErr` string through a JSON parser. |
| F6 | S3 | **No error codes.** JSON errors always carry `"code":"ERROR"`; ASCII errors are free text. Clients cannot distinguish "unknown command" from "queue full" from "fault latched" without string matching. | `respondErr`, `respondStatus`, `statusToString` | Emit the `Harness::ServiceStatus` enumerator name as the code (`QUEUE_FULL`, `INVALID_STATE`, `FAULT`, `DBG_LOCKED`, `UNKNOWN_CMD`, `USAGE`) and document them in `PROTOCOL.md`. |
| F7 | S3 | **Character echo cannot be switched off**, and the banner is printed on every `CommsService::onStart`. Both are terminal conveniences that every machine client has to filter, and the echo doubles the uplink traffic. | `command_parser.cpp:process` (echo), `emitBanner` | `SYST:ECHO OFF` (or echo off automatically in JSON mode); keep the banner but prefix it with a machine-recognisable first line, e.g. `!RESET build=<hash>`, so clients can detect a reset. |
| F8 | S3 | **The banner says "Type 'HELP'" but no HELP exists** in L2. | `command_parser.cpp:587-593` | Implement `SYST:HELP?` from the dispatch tables (one line per entry) or drop the sentence. |
| F9 | S3 | **Telemetry push is ASCII-only** (`TELEM: pos=.. spd=..`) even in JSON mode and carries four fields, while `DIAG:STAT?` carries ~45; clients poll instead and saturate the link (client review §1.4). | `command_parser.cpp:613-628` `emitTelemetry` | In JSON mode emit `{"kind":"telemetry",...}` with the same schema as `DIAG:STAT?.data`, at a host-settable rate; then clients can stop polling. |
| F10 | S3 | **Dropped async events are silent.** `MotorEventManager::post` returns `false` when the 8-deep queue is full and nobody counts it; the event `seq` the client sees would simply skip. | `motor_event_service.cpp:67-75` | Count drops, report in `SYST:EVT:STAT?`, and let the sequence number reveal the gap (document that `seq` is monotonic per boot). |
| F11 | S3 | **Heap headroom is unverified.** `configTOTAL_HEAP_SIZE` is 24 KB; task stacks alone are 1024+2048+256+640 words = 15.9 KB, plus TCBs, two queues, six mutexes and the two `SPIHardware` objects. Roughly 4–5 KB remains; `freeHeap` is in telemetry but nobody has recorded it. | `FreeRTOSConfig.h:36` | Record `freeHeap` at boot on hardware; if below 2 KB move to static task/queue allocation (`configSUPPORT_STATIC_ALLOCATION`), which also retires item 10 of the open-issues list. |
| F12 | S3 | **Multi-line replies have no terminator** (`DBG:MOTOR`, `DRV:CFG?`, `CTRL:ENC?`, `CTRL:SYSID:DATA?`, trace dump): clients rely on inter-line timing, which breaks on slow or busy links. | the `IResponseSink` writers in L3 | End every multi-line reply with a final `OK` (ASCII) or a JSON envelope with a `lines` array; `CTRL:SYSID:DATA?` already has offset/count, add `total`. |
| F13 | S3 | **Four `-Wstringop-truncation` warnings in the release build** (`command_parser.cpp:88`, `terminal_screen.cpp:62`, `arch_screen.cpp:398`, `:753`): `strncpy` with the full buffer length, relying on a following manual terminator. Correct today, but the warning hides real ones. | listed sites | Use `snprintf("%.*s")` or a `copyBounded()` helper; make the release build `-Werror`-clean. |
| F14 | S3 | **`SYST:BAUD` is effectively unusable** with the stock ST-LINK V2-1 VCP (drops bytes above 115200 in bulk transfers, per the client's comment), yet the firmware advertises 230400–921600 and the client offers 9600–921600 in the combo box. | `system_handlers.cpp:237-290`, client `connection_panel.py:69` | Keep the command, document the adapter limitation, remove rates below 115200 from the client. With four boards on a Pi over separate USB CDC links, 921600 may work; verify per adapter. |
| F15 | S4 | **`DEV:ROLE?` is wired to `cmdGetDeviceId`** (`command_parser.cpp:~215`), so the role query returns the id reply. Harmless today, wrong for the four-board identity scheme. | `dispatchDevice` | Separate `cmdGetRole`. |
| F16 | S4 | ~~**Early-debug output and the protocol share USART2**~~ (closed 2026-10-18: `Platform::retireDebugOutput()` runs just before the scheduler starts; later `debugPrint*` calls are dropped and counted in `debugPrintsDropped()`; the fault hooks still print bare-metal) | `F_platform/hw/early_debug_board.cpp`, `system_init.cpp` | done |
| F17 | S4 | **No firmware/protocol version handshake.** `SYST:VER?` prints three free-text lines (`v0.3.0`, build date, "Protocol: ARM/START sync v1") while `*IDN?` and the banner say `v0.1.0`. | `system_handlers.cpp:185-200`, `emitBanner` | One source of truth: a `PROTOCOL_VERSION` constant reported by `*IDN?` as `vendor,model,serial,fw,proto`; clients refuse to run on an unknown `proto`. Needed before four boards and two client implementations exist. |

## What is fine

- Response, event and heartbeat formats are stable and documented well enough to generate
  `PROTOCOL.md` from the tables (`CmdEntry`/`CmdEntry0`/`PfxEntry` arrays in
  `command_parser.cpp`).
- Argument parsing is bounded (`ParsedCommand` fixed arrays, `strtoul`); unknown prefixes and
  suffixes are counted in `DispatchStats`.
- The dispatch tables are the only place commands are named; adding `SYST:HELP?` or a version
  handshake is local.

## Ranked

1. **F1** long handlers + 256-byte RX ring: silent corruption and no stop (ties into Phase 4).
2. **F2** host-disarmable watchdog.
3. **F3** read-only debug dumps behind the lock (client regression).
4. **F4** possible `DIAG:STAT?` truncation.
5. **F17** no version handshake before multi-board and multi-client.
6. **F9/F12/F6/F7** protocol ergonomics that `PROTOCOL.md` should fix in one pass.
