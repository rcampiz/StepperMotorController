# Client review (host GUI, Python)

Reviewed at commit `f7fdc17`, 2026-10-06. Firmware reference: the SCPI tables in
`server/layers/L2_protocol/command_parser.cpp` at the same commit. Nothing was changed; every
finding has a location and a proposed fix. Severity: **S1** safety or data loss, **S2**
wrong behaviour the user will hit, **S3** robustness/maintenance, **S4** cosmetic.

## 0. Where it lives and how it is built

`client/` (12,300 lines of Python, last touched 2026-10-05, no README, no packaging):

```
client/requirements.txt          PySide6, pyqtgraph, numpy, scipy, pyserial, pylink-square, Pillow
client/src/main.py               CLI + GUI launcher (argparse)
client/src/gui_main.py           second GUI launcher (duplicate of main.py's launch_gui)
client/src/transport/            ITransport (abstract), VcpTransport (pyserial), RttTransport (pylink)
client/src/protocol/client.py    MotorClient: line classification, send/recv, JSON/ASCII parsing,
                                 ~40 command helpers, raw bitmap/flash streaming, baud switch
client/src/protocol/display.py   DisplayClient: remote-LCD helpers (text/rect/line/B64 bitmap)
client/src/gui/serial_worker.py  SerialWorker (QObject on a QThread): command FIFO, coalesced
                                 live/indicator commands, heartbeat producer, 10 Hz DIAG:STAT? poll
client/src/gui/main_window.py    MainWindow: connect sequence, wiring of 10 panels, command routing,
                                 bitmap band transfer, flash upload, close handling
client/src/gui/*_panel.py        motor, motion, telemetry, driver, protection, stepmode, display,
                                 graph, sysid (1,546 lines, scipy curve fitting), connection
client/src/gui/rle_codec.py, image_loader.py, arrow_generator.py   image pipeline
client/src/controller/, utils/   empty packages
client/src/test_*.py, serial_diag.py   five ad-hoc scripts hard-coded to COM6, legacy command names
client/tests/test_transport.py   9 pytest cases, all mocked, transport layer only
```

Threading model: the UI thread owns the widgets; one `SerialWorker` thread owns the serial
port after connect and runs a polling loop (`run()`, 10 ms sleep) that, per iteration, sends
one coalesced live command, one FIFO command, one coalesced indicator command, a heartbeat if
due, drains events, and a `DIAG:STAT?` poll if due. Responses come back as Qt signals.

## 1. Protocol correctness

### 1.1 Commands the client sends that the firmware does not have

| Client call | Where | Firmware | Effect |
|---|---|---|---|
| `SYST:HELP?` | `protocol/client.py:~500` `get_help` | no `HELP` anywhere in L2 (the boot banner still says "Type 'HELP'") | `ERROR Unknown SYST command` |
| `DBG:MOTOR` | `serial_worker.py:376` `queue_refresh` | exists, but routes to `DebugService::debugDispatch("MOTOR_DEBUG")`, which is **locked until `DBG:UNLOCK`** since Phase 2 | every connect and every "Refresh" returns `ERROR DBG locked (send DBG:UNLOCK)`; the driver panel's MOTOR_DEBUG view is dead. **S2, regression introduced by the firmware's debug gate.** |
| `CTRL:ENC:DBG?` | not sent by the GUI (label only) | gated the same way | — |
| legacy flat names `PING`, `GET_STATUS`, `SET_FORMAT`, `HEARTBEAT` … | `src/test_*.py`, `serial_diag.py` | parser is SCPI-only; flat names fall through to the debug gate | the five scripts cannot work against this firmware |

### 1.2 Firmware commands the client never uses

