# How the harnesses actually work (text for a diagramming agent)

Source of truth: the code at commit `c9a35e5` on `master`, 2026-10-05. Every claim below
names the file or symbol it comes from. Companion files: `02_sheets.md` (what each sheet
is), `03_open_issues.md` (what is still wrong or unknown), `../ARCHITECTURE_HANDOFF.md`
(measured counts), `../architecture_manifest.json` (per-signal rows).

## 1. The pattern: four artefacts per boundary

Every boundary between two adjacent sheets is represented by exactly one header in
`server/harness/defs/`. Draw a harness as a connector block with four parts:

| Part | What it is in code | How to draw it |
|---|---|---|
| **Header** | `harness_<upper>_<lower>.hpp`. The only header through which the two sheets see each other. It includes the two sheets' `boundary/` directories, shared PODs from `harness/pins/`, and Platform primitives. | The connector's housing. Label with the file name. |
| **Struct of references** | e.g. `struct L3L4Harness { IMotorDriver* motor; ICanvas* canvas; ... }`. One static instance per boundary. Members are references or pointers to interfaces the *provider* side implements. | The pins of the connector. One pin per member. |
| **Signal table** | `constexpr SignalDesc X_SIGNALS[] = { {name, Provider, Flow, Timing, ctx, ownership}, ... }` + `constexpr HarnessDesc X_DESC`. Pure data; the docs generator reads it; the checker verifies row count == declared count. | The connector's datasheet. One row per pin. |
| **connect function** | `connectX()` declared in the header, defined only in `harness/defs/harness_connect.cpp`. Builds the static struct on first call (idempotent), runs the provider side's init contract, applies the trace taps, returns the struct by const reference. | The act of mating the connector. Draw it as the arrow from the consumer's init code into the harness, with the init cascade it triggers hanging below. |

Enumerations in the signal table (`harness/defs/harness_signal.hpp`):

- `Provider::Lower | Upper` — which side implements the interface. Lower = closer to
  hardware (L1 is top, L5 is bottom; the task sheet sits above L3).
- `Flow::Down | Up | Bidir` — call direction. Since Phase 3 every signal is "upper calls
  lower" except the rule-3 exception and the index-pulse callback.
- `Timing::Sync | AsyncQueue | AsyncFlag` — a direct call that returns a result; a call that
  enqueues for another task; or a flag/ring written in one context and polled in another.
  **Suggested arrow styles**: solid for Sync, double-line for AsyncQueue, dashed for AsyncFlag.
- `Ctx` bitmask: `PreScheduler` (init only), `Task`, `ISR`. Draw an ISR-capable signal with a
  lightning glyph; a PreScheduler-only signal in grey.
- `ownership` — one line stating who owns buffers and lifetimes. Put it in a tooltip.

## 2. What surrounds the harness headers

- `harness/pins/` (41 files) — PODs, enums and init contracts shared by more than one
  sheet: `MotorCommand`, `AsyncEvent`, telemetry types, pin bundles, `SeqSnapshot<T>`,
  `IResponseSink`, `MotorDebugParams`, `DispatchStats`, the EXTI callback table, the
  microsecond-clock accessor, and the `init*Board` / `init*Driver` contracts. No pins file
  includes any layer header (checker rule 8). Draw pins as a bus of plain wires that any
  sheet may tap, distinct from the connectors.
- `harness/taps/` (6 files) — `TracedLock`, `TracedSPIBus`, `TracedMotorDriver`,
  `TracedTransport`, `TracedEncoder`, and `applyTap<Tap, Slot, Iface>()`. A tap is a class that
  implements the same interface as the thing it wraps, logs the call, forwards it. Taps exist
  only when `ENABLE_INTERFACE_TRACE` is defined (firmware-debug preset); in release
  `applyTap` returns the raw object. Draw a tap as a small probe clipped onto one pin.
- `harness/trace/` (2 files) — the `ITRACE()` macro, `ITrace::log()`, the sink. A global net:
  L2, L3 and the taps include it directly. No-op in release.
- `harness/defs/harness_connect.cpp` — the **only** file that applies taps and the only
  file that defines connect functions (checker rules 3 and 6).

