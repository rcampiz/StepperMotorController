# Review Notes

Date: 2026-02-02
Scope: Core sources and docs alignment.

## Findings

- ~~Medium: `Core/Inc/comms/uart_transport.hpp` drops RX bytes on ring-buffer overflow without reporting. Add an overflow counter/flag so host can detect truncated commands.~~ **RESOLVED** (2026-02-02): Added `m_overflowCount` to RingBuffer, plus `hasOverflowed()`, `getOverflowCount()`, `clearOverflow()` methods.
- ~~Medium: `Core/Src/comms/uart_transport.cpp:120-170` uses a tight `taskYIELD()` loop in `readByte()` for timeouts. This can spin hot under heavy load; consider `vTaskDelay(1)` or an ISR notification/semaphore.~~ **RESOLVED** (2026-02-02): Replaced `taskYIELD()` with `vTaskDelay(1)` to avoid CPU spin.
- ~~Medium: `Core/Src/comms/command_parser.cpp` uses `atol()` without range/format validation. Invalid or out-of-range args (e.g., negative speed, large accel) can overflow or be silently accepted. Add bounds checks and explicit error responses.~~ **RESOLVED** (2026-02-02): Added `Limits` namespace with powerSTEP01 register-based bounds. Validation added to MOVE, GOTO, RUN, ACCEL, DECEL, MAXSPD commands and their QUEUE variants.
- ~~Low: `Core/Inc/comms/uart_transport.hpp` relies on a global singleton. If multiple instances are ever created, ISR ownership is ambiguous. Consider enforcing single-instance creation or asserting in `init()`.~~ **RESOLVED** (2026-02-02): Added `configASSERT(s_instance == nullptr)` at start of `init()`.

## Documentation and alignment

- ~~Low: `docs/COMMUNICATION_ARCHITECTURE.md` and `docs/IMPLEMENTATION_SUMMARY.md` mark display commands as complete, but `DISP_BITMAP` binary streaming remains unimplemented (only `DISP_BITMAP_B64` with 512-byte decode limit). Call out the limitation explicitly in docs.~~ **RESOLVED** (2026-02-02): Both docs now state "binary streaming TBD" in status tables.
- ~~Low: `docs/IMPLEMENTATION_SUMMARY.md` notes command dispatch complete; this is now accurate, but the protocol section should mention argument validation is still minimal.~~ **RESOLVED** (2026-02-02): Added note after Phase 10 about minimal argument validation.

## Notes

## Display Requirements

- ~~Medium: Display requirements need to include J-Link image transfer. Add a path for pushing per-pixel image data over J-Link (RTT/J-Link tools) so the LCD can be filled with full-frame images.~~ **RESOLVED** (2026-02-02): DISP_BITMAP_B64 command implemented for base64-encoded image transfer (512 byte limit). Full binary streaming (DISP_BITMAP) remains TBD.
- ~~Medium: Implement an on-screen menu system (navigable UI) with clear input handling and state transitions.~~ **RESOLVED** (2026-02-02): MenuScreen class with up to 16 items, scrolling, selection highlight, and callbacks.
- ~~Medium: Add a debug/info display mode for printing runtime information to the screen.~~ **RESOLVED** (2026-02-02): TerminalScreen class with 20-line circular buffer for scrolling text console.
- ~~Medium: Define display modes and switching logic (e.g., Menu, Debug, Image Display) so the UI can be extended without mixing concerns.~~ **RESOLVED** (2026-02-02): UIMode enum (LOCAL/REMOTE), UIModeManager with thread-safe mode switching, IScreen interface for screen abstractions.

## Build warnings

- ~~2026-02-02: Core/Src/tasks/motor_task.cpp:20 warning: Tasks::s_motor defined but not used.~~ **RESOLVED** (2026-02-02): Driver now fully wired up.
- ~~2026-02-02: Core/Src/tasks/motor_task.cpp:21 warning: Tasks::s_spi defined but not used.~~ **RESOLVED** (2026-02-02): SPI bus now used by motor driver.
- ~~2026-02-02: Core/Src/tasks/comms_task.cpp:119 warning: snap set but not used in Tasks::publishTelemetry().~~ **RESOLVED** (2026-02-02): telemetry formatting now uses the snapshot.





## TODO feedback (options + assessment)

### Motor Task - **RESOLVED** (2026-02-02)