`SYNC:QUEUE/ARM/START/START:AT/CLEAR` (the synchronised-start path the multi-board design
depends on), `DEV:ID`, `DEV:ROLE`, `DEV:ID?`, `DEV:ROLE?`, `CTRL:FOLLOW:THRESH[?]`,
`CTRL:FOLLOW?`, `CTRL:FOLLOW:SAVE/CLEAR`, `CTRL:ENC:STAT?`, `CTRL:ENC:ZERO`, `CTRL:MODE?`,
`DRV:CFG:SAVE/LOAD/RESET`, `UI:FLASH:DUMP`, `DBG:UNLOCK/LOCK`, `DBG:TRACE:DUMP/RESET`,
`SYST:FAULT:STAT?`, `SYST:EVT:STAT?`, `*IDN?`, and all flat bring-up commands. `SYST:TICK?`
and `SYST:BAUD` exist as helpers but no GUI path calls them.

### 1.3 Wire format: what matches and what does not

| Item | Firmware | Client | Verdict |
|---|---|---|---|
| Success / error, ASCII | `OK <msg>` / `ERROR <msg>` | `_parse_ascii_response` | ok |
| Success / error, JSON | `{"status":"ok","command":..,"data":{..}}` / `{"status":"error",..,"code":"ERROR","message":..}` | `_parse_json_response` | ok, but `code` is always the literal `ERROR`: the client's `CommandError.code` carries no information |
| Echo | every printable byte is echoed, newline echoed on CR/LF | `_classify_line` drops a line equal to the command | ok only for exact match; a command with trailing whitespace or a line split by the 50 ms native timeout is mis-classified as a RESPONSE |
| Events, ASCII | `!FAULT status=0x1234 detail=OCD` | `startswith("!")` | ok |
| Events, JSON | `{"kind":"event","type":..,"seq":..,"ts_ms":..,"data":{..}}` | `'"kind":"event"' in line` | ok; **`seq` is parsed and discarded** (`serial_worker.py:303`), so lost events are invisible |
| Telemetry push | `TELEM: pos=.. spd=.. enc=.. vel=..` (ASCII even in JSON mode) | classified TELEMETRY and **dropped**; the GUI polls `DIAG:STAT?` instead | wasteful but consistent |
| Joystick | `EVENT JOY <dir> pressed/released` | classified TELEMETRY and dropped | the GUI never sees joystick events |
| Heartbeat ack | JSON `data:{seq,mcu_tick,remaining_ms}`; ASCII `HEARTBEAT_ACK a b c` | reads the three JSON keys | ok in JSON; in ASCII mode `resp.data` is empty and the link-health display is blank |
| `DIAG:STAT?` | ~1 KB JSON (`motor`, `encoder`, `control`, `heartbeat`, `system`) | `telemetry_panel.py:636+` reads `motor.position/speed/busy/hi_z/direction/mot_status` | keys match |
| Boot banner (5 lines) | printed at power-up and by `CommsService::onStart` | handled only in the connect sequence | **a mid-session reset is invisible**: banner lines arrive as RESPONSE frames and become the "response" of whatever command is in flight; the board is back in ASCII mode with the watchdog disarmed and the client keeps going (S1, see §3) |
| Baud switch | `SYST:BAUD n` then revert if no valid command before a deadline | `set_baud` verifies with `DIAG:PING 99` | logic ok; GUI deliberately never calls it (ST-LINK VCP drops bytes above 115200 in bulk) |
| Multi-line replies (`DBG:MOTOR`, `DRV:CFG?`, `CTRL:ENC?`, `CTRL:SYSID:DATA?`) | plain lines via `IResponseSink`, no terminator | `send_command_multiline` reads until a 250 ms gap or `max_lines` (20, 50 for sysid) | works by timing only; a slow link truncates silently; `CTRL:SYSID:DATA?` chunks must stay under 50 lines |

### 1.4 Link budget (why everything feels slow)

`SerialWorker._poll` sends `DIAG:STAT?` every 100 ms. The reply is up to 1,024 bytes
(`system_handlers.cpp:115`, `char buf[1024]`), about 90 ms on the wire at 115200 baud, plus
the echo of every command and a `SYST:HB n` round trip every 50 ms. The link runs near
saturation, so every FIFO command, including ESTOP, waits for the poll in flight.
`client.py:_recv_response` also gives up after 16 non-response lines, which `TELEM:` at 10 Hz
plus events can exceed inside the 2 s timeout.

## 2. Robustness

