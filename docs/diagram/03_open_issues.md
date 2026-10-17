# Open issues and open questions (text for a diagramming agent)

Source of truth: commit `c9a35e5` on `master`, 2026-10-05. Each item gives: where it is in
code, the mechanism to draw (usually a timeline or sequence), the quantified impact, why
it matters, the proposed fix (also drawable), and status. Items are ordered by risk.


## Status after the 2026-10-06 fix-now batch and protocol pass (protocol 2)

| Item | State | Commit |
|---|---|---|
| 10 Heap use in the SPI manager | **closed**: static storage, placement new; the whole firmware is heap-free (`configSUPPORT_DYNAMIC_ALLOCATION 0`, heap_4 removed; tasks/queues/mutexes static) | F11 |
| 4 Encoder stack | **closed 2026-10-17**: measured on the bench with the new `DIAG:TASKS?` after the motion scenarios: Encoder 168 words free (Comms 1403, Motor 797, Display 351, IDLE 114); the estimate held | F11, Step 0 |
| 3 Unlocked Comms-task calls into the controller | **closed 2026-10-17** (Phase 4 step 5): `reinit`, `applyConfig` and `getDebugInfo` are Motor-task jobs (`MotorCmdType::Reinit / ApplyConfig / GetDebugInfo`) answered through `MotorService`'s job slot with a 250 ms bounded wait on the Comms side; while paused or in standby the job is answered "not done" at once. Bench: `DRV:REINIT` 26 ms, `DRV:CFG:APPLY` 47 ms, `MOTOR_DEBUG` 19 ms end to end | F-jobs |
| 6 Two ESTOP state holders | **closed 2026-10-17** (Phase 4 step 4): `CommandQueue::getState()` derives ESTOP from `MotorService::isStopLatched()`; the queue never stores ESTOP itself. A stop from any path (SYST:ESTOP, watchdog, the 0x18 byte alone) reads ESTOP, refuses motion and clears normally. Bench: 0x18 alone -> `state ESTOP`, `MOT:RUN` refused with `FAULT`, clear -> IDLE | F-latch |
| Phase 4 step 1: out-of-band ESTOP | **done 2026-10-17**: byte 0x18 in the USART2 ISR (`UartHardware::handleIRQ` -> `Harness::invokeOobEstop()` -> `MotorService::oobEstopFromIsr`) drives STBY/RESET low and latches in the interrupt; the Motor task does the SPI HardHiZ, publishes a "Hi-Z, stopped" snapshot while the chip is in standby, and on `SYST:FAULT:CLEAR` releases the pin, waits 2 ms and re-initialises the driver with the saved config. Bench: 0x18 -> pin low 0-1 µs in every case, including the enable case that still takes 14.6 ms over SPI1. Raw binary uploads pause the byte for their payload (`OobEstopPause`); the base64 variants keep it. `SYST:ESTOP` replies `oob_pin_us` / `oob_hiz_us`. Simulator: the TCP transport and FakePowerStep honour it; the fleet ESTOP test passes | F-oob |
| Phase 4 step 2: interrupt-driven UART transmit | **done 2026-10-17**: `UartHardware::write()` queues into a 4 KB ring drained by the USART2 TXE interrupt; the writer sleeps 1 ms at a time only when the ring is full (`Harness::setCommsTxClock`); `flush()` waits for ring + shift register. Bench: an ESTOP line parsed 0.7 ms after arrival while a `SYST:HELP?` reply is still going out (was 224 ms), a ping answered on-chip in 1-2 µs during the same. Item 2 (one serial loop) is narrower now: the Comms task is only held by *receiving* (raw binary payloads), never by sending | F-txring |
| Phase 4 step 3: SPI1 locked per chunk during bitmaps | **done 2026-10-17**: `LCD::streamBitmapStart` releases the bus after the window set-up, each `streamBitmapData` is its own locked transaction continued with RAMWRC (0x3C), `streamBitmapEnd` only closes the stream state. Bench: a software-timed 3 s identification run during a 2.7 s bitmap took 6.34 s wall and let the motor run 45558 counts unsupervised before, 3.58 s and 24300 counts after (the Motor task passes every 50 ms through the stream). Host test in `test_lcd_vsync.cpp`. Closes item 1's fix 1 | F-spichunk |
| NEW (bench 2026-10-17): board silent on a port opened after boot | **open, intermittent; worked around**: seen 3 times. Diagnosis so far: every boot captured with the port already open was complete and answered; uptime continues across port open/close cycles with any DTR/RTS setting (the host opening the port does not reset the MCU); 3 + 3 programmings with the port closed later came up fine; in the silent cases nothing at all was pending on the port although telemetry was on, so the ST-LINK VCP bridge delivered nothing in either direction. Mitigations in: `smc flash <bin>` programs through the NUCLEO drive with the session held open (the Board re-runs its setup on `!RESET`) and reports the new build; `!RESET` carries `rst=<cause>` (RCC CSR flags, cleared each boot) so an unexpected MCU reset would show; `build=` is now the real tree hash (+dirty), refreshed per commit. If it recurs: hold a raw logger on the port and note the `rst=` of the next boot | client, F-rst |
| 12 Platform files define `Harness::` functions | open (Phase 4 step 5) | — |
| Server review F1.1/F1.2 | **done**: lines spanning an RX overflow are refused (`ERROR RX_OVERFLOW`), ring 256 -> 1024 B, counters in `DIAG:STAT?.system` | F1 |
| Server review F3 | **done**: read-only debug dumps bypass `DBG:UNLOCK` | F3 |
| Server review F4 | **done**: guarded, host-tested `DIAG:STAT?` render (1280-byte buffer, worst case asserted) | F4 |
| Server review F13 | **done**: bounded copies; release build is `-Werror` | F13 |
| Server review F15 | **done**: `DEV:ROLE?` has its own handler | F15 |
| Client review A1–A7 | **done** on the existing GUI (fast-path ESTOP with purge and confirmation, dead-man heartbeat, no watchdog disarm, heartbeat-timeout display, scripts moved) | client hotfixes |
| Server review F17 | **done**: `!RESET boot= fw= proto= build=`, boot id in heartbeat acks, `*IDN?` | P1 |
| Server review F7 | **done**: echo off in JSON mode, `SYST:ECHO ON\|OFF`, `SYST:ECHO?` | P2 |
| Server review F6 | **done**: `ERROR <CODE> <message>` in ASCII, `code` in JSON, `ServiceStatus` -> code table | P3 |
| Server review F5 | **done**: `Protocol::jsonEscape()` on every string that enters JSON; host tests | P4 |
| Server review F9 | **done**: `SYST:TLM:RATE <ms>` (20..5000, 0 = off) JSON push with the `DIAG:STAT?` data object; `TELEM:` kept for ASCII | P5 |
| Server review F12 | **done**: every multi-line reply ends with `OK <n>` / is one JSON envelope with `lines` (`LineReply`); client reader updated | P6 |
| Server review F10 | **done** (documentation): `seq` is JSON-only, per-boot, counts lines sent; drops are `lost_critical` / `lost_info` in `SYST:EVT:STAT?` | P7 |
| Server review F8 | **done**: `SYST:HELP?` from `help_table.inc`, generated and checked by `gen_protocol.py --check` | P8 |
| Usage strings | **done**: all `Usage:` texts and `SYST:HELP?` spell SCPI names | P9 |
| Server review F14 | **done**: adapter limitation documented in PROTOCOL.md; GUI offers 115200 and above only | P10 |
| Protocol version | **2** in `harness/pins/protocol_version.hpp`; the generator reads it; `docs/PROTOCOL.md` section 7 lists the changes and what Phase 4 still reserves | P11 |
| Server review F2, F16, item 10/11 of PROTOCOL.md §7 | open (Phase 4: `SYST:HB:TIMEOUT 0` only in Hi-Z, debugPrint guard, base64 uploads with the 0x18 rule) | — |
| NEW: `DEV:ID` / `DEV:ROLE` crash (null lock) | **done**: `g_deviceConfig.init()` had not been called since `f874dd5`, so the setters dereferenced a null `m_lock`. `initDisplaySubsystem` now initialises it on the NOR flash it probes (reserved sectors 0x000000-0x001FFF, below the image slots) with a new platform mutex `deviceConfigLock`; the boot log prints `Device identity... OK` / `DEFAULTS`; the setters refuse (`Flash write failed`) instead of crashing when no flash was found. Per-corner identity (docs/review/LINK_BUDGET.md, finding 1) is unblocked | F-dev |
| NEW: config apply stalls the MCU ~1-2 s | **done**: every `DRV:CFG:APPLY` and `*:SAVE` erased the 128 KB internal sector 7 from flash-resident code, freezing the whole MCU (ESTOP included) for 1-2 s. The config store is now an append-only journal in that sector (`InternalFlashJournalStore`: 1984 64-byte slots, the pre-journal blob at the sector's last 4 KB is the first-boot fallback): a save is a few word programs with no erase. The erase happens only when the journal is full: at boot when fewer than 16 saves are left (nothing moves yet), or at run time only if the motor is stopped; otherwise the command answers `STORE_FULL` ("Config store full: stop the motor and save again"). `configSaveToFlash()` returns a `ServiceStatus`; host tests `test_config_journal`, `test_motor_config`. The client's 5 s watchdog widening around persisting commands stays, since it still covers the rare run-time erase | F-journal |
| NEW: upload CR/LF drain can eat a payload byte | **open**, latent (LINK_BUDGET.md, finding 3) | — |
| NEW: encoder velocity units | **done**: `EncoderService` reported counts per sample (delta / window samples), never scaled to per second, while system ID (`actual_tps`), the speed trim, the following supervisor, the LCD and the GUI all take ticks/s; at 100 steps/s the value (2 counts/sample) fell into the processor's ±2 deadband and read 0. Now `delta × sampleRateHz / windowSamples`. Bench: `MOT:RUN 100/400/800` (1/32) reads 1987 / 7944 / 15885 counts/s against 2000 / 8000 / 16000 expected (the chip runs 99 / 399 / 799 steps/s). The "velocity reads 0 while moving" row below was this | F-vel |
| NEW: status snapshot lags an ESTOP by up to 50 ms | **documented**: `motor.hi_z` in `DIAG:STAT?` is refreshed by the Motor task every 50 ms; the client re-reads for up to 300 ms before calling an ESTOP unconfirmed | client |
| NEW: `SYNC:START:AT` blocked the Comms task | **done**: `CommandQueue::startAt` slept and spun until the start tick, so no command (ESTOP included) was parsed and the watchdog was not evaluated meanwhile. Now it validates the tick (past: `TIMEOUT_EXPIRED`, more than `MAX_START_AT_US` = 2 s ahead: `INVALID_PARAM`), queues a `StartAt` command to the Motor task and answers `OK SCHEDULED`; the Motor task (`MotorService::handleStartAt`) sleeps in 1 ms steps, spins the last 1.5 ms and calls `IScheduledStart::executeScheduledStart()` on the queue, abandoning the wait when the ESTOP latch is set or the queue was cleared. Host tests in `test_command_queue` and `test_motor_service` | F-startat |
| Client S-5 (watchdog default) | **done**: 500 ms default, heartbeat 100 ms, telemetry 200 ms, all visible settings; image bands 5 rows | client step 6 |
| NEW (bench 2026-10-17): `DRV:CFG:APPLY` could not change the step mode while the output was enabled | **done**: `MotorController::applyConfig()` wrote STEP_MODE with a plain read-modify-write; the powerSTEP01 accepts that register only in Hi-Z, so the chip silently kept the old mode while `DRV:CFG?` (flash) reported the new one (measured: config 7, chip 5; the GUI Step Mode tab used this path, so a "100 steps" jog at a believed 1/128 sent 12800 usteps = 2 revolutions). Now `applyConfig()` writes STEP_MODE only in Hi-Z and verifies it, `MotorService::applyConfig()` otherwise routes it through the Motor task's `SetStepMode` message (Hi-Z, write, verify, SoftStop), and `DRV:CFG:APPLY` answers `MOTOR_BUSY` while moving and `VERIFY_FAILED` on a readback mismatch; its OK line carries `STEP_MODE=`. Host test in `test_motor_service.cpp`; scenario `step_mode_chip` passes on the bench and on the virtual Nucleo | F-apply |
| NEW (bench 2026-10-17): telemetry at 50 ms starved the Encoder task | **done** (Phase 4 step 2: the USART2 TX is a 4 KB ring drained by the TXE interrupt, the Comms task no longer busy-waits; the floor stays as a link-budget rule). History: `UartHardware` transmitted by busy-waiting on TXE in the Comms task (priority 3); one `DIAG:STAT?` push is ~900 bytes = 78 ms at 115200, so `SYST:TLM:RATE 50` kept the Comms task runnable almost continuously and the Encoder task (priority 2) never refreshed its snapshot: the encoder count in `DIAG:STAT?` froze for a whole 1-revolution move (TIM4 kept counting; it caught up afterwards). Now `SYST:TLM:RATE` is floored at `telemetryPeriodFloorMs(baud)` (status_json.hpp: one worst-case 1280-byte line may use at most 80 % of the link, 139 ms at 115200) and the ack carries `min_ms`; the transport reports its baud rate through `ITransport::baudRate()`. The busy-wait TX itself stays until Phase 4 (interrupt-driven TX ring, which also takes the UART out of the ESTOP path). Scenario `telemetry_load` records `accepted_ms` / `min_ms` | F-tlm |
| NEW (bench 2026-10-17): encoder velocity read 0 while moving | **done** with the velocity-units row: the counts-per-sample value was below the ±2 deadband (`vel_quality` 0 is GOOD) | F-vel |
| Client: late reply shifted every later reply by one | **done** (client): a request that timed out (a slow `SYST:HB` ack) left its reply in the stream, and `Board._is_stale` compared the reply's JSON `command` (`SET_HEARTBEAT`, `HEARTBEAT`, `IDN`, ... 23 names differ) with the request name, so the late reply was handed to the next request. Now matched through `CommandSpec.json_command`; regression test in `test_board.py` | client |
| Client: motion behaviour scenarios | **done**: `motion_scale` (ABS_POS and encoder per N microsteps, REV form, counts/rev measured: 4006 fwd / 3976 rev on the bench), `step_mode_chip`, `telemetry_load`; the in-process fake board now has timed moves and an encoder that follows in counts/rev | client |
| NEW (2026-10-17): Phase 4 Step 0 measured without an analyzer | **done**: `SYST:ESTOP` replies `latency_us` (USART ISR stamp of the line's last byte, `IByteChannel::lastRxTickUs()`, to the return of the Hi-Z SPI write): idle 664 / 697 / 1574 µs, during RUN 678 / 692 / 1546 µs (min / median / max), but 13-15 ms when the ESTOP lands while the Motor task holds SPI1 for an enable (items 1 and 3: the reason for the Phase 4 STBY pin path). `DIAG:TASKS?` prints the LCD Task Monitor table over serial. Debug `STBY_SENSE` floats PA9 and watches it: it falls LOW after ~38 ms, matching UM1910 figure 3 (no resistor on RESET_IC, only C6 = 100 nF to ground, discharging through the chip's 450 kΩ pull-down): a dead MCU is fail-safe after ~40 ms, the pin path must drive PA9 low actively, no hardware change. Results in `docs/bringup/PHASE4_STEP0_BASELINE.md` section 5 | F-step0 |
| `build=` captured at CMake configure time | **done 2026-10-17**: only `harness/pins/build_id.cpp` sees `GIT_HASH`; CMake re-runs configure when `.git/HEAD` or `.git/index` change and appends `+dirty` when `server/`, `cmake/` or `CMakeLists.txt` have uncommitted changes, so `!RESET build=` and `SYST:VER?` name the tree that was built | F-buildid |
| NEW (bench 2026-10-17): a move's speed stuck as the maximum after STOP | **done**: `MOT:MOVE/GOTO/HOME <speed>` lower MAX_SPEED for that move and restore it on completion or a hard stop, but not on a soft stop (the GUI's STOP), so every later RUN was capped at the last move's speed; a second override while one was active also saved the overridden value as the "original". Now restored on SoftStop/SoftHiZ and before any RUN, the original is kept across nested overrides, and `SetMaxSpeed` / config apply reset the override state. Bench: MAX_SPEED raw 68 -> 6 during a 100 steps/s move -> 68 after STOP; a RUN 600 afterwards reaches 599 steps/s (was ~100). Host test | F-maxspd |