Enforcement, which a diagram can show as a "design-rule check" box:
`scripts/arch/check_includes.py --strict` (which sheet may include which header),
`scripts/arch/check_harness_defs.py --strict` (8 rules: header includes, row counts, connect
definitions, single interface definition, pins hold no abstract classes, taps only under
harness/, no adapters, no forwarders), `scripts/arch/gen_architecture.py --check` (docs match
tables). All three run from the CMake target `arch-check`.

## 3. The six harnesses

### H_L4_L5 — Drivers ↔ Board (`harness_l4_l5.hpp`, 8 signals)

Participants: L4 device drivers (consumer, upper) and L5 register-level board code
(provider, lower).

Struct `L4L5Harness`: five **pin bundles** (`MotorPins`, `LCDPins`, `JoystickPins`,
`FlashPins`, `EncoderPins`), `IInternalFlash*`, `IBoardDebug*`, and a function pointer
`registerExti(line, callback)`.

Pin bundles are PODs of interface pointers, e.g. `MotorPins { IGPIO* cs, stbyRst, busy,
flag; ISPIBus* spi }`. Draw each bundle as a small sub-connector carrying 3–6 wires.

Signals (provider = Lower for all):

| Signal | Flow | Timing | Ctx | What really happens |
|---|---|---|---|---|
| `Board::ISPIBus` (SPI1 motor+LCD, SPI2 flash) | Down | Sync | Task, PreScheduler | `lock()` takes the per-bus Platform mutex, `transfer()`/`writeOnly()` poll the SPI data register (LCD uses DMA for bulk writes), `unlock()`. Motor at 2.6 MHz, LCD at 42 MHz, each driver sets its prescaler per transaction. |
| `Board::IGPIO` (per pin) | Down | Sync | Task, ISR, PreScheduler | direct register read/write |
| `Board::ITimerCounter` (TIM4) | Down | Sync | Task | quadrature count read/reset |
| `Board::IEncoderSampler` (TIM3+DMA1) | Down | **AsyncFlag** | Task | DMA fills a 256-entry ring at 1 kHz; the consumer reads the write index |
| `Board::ITearingSync` (EXTI0) | Down | **AsyncFlag** | Task | ISR at NVIC 4 sets a flag; `waitForVSync` busy-waits ≤ 18 ms |
| `Board::IInternalFlash` | Down | Sync | Task, PreScheduler | erase/program blocks |
| `Board::IBoardDebug` | Down | Sync | Task | raw bring-up commands |
| `registerExti` (index pulse) | **Up** | **AsyncFlag** | **ISR** | L4 registers a callback pre-scheduler; the EXTI9_5 ISR (NVIC 5) calls it |

`connectL4L5()` side effects, in order: `initMotorBoard`, `initLCDBoard`,
`initJoystickBoard`, `initFlashBoard`, `initEncoderBoard` (all L5 init contracts), and
`TracedSPIBus` taps on SPI1 (slot 0) and SPI2 (slot 1). Optional hardware leaves its
bundle pointer null.

### H_L3_L4 — Services ↔ Drivers (`harness_l3_l4.hpp`, 7 signals)

Struct `L3L4Harness`: seven pointers, null when absent: `IMotorDriver* motor`,
`ICanvas* canvas`, `IJoystick* joystick`, `IFlash* flash`, `IEncoderHardware* encoderHw`,
`IConfigStore* configStore`, `IDebugAccess* debug`.

All seven are `Provider::Lower, Flow::Down, Timing::Sync`. Contexts are Task plus
PreScheduler for motor/canvas/configStore, and Task plus ISR for `IEncoderHardware` whose
`indexISR()` is called from the EXTI path and whose `readIndex()` returns a seqlock snapshot.

`connectL3L4(clock)` side effects: `initMotorDriver`, `initDisplayDrivers`,
`initEncoderDrivers(clock, usClock, 1000 Hz)`, `initDebugAccess`, `initConfigStore` (L4 init
contracts, each of which calls `connectL4L5()` for its bundle), then `TracedMotorDriver`
and `TracedEncoder` taps. Called first by `initMotorSubsystem`; later callers get the cached
struct.

Runtime flows through this harness (draw as sequences):

- **Motion**: Motor task → `MotorController::processCommand` → `IMotorDriver::move/run/...`
  → `PowerSTEP01::sendCommand` → SPI1 lock → 1–4 bytes → unlock.