- ~~`Core/Src/tasks/motor_task.cpp:31` TODO init SPI + motor~~ **DONE**: Implemented in `MotorTask_Init()` - creates `SPIBus` with SPI1/Mode3 and `PowerSTEP01` instance.
- ~~`Core/Src/tasks/motor_task.cpp:56-108` TODO motor commands~~ **DONE**: All 14 command types implemented with direct mapping to PowerSTEP01 API.
- ~~`Core/Src/tasks/motor_task.cpp:123` TODO read motor status~~ **DONE**: Periodic poll at 50ms, reads STATUS/ABS_POS/SPEED, sign-extends position, publishes to telemetry.

### Display Task - **RESOLVED** (2026-02-02)

- ~~`Core/Src/tasks/display_task.cpp:32` TODO init SPI + LCD~~ **DONE**: LCD init and SPI use are implemented in `DisplayTask_Init()`.
- ~~`Core/Src/tasks/display_task.cpp:147-172` TODO render pages~~ **DONE**: Status/Motor/Encoder/System/Debug pages are rendered in `display_task.cpp`.

### Comms Task - **RESOLVED** (2026-02-02)

- ~~`Core/Src/tasks/comms_task.cpp:127-133` TODO telemetry formatting~~ **DONE**: `publishTelemetry()` now formats and prints telemetry without printf.


### UART Transport - **RESOLVED** (2026-02-02)

- ~~`Core/Src/comms/uart_transport.cpp:21-71` TODO USART2 GPIO/init/read/write~~ **DONE**: Bare-metal register access with interrupt-driven RX (256-byte ring buffer) and polling TX. PA2/PA3 configured as AF7, USART2 at 115200 8N1.

### Command Parser - Mostly Resolved

- Core/Src/comms/command_parser.cpp:839 TODO binary DISP_BITMAP streaming: Options: raw binary mode with length prefix vs chunked frames; assessment: chunked with explicit length + CRC is safer over RTT/serial.

- ~~`Core/Src/comms/command_parser.cpp:220-293` TODO command dispatch to motor + set params~~ **RESOLVED** (2026-02-02): All motion commands (MOVE/GOTO/RUN/STOP) and config commands (ENABLE/DISABLE/ACCEL/DECEL/MAXSPD) now dispatch to MotorTask queue.
- ~~`Core/Src/comms/command_parser.cpp:458` TODO get actual position/velocity~~ **RESOLVED** (2026-02-02): GET_STATUS now uses telemetry snapshot for motor.position and motor.speed.
- ~~`Core/Src/comms/command_parser.cpp:535-540` TODO home/zero~~ **RESOLVED** (2026-02-02): HOME dispatches GoHome, ZERO dispatches ResetPos to MotorTask queue.


- Medium: The codebase is currently named and structured around a mecanum-wheel robot. If this firmware is intended to be reused for other applications (e.g., projector screen lift, linear translation), rename modules/types and separate wheel-specific assumptions so the core control remains application-agnostic. **DEFERRED**: Architectural decision - application-specific naming is acceptable for current use case.






## Unfinished Development Gaps

- Binary `DISP_BITMAP` streaming is not implemented (`Core/Src/comms/command_parser.cpp:839`); only base64 transfer is supported with a 512-byte decode limit.
- Encoder index pulse enablement is still manual (`Encoder::enableIndexInterrupt()` not called in `Encoder::init()`), so index support remains opt-in.
- Closed-loop control and multi-controller testing remain unimplemented (per docs "Next Steps").

## Review of Prior AI Feedback

- ~~Assessment: Prior AI comments about motor task wiring, telemetry formatting, UART transport, and SPI mode safety were addressed and fixed in code (see MotorTask init/commands, comms telemetry formatting, UART IRQ RX + ring buffer, SPI setMode BSY wait).~~ **VERIFIED** (2026-02-02): All code fixes confirmed in place.
- ~~Assessment: Prior AI comments about UI framework and display modes were addressed (UIMode, IScreen, MenuScreen/TerminalScreen, display pages).~~ **VERIFIED** (2026-02-02): UI framework complete.
- ~~Assessment: Prior AI note on command dispatch gaps is now resolved for MOVE/GOTO/RUN/STOP/ACCEL/DECEL/MAXSPD/HOME/ZERO, but binary DISP_BITMAP streaming remains outstanding and should stay listed as a gap.~~ **VERIFIED** (2026-02-02): Command dispatch complete; DISP_BITMAP listed in gaps.
- ~~Assessment: Documentation alignment is mostly corrected for UART, command dispatch, and UI features, but should explicitly call out DISP_BITMAP binary streaming limitation and minimal argument validation in protocol docs.~~ **RESOLVED** (2026-02-02): Both COMMUNICATION_ARCHITECTURE.md and IMPLEMENTATION_SUMMARY.md now explicitly note "binary streaming TBD" and "minimal argument validation".