## 1. SPI1 is held for an entire remote bitmap; ESTOP cannot interrupt it

**Where**: `L2_protocol/handlers/display_handlers.cpp::cmdDispBitmap` (also the RLE and
base64 variants), `L4_drivers/devices/lcd_st7789.hpp::streamBitmapStart/Data/End`.

**Mechanism (draw as a timeline with three lanes: Comms task, Motor task, SPI1 mutex)**:
`streamBitmapStart` locks SPI1. The Comms task then sits in a loop reading pixel bytes from
the UART, 64 at a time, writing each chunk to the LCD by DMA. `streamBitmapEnd` unlocks.
During the loop: (a) the Motor task's 50 ms `periodicUpdate` blocks on the SPI1 mutex at its
first `getStatus`, so the following-error supervisor, speed trim and status telemetry stop;
(b) the Comms task is inside one command handler, so no other host command, including
`ESTOP`, is parsed.

**Numbers** (UART 10 bits/byte; LCD SPI at 42 MHz is negligible):

| Bitmap | Bytes | Hold at 115200 baud | Hold at 921600 baud |
|---|---|---|---|
| 64-byte chunk | 64 | 5.6 ms | 0.7 ms |
| 240 × 240 | 115,200 | 10.0 s | 1.25 s |
| 240 × 320 (max) | 153,600 | 13.3 s | 1.67 s |