| # | Sev | Finding | Location | Proposed fix |
|---|---|---|---|---|
| R1 | S2 | **Unplug is never detected.** `_poll` emits `connection_error` only when `is_connected()` is false, but pyserial keeps `is_open` true after the device vanishes; every 100 ms a warning is logged and the GUI stays "Connected". | `serial_worker.py:280-293`, `vcp.py:is_connected` | Treat any `SerialException` from read/write as link loss: close the port, emit `connection_error`, stop the loop. |
| R2 | S2 | **No reconnect.** After loss the user must disconnect and reconnect by hand; the watchdog state on the board is unknown. | `main_window.py:964` | Auto-retry open every 1 s while the port is listed; on success re-run the post-connect setup (format, `SYST:FAULT:CLEAR`, `SYST:HB:TIMEOUT`, `SYST:EVT:EN`). |
| R3 | S1 | **Mid-session board reset is invisible** (banner as RESPONSE, format back to ASCII, watchdog disarmed, event mask reset). | `client.py:_classify_line` | Classify lines starting with `====` or `Stepper Motor Controller` as a RESET frame; on reset emit a signal, re-run setup, and latch "board reset" in the UI. |
| R4 | S3 | `recv_line` returns a **partial line** at the deadline as if complete, which then parses as a response. | `vcp.py:recv_line` | Keep the partial bytes in the buffer and return `None`. |
| R5 | S3 | **UI thread blocks during connect**: `time.sleep(0.5)`, up to 10 × 200 ms banner reads, a 2 s probe, 3 × 300 ms format retries; the window freezes for up to ~6 s. | `main_window.py:294-345` | Run the connect sequence in the worker (`connect` job) and report through signals. |
| R6 | S3 | `closeEvent` queues `SYST:HB:TIMEOUT 0` and `SYST:ESTOP` through the FIFO, sleeps 200 ms on the UI thread, then closes the port; if the worker is inside a long callable neither command is sent. | `main_window.py:1107-1115` | Send both directly on the transport (see S-1), then stop the worker, then close. |
| R7 | S3 | `SerialThread.stop()` waits 2 s; a worker blocked in a flash upload (13 s) or `ERASE_ALL` (60 s) is abandoned with the port open. | `serial_worker.py:357-365` | Give long jobs a cancel flag checked per chunk; join without timeout after cancel. |
| R8 | S3 | **Exceptions in a queued command are swallowed** (`response_received(tag, cmd, None)`); the UI shows nothing for a failed apply. | `serial_worker.py:221-232` | Emit an error signal with the exception text; show it in the status bar. |
| R9 | S3 | `set_baud` reaches into transport privates (`_serial.baudrate`, `_baudrate`) and sleeps on whatever thread calls it. | `client.py:~560` | Add `ITransport.set_baudrate()`; run from the worker. |
| R10 | S4 | Cross-thread plain attributes: `_pending_live`, `_pending_indicator`, `_heartbeat_interval` (read on the UI thread at `main_window.py:664`). Benign under the GIL, but undocumented. | `serial_worker.py` | Guard with a lock or make them `queue.Queue(maxsize=1)`. |
| R11 | S3 | `_recv_response` `max_skip = 16` can starve a response behind telemetry/event bursts. | `client.py:~140` | Loop on the deadline, not on a line count. |
| R12 | S4 | `VcpTransport.detect_ports` treats every VID 0x0483 device as the board; with four boards (multi-board plan) auto-detect picks `detected[0]`. | `vcp.py:detect_ports` | Return (port, serial number) pairs; never auto-pick when more than one is present. |

## 3. Safety

