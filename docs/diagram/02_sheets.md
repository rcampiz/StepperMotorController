# How each sheet works (text for a diagramming agent)

Source of truth: commit `c9a35e5` on `master`, 2026-10-05. Each section says what the sheet
is for, what is inside it, which task context runs it, what state it owns, and how to draw
it. "Sheet" = one directory = one node; sub-blocks are suggested sub-nodes.

## L1 Transport — `server/layers/L1_transport/`, `Transport::`, 7 files

Purpose: turn a byte channel into a line-oriented, flushable text transport for L2.

Inside:
- `boundary/itransport.hpp` — `ITransport`: `init`, `available`, `readByte(byte, timeoutMs)`,
  `print`, `println`, `flush`, `setBaudRate`.
- `boundary/ibyte_channel.hpp` — `IByteChannel`: the rule-3 contract L5 implements (read
  one byte from an ISR-fed ring, write bytes, set baud).
- `UartTransport : ITransport` — over `UartHardware`; `readByte` polls the ring with a
  clock-based timeout; `setBaudRate` forwards (115200/230400/460800/921600).
- `RttTransport : ITransport` — over `RttChannel`; polling, no baud.
- `init/transport_init.cpp` — `initTransport(out, type, clock)`: calls
  `connectL5L1Bytes(type)` and constructs the matching transport (static storage).

Context: Comms task (reads/writes), pre-scheduler (init). Owns: nothing shared; the RX ring
belongs to L5. Draw as a thin adapter box with one connector up (H_L1_L2) and the dashed
bypass down to L5.

## L2 Protocol — `server/layers/L2_protocol/`, `Protocol::`, 19 files

Purpose: SCPI-style ASCII (optionally JSON) command protocol. All host-facing bytes are
formatted here and nowhere else.

Inside:
- `CommandParser : ICommandProcessor` — `process()` drains the transport byte by byte into
  a line buffer (backspace handling, echo), on CR/LF parses into `ParsedCommand {cmd, args[],
  argCount}`, looks up the handler in static tables (prefix table + ~110 entries across
  motion, config, queue, control, encoder, driver, display, flash, system, trace, safety,
  `DBG:` and `EVT:` groups), updates `DispatchStats` (totals, unknown, parse errors, last 8
  names), runs the handler. Responses: `OK <msg>` / `ERROR <msg>` or JSON. Also
  `emitBanner`, `emitEvent` (`EVT …` with sequence and timestamp), `emitTelemetry` (`TLM …`),
  `emitJoystick`, `checkBaudRevert` (auto-revert if no valid command after a baud switch).
- `handlers/*.cpp` — nine files, one per command group; each handler validates arguments and
  calls one `ICommandDispatcher` method.
- `ServiceDispatcher : ICommandDispatcher` — the composite of the 13 `Services::I*Dispatcher`
  interfaces; `bind(const L3Dispatchers&)` then pure forwarding.
- `event_codec` — event name/mask encoding.
- `TransportSink : Harness::IResponseSink` — lets L3 debug commands write lines back
  through L2 without L3 knowing the transport.
- `init/comms_protocol_init.cpp` — cascades to L1, connects H_L1_L2, binds the dispatcher,
  builds the parser.

Context: Comms task only. Owns: the line buffer, dispatch statistics, baud-revert timer,
output mode (ASCII/JSON). Draw as parser → handler table → composite dispatcher, with the
`DBG:UNLOCK` gate noted on the debug group.

## L3 Services — `server/layers/L3_services/`, `Services::`, 172 files

Purpose: all behaviour and all safety. Host-testable: no RTOS loops, only Platform
interfaces. Runs in four task contexts, so draw it as four coloured regions plus shared state.

Sub-blocks (directories) and what runs where:

| Sub-block | Key classes | Task context |
|---|---|---|
| `motion/motor_service` | `MotorService` : `IMotorService` + `IMotorCommandSink` + `IMotorTaskControl` + `IImmediateStop`. Motor task body; ESTOP latch; step-mode message; debug pause. | Motor task (entry), Comms task (sink/control/latch) |
| `motion/motor_controller` | `MotorController` : `IMotorControlService`. `processCommand` (Move/GoTo/Run/stops/HiZ/home/mark/config/status), `periodicUpdate` (status read → telemetry → sysid → speed trim → following supervisor → events), `emergencyHiZ`, `onEmergencyStop`, `clearEmergencyStop`, `setStepModeSafe`, `reinit`, `applyConfig`, `getDebugInfo`. | Motor task (plus three Comms-task calls, see issues) |
| `motion/following_supervisor` | `FollowingSupervisor`: states IDLE/MOVING/HOLDING/RECOVERY/FAULT, tiers OBSERVE/SOFT_CORRECT/PAUSE_RECOVER/FAULT_LATCH; compares commanded vs encoder position. | Motor task |
| `motion/speed_trim_controller`, `motion/pid_controller` | PI trim on top of the base speed (`SPEED_TRIM` mode). | Motor task |
| `motion/sysid` | `SysIdRunner`: test types/phases; drives `run()` directly while active. | Motor task |
| `motion/control_mode` | `ControlModeManager` : `IEncoderStatusSink`; modes OPEN_LOOP / CLOSED_LOOP_MONITOR / SPEED_TRIM; mutex. | Comms + Motor + Encoder tasks |
| `motion/encoder_service` | `EncoderService` : `IEncoderService` + `IEncoder` + `IEncoderFilterControl`. Reads the DMA ring, window velocity, filter chain, seqlock index snapshot, critical-section snapshot for readers. | Encoder task (entry), Comms task (readers) |
| `motion/encoder_processor` | `EncoderProcessor` : `IEncoderProcessing`. Filter chain EMA → SMA → Padé → Butterworth biquad → notch → Holt → deadband; velocity quality by edge age. | Encoder task |
| `motion/motion_service`, `motion/*_dispatch` | `Motion::run/move/goTo/stop/enable` (sends into the motor queue, refuses while ESTOP latched) and the 13 dispatcher classes implementing `I*Dispatcher`. | Comms task |
| `motion/motor_config`, `config/` | `MotorConfigManager` + `StepperConfig`/`EncoderConfig`/`ControlConfig` views; `DeviceConfigManager`; persisted through `IConfigStore`. | Comms task (writes), all (reads) |
| `motion/motor_event_service` | `MotorEventManager` : `IMotorEventReceiver`; posts `AsyncEvent` into the event queue. | Motor task |
| `dispatch/command_queue` | `CommandQueue` : `IEmergencyStop`. Staged ARM/START/START_AT queue: states IDLE/ARMED/RUNNING/FAULT/ESTOP; `flushToMotorTask` with bounded sends; `emergencyStop`, `clearFault`. | Comms task |
| `comms/comms_service` | `CommsService` : `ICommsService` + `ISafetyActions` + `IDispatchStats`. `onStart` (banner), `tick` (process, baud revert, telemetry every 100 ms, event drain, heartbeat timeout 100–5000 ms). | Comms task |
| `comms/system_dispatch`, `comms/trace_dispatch` | system info/telemetry snapshot/baud; trace ring access. | Comms task |
| `debug/` | `DebugService` : `IDebugDispatcher`. Locked until `DBG:UNLOCK`; SPI_STOP/START → motor debug-pause flag, LCD_DISABLE → display enable flag, MOTOR_DEBUG → formatted registers, everything else → `IDebugAccess`. | Comms task |
| `infra/telemetry_store` | `TelemetryStore` : `ITelemetry` + `ISnapshotWriter`; `TelemetrySnapshot {motor, encoder, system, control, timestamp}` under a mutex. Global `g_telemetryStore`. | written by Motor + Encoder tasks, read by Comms + Display |
| `infra/trace`, `infra/tick_timer`, `infra/timing_service`, `infra/unit_conversion` | trace ring + sink adapter, TIM5-based tick timer, timing helpers, unit conversions. | mixed |
| `infra/flash_image_service`, `infra/indicator_service`, `infra/flash_dispatch` | NOR-flash image slots (SPI2); the rotating indicator renderer (vsync + bitmap stream on SPI1). | Comms task (flash commands), Display task (render) |
| `ui/` | `DisplayService` : `IDisplayService` + `IDisplayControl` + `IRemoteDisplay`; `ScreenManager`; 11 screens (boot colour, device info, encoder, motion, config, graph, image view, trace, task monitor, dispatcher, arch) + menu + terminal; joystick navigation (long press 1 s → main menu, hold auto-repeat); `UIModeManager` LOCAL/REMOTE. | Display task (tick), Comms task (remote drawing) |
| `boundary/` | 37 interface headers: the 13 dispatchers, the 4 task-entry services, and L3-internal contracts (`IMotorCommandSink`, `IImmediateStop`, `IMotorTaskControl`, `IDisplayControl`, `IEmergencyStop`, `ITelemetry`, …). | — |
| 5 `*_init.cpp` | `initSystemServices`, `initMotorSubsystem`, `initEncoderSubsystem`, `initDisplaySubsystem`, `initCommsSubsystem`: call `connectL3L4`, bind, init the services, return the entry points. No task creation. | pre-scheduler |