ESTOP latency while streaming = remaining bitmap time, bounded only by the 100 ms
per-byte timeout if the host stops sending. Outside streaming the ESTOP path is ~0.6 ms at
115200 baud (parse + one SPI1 transaction), worst case ~23 ms if a full-screen fill holds
SPI1.

**Significance**: a safety command can be delayed by seconds; the supervisor that detects
following faults is blind for the same period. Both violate the 5 ms target.

**Proposed fix (draw as the same timeline, after)**:
1. Lock SPI1 per chunk inside `streamBitmapData` (ST7789 `RAMWRC` 0x3C continues the
   write), so the mutex is free between chunks and the Motor task interleaves.
2. An out-of-band ESTOP byte recognised in the USART2 ISR (`UartHardware::handleIRQ`) sets
   an atomic flag and notifies the Motor task; the bitmap loop checks the flag per chunk and
   aborts; the Motor task performs the Hi-Z (not the Comms task). Latency becomes ≤ one
   chunk at the UART rate, or ≤ one Motor-task pass if the Motor task is woken directly.

**Status**: closed 2026-10-17. Fix 2 (the out-of-band byte, Phase 4 step 1) makes the stop itself independent of the stream; fix 1 (per-chunk SPI1 locking, Phase 4 step 3) keeps the Motor task running through a bitmap. What remains is only that a raw binary payload pauses the 0x18 byte for its duration (the client's bands are ~0.2 s; base64 variants keep it active).

## 2. The Comms task is one serial command loop

**Where**: `L3_services/comms/comms_service/comms_service.cpp::tick`,
`L2_protocol/command_parser.cpp::process`.

**Mechanism**: one task reads bytes, parses one line, runs its handler to completion, then
reads the next. Any long handler (bitmap, flash erase, flash image write, base64 decode)
delays everything behind it: the next command, the 100 ms telemetry emit, the event drain,
the heartbeat check. Draw as a single queue of commands with one worker.

**Significance**: root cause of item 1; also means heartbeat/telemetry cadence jitters with
command length.

**Options to draw**: (a) split long transfers onto the Display task via a chunk queue; (b)
an out-of-band ESTOP (item 1.2); (c) accept and document.

**Status**: open design question.

## 3. Three Comms-task calls still mutate the motor controller without a lock

**Where**: `MotorService::reinit`, `applyConfig`, `getDebugInfo` → `MotorController` (same
names), called from `MotorDriverDispatch`, `ConfigDispatch` and `DebugService` on the Comms
task, while `MotorController::periodicUpdate` may be mid-sequence on the Motor task.

**Mechanism (draw as two lanes interleaving)**: `reinit` calls `IMotorDriver::init()` and
re-applies every register; `periodicUpdate` does three separate SPI transactions
(`getStatus`, `getParam ×2`) then possibly `run()`. Each single transaction is serialised by
the SPI1 mutex, but the sequences can interleave: a `run()` from the Motor task can land
between `init()` and `applyConfig()` on the Comms task.

Related residual window in ESTOP: a command the Motor task dequeued just before the latch
can reach the driver after the Hi-Z; `onEmergencyStop` re-asserts Hi-Z within one Motor-task
pass (≤ 50 ms). Draw as a short red window on the ESTOP timeline.

**Significance**: rare but real: a reinit during motion could leave the driver in a
half-configured state; the ESTOP window is bounded and short.

**Proposed fix**: route `reinit`/`applyConfig`/`getDebugInfo` through the same message
mechanism now used for `SetStepMode` (command + atomic result slot), making the Motor task
the only caller of `MotorController`.

**Status**: closed 2026-10-17 (Phase 4 step 5): all three are Motor-task jobs, see the status table.

## 4. Encoder task stack headroom is estimated, not measured

**Where**: `F_platform/tasks/encoder_task.hpp` (`ENCODER_TASK_STACK_SIZE = 256` words).

**Mechanism**: a one-off build with `-fstack-usage` gave the deepest static chain
(`vEncoderTask` 16 + `EncoderService::sample` 72 + telemetry write 16 + mutex take 40) plus
the Cortex-M4F context frame when the FPU is active (~204 bytes) ≈ 350 bytes. The old 512-byte
stack left ~40 words; it is now 1024 bytes. Draw as a stacked bar: used vs headroom, before
and after.

**Significance**: a stack overflow on the Encoder task would hit the
`vApplicationStackOverflowHook` and halt. The estimate ignores interrupt frames stacked on
MSP (not on the task stack) and any callback depth through `IEncoderStatusSink`.

**What is needed**: read `uxTaskGetStackHighWaterMark` on hardware (the LCD task-monitor
screen shows it) or add a host command that prints `ITaskStats` per task.

**Status**: mitigated by the raise; unverified.

## 5. Nothing from Phase 3 has run on the board

**Where**: every change since commit `b21e275`.

**Mechanism**: all verification was compile (debug + release), 12 host tests, and the three
checkers. The board was not available. The host tests exercise L2, L3 and L4 logic over
fakes; L1, L5, the ISRs, FreeRTOS timing and the SPI/DMA paths are untested.

**Significance**: the ESTOP latch, the seqlock snapshot, the step-mode message wait (250 ms
bound), the debug-pause flags and the RX-wake notification all have timing assumptions that
only hardware confirms. Draw as a coverage map: green for host-tested blocks, grey for
"compiled only".

**Status**: needs a bring-up session: boot banner, `ESTOP` during `RUN`, `DRV:STEPMODE` with
readback, `DBG:UNLOCK` + `SPI_STOP/START` + `LCD_DISABLE`, encoder index count with the Z
pulse, stack high-water marks.

## 6. Two ESTOP state holders

**Where**: `CommandQueue::m_state` (IDLE/ARMED/RUNNING/FAULT/ESTOP) and
`MotorService::m_stopLatched` (atomic).

**Mechanism**: `CommandQueue::emergencyStop` sets both; `CommandQueue::clearFault` clears
both. Nothing else may touch the latch, but nothing enforces that. Draw as two state boxes
with the two transitions that keep them aligned.

**Significance**: a future caller of `immediateHardHiZ` or `clearImmediateStop` that is not
`CommandQueue` would desynchronise the host-visible state from the motor's.

**Status**: closed 2026-10-17 (Phase 4 step 4): the MotorService latch is the sole owner; the queue derives ESTOP from it.
interface) or by deriving one from the other.