| # | Sev | Finding | Location | Proposed fix |
|---|---|---|---|---|
| S-1 | **S1** | **ESTOP goes through the FIFO.** `motor_panel._on_estop` emits `SYST:ESTOP`, `MainWindow._send_command` puts it in the worker queue, and the worker sends one FIFO entry per loop iteration *after* the command currently executing. Ahead of it can be: the three refresh commands (each up to 20 × 250 ms), a bitmap band (`disp_bitmap_rle` callable, ~1 s per band at 115200), a flash upload (one callable, up to 13 s), `ERASE_ALL` (up to 60 s). Measured bound today: **tens of seconds**. | `motor_panel.py:776`, `main_window.py:486-506`, `serial_worker.py:175-190` | Dedicated ESTOP path that bypasses the queue: write directly on the transport from the UI thread (`transport.send(b"SYST:ESTOP\r\n")` today; `b"\x18"` after Phase 4), with its own lock, and set a worker flag that drops everything queued. pyserial writes are safe to call from a second thread. |
| S-2 | **S1** | **Heartbeat does not prove the UI is alive.** The producer lives in the worker loop (`_send_heartbeat`), so a frozen or deadlocked UI thread keeps the watchdog fed indefinitely. This is exactly the failure the watchdog exists for. | `serial_worker.py:192-197` | Dead-man token: a `QTimer` on the UI thread stamps `ui_alive_at`; the worker sends a heartbeat only if the stamp is younger than one period. Add a second token from the control source (joystick/keyboard handler) when driving. |
| S-3 | **S1** | **The client disarms the watchdog** (`SYST:HB:TIMEOUT 0`) around flash uploads because the upload blocks the worker; a stalled upload leaves the motor unprotected until the `finally`. | `main_window.py:667-679` | Never disarm from the client. Send heartbeats from inside the upload loop (per chunk), or move uploads to a second worker. Firmware side: Phase 4 step 3 moves the timeout check off the Comms task, but the client must still feed it. |
| S-4 | S2 | `heartbeat_timeout` is declared but **never emitted**; `_on_heartbeat_timeout` never runs; missed acks only increment a counter. | `serial_worker.py:53`, `main_window.py:1057` | Emit after N consecutive misses; show a red banner; treat as link loss. |
| S-5 | S2 | Watchdog timeout is `max(2000, 6 × period)`; with the default 50 ms period that is 2 s of uncontrolled motion after the client dies. | `main_window.py:430` | Make the timeout a visible setting with a 300–500 ms default for bench use; the firmware clamps to 100–5000 ms. |
| S-6 | S2 | After a board reset the watchdog is disarmed and nothing re-arms it (see R3). | — | Part of the reset-detection fix. |
| S-7 | S3 | The `SYST:HB` ack `remaining_ms` is displayed but not acted upon; a shrinking `remaining_ms` (worker falling behind) gives no warning. | `telemetry_panel.py:550-566` | Warn when `remaining_ms < timeout/2`. |

## 4. Phase 4 compatibility: everything that must change

Phase 4 reserves byte **0x18** as an out-of-band ESTOP recognised in the USART2 ISR, and
forbids 0x18 anywhere in host traffic. Today the client can emit 0x18 in four places:

| Path | Why 0x18 can appear | Change |
|---|---|---|
| `MotorClient.disp_bitmap` (raw `UI:DISP:BITMAP`) | raw RGB565 pixel bytes | delete; firmware removes the raw variant |
| `MotorClient.disp_bitmap_rle` (`UI:DISP:BITMAP:RLE`) and `main_window._on_image_send_requested` (16 bands) | RLE header 0x18 = "literal run of 25 pixels", pixel bytes, 4-byte CRC trailer | switch bands to `UI:DISP:BITMAP:B64` (exists; 33% larger) or add a `UI:DISP:BITMAP:RLE:B64` firmware command and base64 the RLE payload |
| `MotorClient.flash_upload` / `flash_upload_rle` (`UI:FLASH:UPLOAD[:RLE]`) | raw bytes + CRC | firmware needs a base64 upload variant; until then uploads are impossible under the 0x18 rule |
| `main_window.closeEvent`, `motor_panel._on_estop` | text `SYST:ESTOP` only | keep the text command as the secondary path; add the raw `b"\x18"` write first (S-1) |

Text commands are 7-bit printable, so no other path can emit 0x18. Base64 output never
contains 0x18. `rle_codec.py` needs no change if its output is base64-wrapped.