Shared state inside L3, worth drawing as a central store: `g_telemetryStore` (mutex),
`g_commandQueue` (mutex), `ControlModeManager` (mutex), `UI::g_uiMode` (mutex), the
`MotorService` atomics, `EncoderService` snapshot (critical section).

## L4 Drivers — `server/layers/L4_drivers/`, `Drivers::`, 21 files

Purpose: device protocols over the L5 pin bundles. No business rules.

- `PowerSTEP01 : IMotorDriver` — command bytes (Run 0x50, Move 0x40, GoTo 0x60, GoHome
  0x70, GoMark 0x78, ResetPos 0xD8, SoftStop 0xB0, HardStop 0xB8, SoftHiZ 0xA0, HardHiZ
  0xA8, GetStatus 0xD0, Set/GetParam), each transaction = lock SPI1, CS low, bytes, CS high,
  unlock. `MotorStatus` bit helpers.
- `LCD : ICanvas` (ST7789, `lcd_st7789.hpp`) — window set + pixel writes, text, lines,
  rects, bitmaps, and the streaming API (`streamBitmapStart/Data/End`) that holds SPI1 across
  calls; `waitForVSync` via `ITearingSync`.
- `SPIFlash : IFlash` (MX25L, SPI2), `Joystick : IJoystick` (5 GPIO), `Encoder :
  IEncoderHardware` (TIM4 count, DMA ring accessors, index ISR → `SeqSnapshot`),
  `InternalFlashConfigStore : IConfigStore`, `DebugAccess : IDebugAccess`.
- `init/*.cpp` — five contracts; each calls `connectL4L5()` and constructs its device
  statically.

Context: whichever task holds the harness pointer (Motor, Display, Comms, Encoder) plus the
index ISR. Draw as five device boxes, each with a bundle connector down to L5.

## L5 Board — `server/layers/L5_board/`, `Board::`, 35 files

Purpose: registers, peripherals, ISRs. The only sheet (with Platform) allowed to include
CMSIS.

- `gpio.hpp` (`GPIO`, `LED`, `enableExti` with explicit NVIC priority), `board_pins.hpp` (the
  pin map: SPI1 PA5/6/7, SPI2 PB13/PC2/PC3, motor CS PC8 / STBY PA9 / FLAG PA10 / BUSY PB5,
  LCD CS PC6 / TE PA0 / NRESET PA1 / DC PB10, flash CS PA8, joystick PC5/PA12/PB4/PB0/PC0,
  encoder PB6/PB7/PC9).
- `spi/` — `SPIHardware : ISPIBus` (polled transfer, DMA bulk write, per-call prescaler),
  `SPIBitBang`, `SPIManager` (`g_spiManager`, builds the two buses with the Platform locks).
- `uart/uart_hardware.hpp` — `UartHardware : Transport::IByteChannel`, USART2 ISR → SPSC ring
  → optional task notification. `rtt/rtt_channel.hpp` — `RttChannel : IByteChannel`.
- `timer/`, `dma/` — `TimerCounterTIM4 : ITimerCounter`, `EncoderSampler : IEncoderSampler`
  (TIM3 trigger + DMA1 stream into a 256-entry ring).
- `display/te_sync.cpp` — `TearingSyncEXTI0 : ITearingSync`, EXTI0 at NVIC 4, flag only.
- `isr/exti_handlers.cpp` — `EXTI9_5_IRQHandler` → harness callback table.
- `flash/internal_flash.cpp` — `InternalFlashSTM32F401 : IInternalFlash`.
- `debug/board_debug.cpp` — `BoardDebug : IBoardDebug`.
- `init/` — `clock_board` (HSE → PLL → 84 MHz, APB1 42, APB2 84), `spi_init`,
  `comms_board`, `motor_board`, `display_board`, `encoder_board` (EXTI priority constant 5).