- **Status poll**: Motor task every 50 ms → `getStatus`, `getParam(ABS_POS)`,
  `getParam(SPEED)` (three separate lock/unlock pairs) → `TelemetryStore`.
- **Render**: Display task every 50 ms → `ICanvas` draw calls, each locking SPI1 for its
  own window write; the indicator renderer waits for vsync *before* locking.
- **Remote bitmap**: Comms task → `ICanvas::streamBitmapStart` (locks SPI1) → N ×
  `streamBitmapData` (64-byte chunks via DMA) → `streamBitmapEnd` (unlocks). See
  `03_open_issues.md` item 1.
- **Encoder**: Encoder task every 10 ms → `sampleWriteIndex/sampleBuffer` (DMA ring) and
  `readIndex()` (seqlock).

### H_L2_L3 — Protocol ↔ Services (`harness_l2_l3.hpp`, 14 signals)

Struct `L3Dispatchers`: thirteen references, one per L3 dispatcher interface:
`IMotionDispatcher`, `IMotorConfigDispatcher`, `ISafetyDispatcher`, `IQueueDispatcher`,
`IControlModeDispatcher`, `ISupervisorTrimDispatcher`, `IEncoderDispatcher`,
`IMotorDriverDispatcher`, `IDisplayDispatcher`, `IFlashImageDispatcher`, `ISystemDispatcher`,
`ITraceDispatcher`, `IDebugDispatcher`. Struct `L2L3Harness` holds that bundle (down) plus
`Protocol::ICommandProcessor& processor` (up).

Thirteen signals are `Lower, Down, Sync, Task`: L2's `ServiceDispatcher` (one class
implementing the composite `Protocol::ICommandDispatcher`) forwards each call to the matching
L3 dispatcher. The fourteenth, `Protocol::ICommandProcessor`, is `Upper, Up, Sync, Task`: L3's
`CommsService` calls `process()`, `checkBaudRevert()`, `emitEvent()`, `emitTelemetry()`,
`emitBanner()` on L2.

Draw this harness as a 13-wire ribbon down and a 1-wire return up. Everything crosses by
value or const reference; the dispatchers return `Harness::ServiceStatus` codes and L2 does
all formatting (`OK …`, `ERROR …`, JSON, `EVT`, `TLM`).

`connectL2L3(dispatchers, processor)` has no side effects and no taps.

### H_L1_L2 — Transport ↔ Protocol (`harness_l1_l2.hpp`, 1 signal)

Struct `L1L2Harness { Transport::ITransport& transport }`. One signal, `Lower, Down, Sync,
Task + PreScheduler`. L2 calls `available()`, `readByte(byte, timeoutMs)`, `print()`,
`println()`, `flush()`, `setBaudRate()`. `connectL1L2(raw)` wraps the transport in
`TracedTransport`.

### H_L5_L1 — the documented exception (`harness_l5_l1_bytes.hpp`, 1 signal)

Rule 3: host command bytes skip L4, L3 and L2. `Transport::IByteChannel` is **owned by L1**
and implemented by two L5 classes, `Board::UartHardware` (USART2, ISR-fed ring) and
`Board::RttChannel` (SEGGER RTT). Struct `L5L1ByteHarness { IByteChannel& channel }`. The
signal is `Lower, Up, AsyncFlag, Task + ISR`: the USART2 ISR (NVIC 6) is the producer, the
Comms task the consumer, and the ISR may wake the Comms task through
`Platform::ITaskNotifier` (registered by the composition root via `setCommsRxWake`).

`connectL5L1Bytes(CommsChannel)` is called **by L1** (`transport_init.cpp`); it runs the L5
`initCommsBoard` contract and returns the channel. No taps.

Draw this as a dashed bypass arrow from L5 straight up to L1, labelled "bytes only", with a
lightning glyph (ISR producer) and a small "wake" arrow to the Comms task.

### H_TASKS_L3 — Task sheet ↔ Services (`harness_tasks_l3.hpp`, 4 signals)

No struct. The header includes the four L3 service interfaces: `IMotorService`
(`onCommand`, `periodicUpdate`), `ICommsService` (`onStart`, `tick`), `IEncoderService`
(`sample`), `IDisplayService` (`tick`). All four are `Lower, Down, Sync, Task`: the task
is the upper side and calls down into L3. Nothing flows back; task handles are not a signal.