Other Phase 4 touch points: the firmware will refuse motion while latched and require `CLEAR`
to run the wake-up sequence (PA9 release, 2 ms, HardHiZ, reinit, HiZ check); the client's
`CLEAR_FAULT` button must expect `ERROR` when the HiZ check fails and surface it. The
heartbeat watchdog moves off the Comms task (step 3), which makes S-3's disarming even more
clearly wrong.

## 5. Structure, dead code, tests, dependencies

| # | Sev | Finding | Proposed fix |
|---|---|---|---|
| T1 | S3 | **Tests cover only the transport with mocks** (9 cases). No test parses an `OK`/`ERROR`/JSON line, an event, a banner, or a heartbeat ack; no test for `_classify_line`, `recv_line` partial lines, or the worker. | Add `tests/test_protocol.py` driven by recorded firmware lines; once the virtual Nucleo exists (item 3 of the plan), an end-to-end pytest against it. |
| T2 | S3 | Five ad-hoc scripts in `src/` (`test_raw_commands.py`, `test_connection.py`, `test_gui_connect.py`, `test_encoder.py`, `serial_diag.py`) hard-code `COM6` and use legacy command names; pytest will try to collect them. | Move to `client/scripts/` and update, or delete. |
| T3 | S4 | Two launchers (`main.py`, `gui_main.py`) with duplicated palette code; empty `controller/` and `utils/` packages; `.pytest_cache/` is untracked but not ignored. | Keep `main.py`, delete the rest, add `.pytest_cache/` to `.gitignore`. |
| T4 | S3 | `main_window.py` (1,116 lines) is the integration point for everything: connect sequence, 20 tagged-response handlers (`_on_response_received`, 180 lines of `if tag ==`), image pipeline, flash upload. | Split: `connection_controller.py` (connect/reconnect/setup), `transfer_jobs.py` (bitmap/flash), and per-panel response handlers registered by tag. |
| T5 | S3 | `MotorClient` mixes three roles: line framing, JSON/ASCII parsing, and ~40 command wrappers plus raw streaming. | Split into `framing.py` (classify, recv), `codec.py` (parse), `commands.py` (typed wrappers generated from PROTOCOL.md). |
| T6 | S4 | `requirements.txt` has no upper bounds and includes `pylink-square` and `scipy` for everyone; on a Raspberry Pi scipy is a heavy install used only by the sysid fit. | `pyproject.toml` with extras: `[gui]`, `[sysid]`, `[rtt]`; pin versions. |
| T7 | S4 | Logging at DEBUG prints every RX/TX line; at 10 Hz polling that is ~20 KB/s of log. | Rate-limit or make TX/RX logging a separate flag. |
| T8 | S4 | `rle_codec.py` builds a Python list of 2-byte tuples per pixel (76,800 tuples for a full screen) before encoding. | Operate on `memoryview` slices or numpy. |

## 6. Rust port: what would be easy, what would be hard

Easy, near 1:1:
- `transport/` → the `serialport` crate; the deadline-loop reader is simpler in Rust.
- `protocol/client.py` framing and parsing → an enum `Frame { Response, Event, Telemetry, Echo, Reset }` plus `serde_json`; typed command wrappers can be generated from `docs/PROTOCOL.md`.
- `serial_worker.py` → one `std::thread` with an `mpsc` channel of a `Job` enum (the Python "queued callable" closures become enum variants carrying their data); heartbeat and polling as timers in the same loop.
- `rle_codec.py`, `arrow_generator.py`, image conversion → `image` crate; trivial.
- The planned Pi coordinator and mecanum kinematics: pure functions, ideal for Rust first.

Hard or without a clean equivalent:
- **The GUI itself.** PySide6 + pyqtgraph has no drop-in Rust twin. Options: `egui`/`eframe`
  with `egui_plot` (immediate mode, fastest to port, plots are fine for 10 Hz telemetry),
  `iced` or `slint` (retained, closer to Qt), or keep a thin Python/Qt front end over a Rust
  core exposed through PyO3. The ten panels are 7,000 lines of Qt layout code that would be
  rewritten, not translated.
- **`sysid_panel.py`'s `scipy.optimize.curve_fit`** (FOPDT and sine fits, `sysid_panel.py:125-260`).
  Rust has `argmin` and `levenberg-marquardt` crates, but the fit code and its tolerances would
  be re-derived by hand.
