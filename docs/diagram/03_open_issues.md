# Open issues and open questions (text for a diagramming agent)

Source of truth: commit `c9a35e5` on `master`, 2026-10-05. Each item gives: where it is in
code, the mechanism to draw (usually a timeline or sequence), the quantified impact, why
it matters, the proposed fix (also drawable), and status. Items are ordered by risk.


## Status after the 2026-10-06 fix-now batch and protocol pass (protocol 2)

| Item | State | Commit |
|---|---|---|
| 10 Heap use in the SPI manager | **closed**: static storage, placement new; the whole firmware is heap-free (`configSUPPORT_DYNAMIC_ALLOCATION 0`, heap_4 removed; tasks/queues/mutexes static) | F11 |
| 4 Encoder stack | still an estimate; the task now runs from a 4096-word static pool (3968 used) and `free_heap` telemetry reports the pool's unused bytes | F11 |
| 3 Unlocked Comms-task calls into the controller | open (Phase 4 step 4) | — |
| 6 Two ESTOP state holders | open (Phase 4 step 2 makes the MotorService latch the single source) | — |
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
| NEW: `DEV:ID` / `DEV:ROLE` crash (null lock) | **open**: `g_deviceConfig.init()` is never called since `f874dd5`; setters dereference a null `m_lock`. Blocks per-corner identity (docs/review/LINK_BUDGET.md, finding 1) | — |
| NEW: config apply stalls the MCU ~1-2 s | **open**: `DRV:CFG:APPLY` and every *:SAVE erase the 128 KB internal sector 7 from flash-resident code. Client mitigation: watchdog widened to 5 s around these commands (LINK_BUDGET.md, finding 2) | — |
| NEW: upload CR/LF drain can eat a payload byte | **open**, latent (LINK_BUDGET.md, finding 3) | — |
| NEW: encoder velocity units | **open**: `EncoderService` reports counts per sample (delta / window samples), never scaled to per second, but system ID logs it as `actual_tps` beside a setpoint in ticks/s and the speed-trim and following supervisor take it as ticks/s: a factor of the sample rate (1000) apart. Found on the virtual Nucleo; confirm on hardware by reading `DIAG:STAT?` encoder velocity at a known `MOT:RUN` speed | — |
| NEW: status snapshot lags an ESTOP by up to 50 ms | **documented**: `motor.hi_z` in `DIAG:STAT?` is refreshed by the Motor task every 50 ms; the client re-reads for up to 300 ms before calling an ESTOP unconfirmed | client |
| Client S-5 (watchdog default) | **done**: 500 ms default, heartbeat 100 ms, telemetry 200 ms, all visible settings; image bands 5 rows | client step 6 |

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

**Status**: analysed, proposed, not implemented. Waiting for a decision.

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

**Status**: open; low effort now that the SetStepMode message exists.

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

**Status**: documented; could be closed by making `CommandQueue` the sole owner (private
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