There is no connect function. The composition root calls `Scheduler::init*Task(service,
…)`, which stores the service pointer in the task body and creates the FreeRTOS task.

Draw the task sheet above L3 with four single-wire connectors, one per task, each labelled
with the task's wait primitive (queue receive 50 ms; notification 10 ms; sleepUntil 10 ms;
sleepUntil 50 ms).

## 4. The one power-net connection that is not a harness

`Harness::connectSPIManager(spi1Lock, spi2Lock)` (declared in `pins/spi_init.hpp`, defined in
`harness_connect.cpp`) wraps the two Platform mutexes in `TracedLock` taps (slots 0 and 1)
and hands them to the L5 `initSPIManager` contract, which constructs the two `SPIHardware`
buses. Draw it as a short link from the PLATFORM rail to L5 with two probes on it.

## 5. The connect call tree (who mates which connector, and when)

```
System::initPlatform      -> connectSPIManager(locks)            [Platform locks -> L5 SPI manager]
Services::initMotorSubsystem
   -> connectL3L4(clock)  -> Drivers::initMotorDriver   -> connectL4L5() -> L5 init*Board x5
                           -> Drivers::initDisplayDrivers -> connectL4L5() (cached)
                           -> Drivers::initEncoderDrivers -> connectL4L5() (cached)
                           -> Drivers::initDebugAccess    -> connectL4L5() (cached)
                           -> Drivers::initConfigStore    -> connectL4L5() (cached)
Services::initEncoderSubsystem / initDisplaySubsystem -> connectL3L4 (cached)
Services::initCommsSubsystem
   -> connectL3L4 (cached)
   -> Protocol::initCommsProtocol
        -> Transport::initTransport -> connectL5L1Bytes(VCP_UART) -> Board::initCommsBoard
        -> connectL1L2(rawTransport)
   -> connectL2L3(dispatchers, parser)
System::initTasks         -> Scheduler::init{Motor,Encoder,Display,Comms}Task(service)
```

Every connect is memoised: the first call builds a function-local static and runs the init
cascade; later calls return the same struct. No heap in the harness itself (the L5
`SPIManager` does `new` its two bus objects; see `03_open_issues.md`).

## 6. Three end-to-end traversals worth drawing as sequence diagrams

**A. Host command `MOVE 1000`**
USART2 ISR (L5, NVIC 6) pushes bytes into the ring and notifies the Comms task →
Comms task `CommsService::tick` → `CommandParser::process` reads bytes through H_L1_L2 →
line complete → `parse` → `cmdMove` handler → `ServiceDispatcher::motion…` through H_L2_L3
→ `MotionDispatch` → `Motion::move` → `MotorService::sendCommand` (bounded 50 ms send into
`s_motorCmdQueue`) → L2 prints `OK`. Motor task: `queue.receive` → `MotorService::onCommand`
(ESTOP latch check) → `MotorController::processCommand` → `IMotorDriver::move` through
H_L3_L4 → `PowerSTEP01` → `ISPIBus` through H_L4_L5 → SPI1 wire.

**B. ESTOP**
Comms task parses `ESTOP` → `SafetyDispatch` → `CommandQueue::emergencyStop` (clears the
staged queue) → `IImmediateStop::immediateHardHiZ` (`MotorService`): atomic latch, reset
`s_motorCmdQueue`, `MotorController::emergencyHiZ` → `hardHiZ` on SPI1 → enqueue HardHiZ.
Motor task, next pass (≤ 50 ms): `applyPendingTransitions` → `onEmergencyStop` (sysid abort,
trim reset, supervisor idle, loops inhibited, Hi-Z re-asserted); motion commands refused
until `CLEAR` → `clearFault` → `clearImmediateStop`.

**C. Telemetry**
Motor task every 50 ms writes motor status/position/speed and system telemetry into
`TelemetryStore` (mutex). Encoder task every 10 ms writes encoder telemetry (velocity,
quality, index snapshot). Comms task every 100 ms (`TELEMETRY_PERIOD_MS`), if enabled,
`getSnapshot()` → `ICommandProcessor::emitTelemetry` → L2 formats `TLM …` → H_L1_L2 →
UART. Async events (`AsyncEvent {type, statusReg}`, 8 types) travel Motor task →
`s_eventQueue` → Comms task `tick` → `emitEvent` with a sequence number.