## 7. Two entry paths for motion commands

**Where**: `Motion::run/move/goTo` → `MotorService::sendCommand` directly into the FreeRTOS
queue (immediate), versus `QueueDispatch` → `CommandQueue::queueCommand/arm/start/startAt`
(staged, synchronised, flushed on START). Draw as two pipes into one queue.

**Significance**: the ESTOP fix had to cover both; the staged path has explicit states, the
immediate path has none beyond the latch. A host can mix them and see different failure
modes (`Queue full` vs `INVALID_STATE`).

**Status**: by design; worth showing so the diagram does not imply one funnel.

## 8. Debug and control flags act at the next task pass

**Where**: `MotorService::m_debugPaused`, `m_stopLatched`, `DisplayService::m_enabled`,
`MotorController::m_motionInhibited`.

**Mechanism**: flags are read at the top of `onCommand`/`periodicUpdate`/`tick`. A render or
a `periodicUpdate` already in progress finishes first (≤ 50 ms). The step-mode message waits
up to 250 ms for the Motor task's readback. Draw as a timeline with "flag set" and "takes
effect" markers.

**Significance**: fine for debug use; the ESTOP path does not rely on it (the Hi-Z is sent
synchronously from the Comms task).

**Status**: documented behaviour.

## 9. USART2 has two owners in sequence

