# Phase 4: Deferred Tasks — Agent Briefing

**Status**: Not started
**Prerequisite**: Phases 1-3 complete and verified (2026-02-16)
**Parent plan**: ScpiServiceRefactorPlan.md

---

## Context

Phases 1-3 of the SCPI Service Refactor are complete:
- **Phase 1**: Two-level SCPI namespace dispatch in `command_parser.cpp`, safety-hardened `clearFault()`, all legacy flat commands preserved as aliases in `dispatchLegacy()`
- **Phase 2**: Service layer extraction — `Services::Motion`, `Services::Safety`, `Services::Config` as namespace free functions (no heap, no vtable, no RTTI). Parser calls services instead of `Tasks::` directly.
- **Phase 3**: Trace ring buffer (128 entries, ISR-safe, `TRACE_ENTRY`/`TRACE_EXIT` macros) with `DBG:TRACE:DUMP` and `DBG:TRACE:RESET` SCPI commands.

Build is clean (zero new warnings). GUI works unchanged via legacy aliases.

---

## Phase 4 Items

These are independent of each other unless noted.

### 1. Async Events (`!FAULT`, `!MOTION:COMPLETE`, `!STALL`)

Unsolicited messages pushed from firmware to client when faults occur, motion completes, or stall is detected. Currently the client polls via `GET_STATUS`. This would require both firmware (event emission) and client (event parsing) changes. The `!` prefix convention distinguishes async events from command responses.

### 2. Telemetry Streaming (`DIAG:TELEM:EN/DIS/RATE`)

SCPI commands to enable/disable/set rate of periodic telemetry pushes. Currently telemetry is polled by the client via `GET_STATUS`. Streaming would reduce latency and polling overhead.

### 3. Protocol Version Negotiation (`SYST:VER:PROTO?`)

Query command returning the SCPI protocol version so the client can detect firmware capabilities. `SYST:VER?` already exists (returns firmware version string); this would add a separate protocol contract version.

### 4. Display Service Extraction

Move display/LCD calls out of `command_parser.cpp` into a `Services::Display` namespace, following the same pattern as Motion/Safety/Config. Currently `cmdDispClear()`, `cmdDispText()`, `cmdDispRect()`, `cmdDispLine()`, `cmdDispBitmap()`, `cmdDispBitmapB64()` call display drivers directly from the parser.

### 5. Encoder Service Extraction

Move encoder calls into a `Services::Encoder` namespace. Currently `cmdEncoder()`, `cmdGetEncoderStatus()`, `cmdEncDebug()` call `Tasks::` directly from the parser.

### 6. Debug Command Lockout (`DBG:UNLOCK`/`DBG:LOCK`)

Gate access to `DBG:*` namespace behind an unlock command. Prevents accidental use of low-level debug commands (SPI_TEST, GPIO manipulation, etc.) during normal operation. All `DBG:*` commands would return an error unless `DBG:UNLOCK` is sent first.

### 7. Driver Swap Abstraction

Abstract the powerSTEP01 driver behind an interface so other stepper drivers could be substituted. Currently `motor_task.cpp` and config commands directly reference powerSTEP01 register layouts and SPI commands.

### 8. ISR-Driven CommsTask Wake (Replace 10ms Poll)

Currently `comms_task.cpp` polls UART at 10ms intervals via `vTaskDelay`. Replace with ISR-triggered task notification (`xTaskNotifyFromISR`) on UART RX, reducing latency from up to 10ms to near-zero and saving CPU cycles.

---

## Known Gap from Phases 1-3

The `DBG:SPI:*`, `DBG:GPIO:*`, and `DBG:PS01:*` SCPI aliases listed in the mapping table were not implemented. The ~50 inline debug commands (SPI_TEST, SPI_MODE_TEST, GPIO_STATE, GPIO_DUMP, PS01_DIAG, PS01_RESET, etc.) only work via their legacy flat names. Low priority since these are unstable bringup tools, not part of the stable SCPI contract.

---

## Key Files

| File | Role |
|---|---|
| `Core/Src/comms/command_parser.cpp` | Main parser (~3400 lines), all dispatch handlers |
| `Core/Inc/comms/command_parser.hpp` | Parser class with dispatch method declarations |
| `Core/Src/services/motion_service.cpp` | Motion service (wraps MotorTask) |
| `Core/Src/services/safety_service.cpp` | Safety service (ESTOP, fault clear, heartbeat) |
| `Core/Src/services/config_service.cpp` | Config service (ACC/DEC/MAXSPD with integer unit conversion) |
| `Core/Src/services/trace.cpp` | Trace ring buffer implementation |
| `Core/Src/tasks/comms_task.cpp` | UART polling loop (relevant to item 8) |
| `Core/Src/tasks/motor_task.cpp` | Motor command processing (relevant to items 1, 7) |
| `ScpiServiceRefactorPlan.md` | Full refactor plan with SCPI mapping tables |
| `COMMAND_REFERENCE.md` | Legacy command reference |

---

## Constraints

- 96KB RAM Cortex-M4 (STM32F401RE) — no heap in service layer, integer math only
- FreeRTOS — all inter-task communication via queues/notifications
- No vtable, no RTTI, no exceptions (`-fno-exceptions -fno-rtti`)
- C++14 (`-std=c++14`)
- Build via `scripts/build_direct.bat` (no make) — new .cpp files must be added to compile + link steps
- GUI (Python/PySide6) must not break — legacy aliases must always work