Contexts: every task, plus ISRs USART2 (6), EXTI9_5 (5), EXTI0 (4). Draw as a peripheral
map with three lightning glyphs.

## TASKS — `server/foundation/F_platform/tasks/`, `Scheduler::`, 16 files

Purpose: the four RTOS loops and nothing else. Each `*_task.cpp` holds the priority, stack
size, period, the wait, and one call into L3. Each `*_task_init.cpp` (`Scheduler::init*Task`)
stores the service pointer and calls `IScheduler::createTask`.

| Task | Prio | Stack | Wait | L3 call |
|---|---|---|---|---|
| Motor | 4 | 1024 w | `motorCmdQueue.receive(cmd, 50 ms)` | `onCommand`, `periodicUpdate` every 50 ms |
| Comms | 3 | 2048 w | `ITaskNotifier::waitForNotification(10 ms)` | `onStart` once, `tick` |
| Encoder | 2 | 256 w | `sleepUntilMs(10)` | `sample` (task skipped without encoder) |
| Display | 1 | 640 w | `sleepUntilMs(50)` | `tick` |

Each loop also sets the trace task/service id before the call. Task handles live here
(`g_motorTaskHandle`, `g_commsTaskHandle`, `g_displayTaskHandle`); only the composition
root reads one of them (Comms, for the RX wake). Draw as four small loops above L3.

## PLATFORM — `server/foundation/F_platform/` (minus tasks), `Platform::`, 30 files

Purpose: the power net. RTOS-neutral interfaces plus their FreeRTOS and Null
implementations.

- `boundary/` — `IClock` (ms tick, delay, sleepUntil, ISR-safe tick), `IMicrosecondClock`,
  `ILock`, `IQueue<T,N>`, `IScheduler` (createTask, start, suspend/resume: unused),
  `ITaskStats` (free heap, per-task stack high-water marks), `ICriticalSection`,
  `ITaskNotifier`. Every one has a `Null*` for host tests.
- `rtos/` — `FreeRTOSClock`, `FreeRTOSMutex`, `FreeRTOSQueue`, `FreeRTOSScheduler`,
  `FreeRTOSTaskStats`, `FreeRTOSCriticalSection`, `FreeRTOSTaskNotifier`, `FreeRTOSConfig.h`,
  `freertos_hooks.cpp` (HardFault, stack overflow, malloc failed, idle),
  `platform_init.cpp` (`Platform::initPlatformBoard` → `PlatformResources`: 6 mutexes, 2
  queues, clock, µs clock, scheduler, stats, critical, notifier; all static).
- `hw/` — `microsecond_timer_tim5` (`Platform::initMicrosecondClockTIM5`), `early_debug*`
  (bare-metal USART2 TX for pre-scheduler prints, `Platform::debugPrint*`).
- `startup/` — vector table, linker script.

Draw as a rail along the bottom/side touching every sheet, with the `PlatformResources`
bundle as the plug the composition root hands out.

## HARNESS — `server/harness/`, `Harness::`, 57 files

See `01_harness_mechanics.md`. Sub-nodes: `defs/` (6 headers + `harness_connect.cpp`),
`pins/` (41), `taps/` (6), `trace/` (2).

## SYSTEM — `server/main.cpp`, `server/system_init.cpp`, `System::`, 3 files

Purpose: the wiring diagram. Rule 5: it may see every sheet.

Phases (each failure halts with a message on the early debug UART):
1. `initHardware` — clocks, early debug UART, TIM5 µs clock, tick timer, trace sink.
2. `initPlatform` — `initPlatformBoard`, `connectSPIManager`.
3. `initServices` — task-stats provider, `initSystemServices` (control mode, events,
   telemetry store, UI mode).
4. `initTasks` — for each subsystem: L3 init cascade, then `Scheduler::init*Task`; finally
   `setCommsRxWake`.
5. `start` — `vTaskStartScheduler`, never returns.

Draw as a numbered ladder on the left, with arrows into every sheet it touches.