**Where**: `F_platform/hw/early_debug.hpp` (bare-metal USART2 TX, used by
`Harness::debugPrint*` during init and by the hooks) and `L5_board/uart/uart_hardware.hpp`
(ISR-driven USART2 for the host protocol, initialised in `initCommsBoard` during the Comms
subsystem init).

**Mechanism**: early-debug prints and the protocol transport share the same peripheral and
the same ST-LINK virtual COM port; after the Comms subsystem starts, `debugPrint` writes
would interleave with protocol bytes. The hooks (hard fault, stack overflow) still use the
bare-metal path, which is what you want when the RTOS is gone. Draw as one peripheral with
two drivers and a hand-over point in the init ladder.

**Significance**: a benign shared resource during init; a possible source of garbled output
if `debugPrint` is used after phase 4.

**Status**: documented; no change proposed.

## 10. Heap use in the SPI manager (closed by F11, kept for history)

**Where**: `L5_board/spi/spi_manager.hpp::SPIManager::init` does `new SPIHardware(...)` twice.

**Mechanism**: the project's stated goal is static allocation; FreeRTOS uses `heap_4` for
its own objects; these are the only application-level `new`s. Draw as a small heap box with
two arrows.

**Significance**: low (happens once at init, never freed), but it contradicts the
"static allocation" rule and complicates a `configSUPPORT_DYNAMIC_ALLOCATION 0` build.