- `RttTransport` (pylink / J-Link DLL): no maintained Rust binding; drop it (the Pi uses USB VCP).
- Qt signals/slots as the threading contract: Rust would use channels; every `Signal(...)` in
  the panels (41 of them) becomes an explicit message type.

If a port happens, do it core-first: transport + protocol + worker + kinematics as a Rust crate
with a C-ABI or PyO3 surface, keep the Qt panels until a Rust GUI is chosen. The protocol tests
written against the virtual Nucleo then validate both implementations.

## 7. Summary, ranked

1. **S-1** ESTOP waits in the FIFO behind seconds of transfers. Bypass the queue.
2. **S-2** Heartbeat proves the worker is alive, not the UI. Gate it on a UI dead-man token.
3. **S-3** The client disarms the watchdog during uploads. Never disarm; feed per chunk.
4. **R3/S-6** A board reset mid-session is invisible; the watchdog stays disarmed afterwards.
5. **1.1** `DBG:MOTOR` is locked since the Phase 2 debug gate; the driver panel refresh is broken.
6. **R1/R2** Unplug is never detected; no reconnect.
7. **§4** Four raw-binary paths must become base64 before the 0x18 rule lands.
8. **1.4** `DIAG:STAT?` polling saturates 115200 baud; push telemetry (`TELEM:`) is ignored.
9. **T1** No protocol tests; the virtual Nucleo would make them cheap.

## 8. Separation: how much protocol lives in the GUI today

Measured by counting SCPI string literals and response parsing in `client/src/gui/*.py`:

| File | SCPI strings built here | Response parsing here | Notes |
|---|---|---|---|
| `main_window.py` | 38 | 14 (the 179-line `_on_response_received` `if tag ==` chain) | connect sequence, setup commands, bitmap bands, flash upload, all refresh parsing |
| `motor_panel.py` | 22 | 0 | every motion button formats its own `MOT:*` string |
| `motion_panel.py` | 16 | 0 | unit conversion + `MOT:MOVE/GOTO/RUN` formatting |
| `telemetry_panel.py` | 10 | 0 | `CTRL:ENC:FILT:*` formatting; reads `DIAG:STAT?` dict directly |
| `sysid_panel.py` | 10 | 9 | runs the sysid *test sequence* itself (`_start_tests`, 868), parses `CTRL:SYSID:DATA?` CSV, fits with scipy |
| `protection_panel.py`, `driver_panel.py`, `stepmode_panel.py` | 9 | 0 | `DRV:CFG:*` formatting |
| `serial_worker.py` | 3 | 7 | heartbeat, polling and event parsing live in a Qt object |

Totals: 9,215 lines of GUI code against 2,086 lines of protocol + transport. Roughly 110
command strings and all tagged-response parsing sit in Qt classes, and the only test-like
sequences (sysid runs, connect/bring-up probing, bitmap transfer) are GUI methods. A
Raspberry Pi program could reuse `MotorClient` and the transports today, but would have to
re-implement the heartbeat, polling, event routing, reconnect, every panel's command
formatting, the sysid sequence and the bring-up probe. That is the duplication the shared
core must remove.

## 9. Proposal: one client core, two thin frontends

Package layout (`client/` becomes a proper package; nothing in `smc_core` imports Qt):