**Status**: open; trivial to make static.

## 11. Global accessors that bypass the harness structs

**Where**: `Harness::microsecondClock()` (`pins/imicrosecond_clock.cpp`, set by the
composition root), `Harness::getExtiCallback(line)` (`pins/exti_callbacks`, the ISR
callback table), `Services::g_telemetryStore` (written directly by `MotorController` and
`EncoderService`, read by dispatchers and screens).

**Mechanism**: these are reachable without going through any harness struct, so a diagram
that only follows the six connectors misses them. Draw as unlabelled wires from a global
bus to their users.

**Significance**: they are the remaining "hidden" edges; all three are deliberate
(ISR-safe, pre-scheduler, or L3-internal).

**Status**: documented in the handoff; no change proposed.

## 12. Four Platform files define `Harness::` functions

**Where**: `F_platform/hw/early_debug_board.{hpp,cpp}` (`Harness::initDebugOutput`,
`debugPrint`, `debugPrintln`), `F_platform/rtos/platform_board.hpp` and `platform_init.cpp`
(`Harness::PlatformResources`, `Harness::initPlatformBoard`).

**Mechanism**: the init contracts are declared in `harness/pins/` and implemented in the
Platform directory under the `Harness` namespace. The manifest classifies by directory.
Draw with a dual label (directory PLATFORM, namespace Harness).

**Significance**: cosmetic inconsistency; the task-sheet equivalents were renamed to
`Scheduler::` in step 4, these were not.

**Status**: open; rename to `Platform::` and update the two pins headers.

## 13. Trace is a global net only in debug builds

**Where**: `harness/trace/interface_trace.hpp`, `CMakePresets.json`.

**Mechanism**: with `ENABLE_INTERFACE_TRACE` (firmware-debug) every `ITRACE()` emits over
RTT and into the L3 trace ring; the taps add per-call logging on five interfaces. In
firmware-release the macros are empty and `applyTap` returns the raw object, so the release
binary has different timing (no RTT writes on the hot paths) and 11 KB more text (-O2
inlining). Draw as a switch on the TRACE rail.

**Significance**: timing measured in debug (e.g. ESTOP latency) is pessimistic; a bug that
depends on trace timing would hide in release.

**Status**: documented behaviour.

## 14. EXTI9 index priority: 5, deliberately not 6

**Where**: `L5_board/init/encoder_board.cpp::ENCODER_INDEX_IRQ_PRIORITY`.

**Mechanism**: 5 equals `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`, the highest priority
allowed to call `FromISR` kernel functions, and it preempts USART2 at 6. The index ISR
timestamps the Z pulse with the microsecond clock; at 6 a UART burst could delay that stamp.
Draw as a priority ladder: TE flag 4 (no kernel calls) > index 5 > USART2 6 > SysTick/PendSV.

**Significance**: the user asked for ≥ 5 matching USART2 unless there was a reason; this is
the reason. Open only if index timing accuracy turns out not to matter.

**Status**: decided; revisit if desired.

## 15. Archived documents still describe the old structure

**Where**: `docs/archive/` (14 files from the Core/Inc era, refactor plans, forum threads).

**Significance**: an agent that reads them will draw the wrong system. The README's docs
table marks them historical.

**Status**: intentional; do not use.