```
client/
  smc_core/                      # the shared core — pure Python, no GUI types
    transport/   serial.py (pyserial), tcp.py (virtual Nucleo), pty.py, base.py
    framing.py   Frame enum {Response, Event, Telemetry, Echo, Reset}, line classifier,
                 partial-line handling, reset/banner detection
    codec.py     ASCII/JSON response parsing, event parsing, telemetry parsing,
                 error codes (from PROTOCOL.md)
    commands.py  typed command API generated from docs/PROTOCOL.md (move, run, goto,
                 estop, clear_fault, set_step_mode, heartbeat, set_heartbeat, ...)
                 + a test that every command in PROTOCOL.md has a wrapper and vice versa
    board.py     Board: one connection = transport + framing + codec + worker thread +
                 command FIFO + coalesced live commands + heartbeat (dead-man gated) +
                 reconnect + reset handling + ESTOP fast path (0x18 + SYST:ESTOP)
    fleet.py     Fleet: N boards, identity (DEV:ID/ROLE, USB serial), ESTOP fan-out,
                 synchronised START, heartbeat per board, one-board-lost policy,
                 merged telemetry with per-board timestamps
    kinematics.py mecanum inverse/forward (plain functions, numpy-free)
    scenarios/   named test scenarios: bringup, estop_latency, heartbeat_loss,
                 step_mode_readback, sync_start, four_board_estop ...; each is a
                 function (fleet, params) -> ScenarioResult (plain dataclass)
    runner.py    runs scenarios by name, records the byte stream sent/received per
                 board, emits one result format (JSON lines)
    events.py    plain callbacks / queues for frontends (no Qt signals)
  smc_gui/                       # PySide6 frontend: panels call smc_core only
  smc_cli/                       # Pi frontend: `smc board list`, `smc test run <name> --board FL`,
                                 # `smc fleet estop`, `smc telemetry --json`
  tests/                         # pytest: unit (framing/codec/commands/kinematics),
                                 # integration against the virtual Nucleo, and the
                                 # GUI-vs-CLI identical-output test
docs/PROTOCOL.md                 # generated; commands.py and its test derive from it
```

Rules that make it hold:
1. A frontend never formats a command string or parses a line. If it needs something new, it
   goes into `smc_core.commands` (and PROTOCOL.md) first.
2. The core speaks to frontends through plain data: dataclasses, `queue.Queue`, callbacks.
   The GUI wraps a core callback in a Qt signal at one place (`smc_gui/bridge.py`).
3. Threading lives in `board.py` only. Frontends are single-threaded callers.
4. Scenarios are data in, data out. The GUI "Tests" panel and the CLI both call
   `runner.run(name, fleet, params)` and render the same `ScenarioResult`.
5. Identical-output test: start four virtual Nucleos, run the full scenario list once through
   `smc_gui`'s entry point (headless `QApplication`) and once through `smc_cli`, capture each
   board's TX byte stream and the results, assert byte-for-byte equality (timestamps
   normalised).
6. Portability: pure Python 3.11+, pyserial, no Qt in the core; the same wheel runs on
   Windows, Linux and the Pi. The CLI needs nothing else; the GUI adds PySide6/pyqtgraph.

Migration order (each a commit, client tests green after each):
1. Create `smc_core` with `transport/`, `framing.py`, `codec.py` by moving `protocol/` and
   `transport/` and adding the reset frame; add unit tests from recorded lines.
2. Generate `commands.py` from PROTOCOL.md; add the coverage test; point `MotorClient`
   callers at it.
3. Move `SerialWorker` logic into `board.py` (threads, FIFO, heartbeat with dead-man token,
   reconnect, ESTOP fast path); `smc_gui/bridge.py` adapts callbacks to signals.
4. Move panel command formatting into core helpers (motion units, driver config, encoder
   filters); panels become widgets that call `board.move(...)`.
5. Move the sysid sequence and bring-up probe into `scenarios/`; add `runner.py` and the
   CLI; add the GUI Tests panel.
6. `fleet.py` + `kinematics.py` (multi-board work, section 5 of the plan).

## 10. Rust, revisited with the core in place

With section 9 done, a Rust port can replace `smc_core` alone first: its boundary is plain
data (dataclasses, byte streams, JSON lines), its threading is internal, and the frontends
only call functions. A PyO3 wheel exposing `Board`, `Fleet`, `runner.run` with the same
signatures lets the PySide6 GUI and the Python CLI keep running unchanged while the core is
Rust; the identical-output test and the scenario results format are the acceptance test for
the port. The hard items in §6 (Qt panels, scipy fitting) stay in Python until a Rust GUI is
chosen, and the sysid fit can move into the core as a scenario post-processing step with a
Rust implementation later. So yes: the "core first" port holds, provided the core keeps to
plain types and PROTOCOL.md stays the contract.
