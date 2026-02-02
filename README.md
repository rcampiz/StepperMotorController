# STM32F401RE Stepper Motor Controller

## Project Vision

A **precision stepper motor controller** for a **mecanum-wheel robotic platform**, where **four identical controllers** (one per wheel) are coordinated by a Raspberry Pi host for synchronized motion.

**Target Application:** Mecanum-wheel robot with:
- **Synchronized multi-axis control** - Four controllers start motion simultaneously
- **Closed-loop positioning** with quadrature encoder feedback
- **Sub-millisecond coordination** via ARM/START protocol or timestamp-based sync
- **Latency measurement** for host-side compensation (PING/PONG with timestamps)
- **Dual-mode LCD display** - Local UI or host-controlled rendering
- **Windows + Raspberry Pi** host support via USB VCP

This firmware is designed for deterministic, real-time motor control where the host issues high-level commands and the MCU handles precise step generation.

## System Architecture

```
                              Raspberry Pi / Windows Host
                                          |
              +------------+--------------+--------------+------------+
              |            |              |              |            |
              v            v              v              v            |
       +----------+  +----------+  +----------+  +----------+        |
       | Wheel FL |  | Wheel FR |  | Wheel RL |  | Wheel RR |        |
       | STM32    |  | STM32    |  | STM32    |  | STM32    |        |
       +----------+  +----------+  +----------+  +----------+        |
                                                                     |
       Host -> USB -> ST-LINK/J-LINK -> MCU (VCP UART or RTT)        |
```

Each controller runs identical firmware. The host coordinates them via:
1. **Command queuing** - Buffer multiple motion commands
2. **ARM/START** - Prepare commands, then trigger simultaneous execution
3. **PING/PONG** - Measure communication latency for compensation
4. **GET_TICK** - Synchronize tick counters across controllers

## Hardware Platform

| Component | Model | Purpose |
|-----------|-------|---------|
| **MCU Board** | NUCLEO-F401RE | ARM Cortex-M4 @ 84MHz, 512KB Flash, 96KB RAM |
| **Motor Driver** | X-NUCLEO-IHM03A1 | powerSTEP01 stepper driver (voltage/current mode) |
| **Display Board** | X-NUCLEO-GFX01M2 | ST7789 LCD (240x240), NOR flash, 5-way joystick |
| **Encoder** | External quadrature | A/B channels + index pulse (Z) |
| **Debug Interface** | ST-LINK/V2-1 | On-board, convertible to J-Link OB |

## Key Features

### Synchronized Motion Control
- **ARM/START protocol** - Queue commands, arm, then trigger all controllers simultaneously
- **Timestamp-based sync** - START_AT command for precise scheduling
- **Monotonic tick counter** - 1 microsecond resolution for timing
- **Deterministic execution** - Motion driven by hardware timers, not host timing

### Motor Control
- Full powerSTEP01 command set (move, goto, run, stop, hi-z)
- Configurable acceleration, deceleration, and max speed
- Position tracking with 32-bit absolute position
- Stall detection and fault monitoring

### Dual-Mode LCD Display
- **LOCAL mode** - MCU-owned UI with joystick navigation
  - Status, Motor, Encoder, System, Debug pages
  - Menu and Terminal screen abstractions
- **REMOTE mode** - Host-controlled rendering
  - Clear, text, rectangle, line, bitmap commands
  - Joystick events forwarded upstream

### Latency Measurement
- **PING/PONG** with timestamps for round-trip measurement
- MCU captures tick at RX and TX for precise timing
- Host can compensate for communication jitter

### Encoder Feedback
- Hardware quadrature decoding via TIM2 encoder mode
- 32-bit position counter
- 100 Hz velocity calculation
- Index pulse detection for homing

### Communication
- ASCII command protocol over USB VCP (USART2)
- Works from **Windows** (COM port) and **Raspberry Pi** (/dev/ttyACM0)
- Optional SEGGER RTT for high-speed debugging

### Safety
- **ESTOP** command for immediate stop
- Fault detection and reporting
- Explicit recovery sequence (CLEAR_FAULT -> ENABLE)

## Command Protocol Summary

### Motion Commands

| Command | Description |
|---------|-------------|
| `MOVE <steps> <dir>` | Relative move |
| `GOTO <position>` | Absolute move |
| `RUN <speed> <dir>` | Continuous rotation |
| `STOP [hard]` | Decelerate or immediate stop |
| `ESTOP` | Emergency stop |

### Synchronization Commands

| Command | Description |
|---------|-------------|
| `QUEUE <cmd> <params>` | Add to command queue |
| `ARM` | Prepare queued commands |
| `START` | Begin execution |
| `START_AT <tick>` | Begin at specified tick |
| `PING <seq>` | Latency measurement |
| `GET_TICK` | Query tick counter |
| `GET_STATUS` | Query controller state |

### UI/Display Commands

| Command | Description |
|---------|-------------|
| `UI_MODE [LOCAL\|REMOTE]` | Get/set UI mode |
| `DISP_CLEAR [color]` | Clear display (REMOTE mode) |
| `DISP_TEXT <x> <y> <fg> <bg> <text>` | Draw text |
| `DISP_RECT <x> <y> <w> <h> <color> [fill]` | Draw rectangle |
| `DISP_LINE <x1> <y1> <x2> <y2> <color>` | Draw line |
| `DISP_BITMAP_B64 <x> <y> <w> <h> <b64>` | Draw bitmap |

See [docs/HOST_INTERFACE_AND_SYNC.md](docs/HOST_INTERFACE_AND_SYNC.md) for complete protocol specification.

## Project Status

| Component | Status |
|-----------|--------|
| Project structure & build system | **Complete** |
| FreeRTOS task framework | **Complete** |
| SEGGER RTT/SystemView | **Complete** |
| Command parser protocol | **Complete** |
| Services (tick timer, command queue, device config) | **Complete** |
| Encoder driver (TIM2 + index interrupt) | **Complete** |
| Motor control (powerSTEP01) | **Complete** |
| LCD display (dual-mode UI) | **Complete** |
| Remote display commands | **Complete** |
| Screen abstractions (Menu, Terminal) | **Complete** |
| UART transport | *Scaffolded* |
| Closed-loop control | *Not started* |

**Current State:** Motor control and dual-mode LCD UI framework complete. Project compiles successfully (42.8KB code). Remaining work includes UART transport completion and closed-loop PID control.

## Quick Start

### Prerequisites

1. **ARM GCC Toolchain** (14.x recommended)
   - Download: https://developer.arm.com/downloads/-/gnu-rm

2. **CMake** (3.20+) and **Ninja** (optional)
   - CMake: https://cmake.org/download/
   - Ninja: https://ninja-build.org/

3. **VS Code Extensions** (recommended)
   - C/C++ (Microsoft)
   - Cortex-Debug

### Building

**Option 1: Direct Build Script (Windows)**

```batch
scripts\build_direct.bat
```

**Option 2: CMake**

```bash
# Configure (first time only)
mkdir build && cd build
cmake -G Ninja ..

# Build
ninja

# Output: build/StepperMotorController.elf
```

### Testing from Windows

```python
import serial

# Connect via COM port (VCP)
ser = serial.Serial('COM3', 115200, timeout=1)

# Query tick
ser.write(b'GET_TICK\n')
print(ser.readline())

# Measure latency
ser.write(b'PING 1\n')
print(ser.readline())  # PONG with timestamps

# Queue and execute motion
ser.write(b'QUEUE MOVE 1000 1\n')
ser.write(b'ARM\n')
ser.write(b'START\n')

# Switch to remote display mode
ser.write(b'UI_MODE REMOTE\n')
ser.write(b'DISP_CLEAR 001F\n')  # Blue background
ser.write(b'DISP_TEXT 10 10 FFFF 001F Hello\n')
```

## Documentation

| Document | Description |
|----------|-------------|
| [docs/HOST_INTERFACE_AND_SYNC.md](docs/HOST_INTERFACE_AND_SYNC.md) | **Synchronization protocol and command reference** |
| [docs/DISPLAY_ARCHITECTURE.md](docs/DISPLAY_ARCHITECTURE.md) | **Dual-mode LCD UI framework** |
| [docs/PROJECT_STRUCTURE.md](docs/PROJECT_STRUCTURE.md) | Directory layout and design rationale |
| [docs/TASK_ARCHITECTURE.md](docs/TASK_ARCHITECTURE.md) | FreeRTOS task design and priorities |
| [docs/COMMUNICATION_ARCHITECTURE.md](docs/COMMUNICATION_ARCHITECTURE.md) | Transport layer design |
| [docs/PIN_ASSIGNMENTS.md](docs/PIN_ASSIGNMENTS.md) | Hardware pin mappings |
| [docs/IMPLEMENTATION_SUMMARY.md](docs/IMPLEMENTATION_SUMMARY.md) | Implementation notes and phase history |
| [docs/SYSTEMVIEW_INTEGRATION.md](docs/SYSTEMVIEW_INTEGRATION.md) | SEGGER SystemView setup |

## What's Next

### Remaining Implementation

1. **UART Transport** - Complete USART2 initialization and DMA
2. **Command Dispatch** - Wire motion commands to powerSTEP01 driver
3. **Telemetry Publishing** - Format and transmit telemetry over transport
4. **Closed-loop Control** - PID control using encoder feedback
5. **Multi-controller Testing** - Synchronized start across 4 controllers

### Future Enhancements

- Binary bitmap streaming (DISP_BITMAP command)
- Persistent UI mode preference
- Additional screen types (graphs, gauges)

## License

This project is provided as-is for educational and development purposes.

## References

- [STM32F401RE Reference Manual (RM0368)](https://www.st.com/resource/en/reference_manual/rm0368-stm32f401xbc-and-stm32f401xde-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [NUCLEO-F401RE User Manual (UM1724)](https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf)
- [X-NUCLEO-IHM03A1 User Manual](https://www.st.com/resource/en/user_manual/um2032-getting-started-with-the-xnucleoihm03a1-high-power-stepper-motor-driver-expansion-board-based-on-powerstep01-for-stm32-nucleo-stmicroelectronics.pdf)
- [X-NUCLEO-GFX01M2 User Manual](https://www.st.com/resource/en/user_manual/um2739-xnucleogfx01m2-graphics-expansion-board-stmicroelectronics.pdf)
- [powerSTEP01 Datasheet](https://www.st.com/resource/en/datasheet/powerstep01.pdf)
- [ST7789 Datasheet](https://www.waveshare.com/w/upload/a/ae/ST7789_Datasheet.pdf)
- [FreeRTOS Documentation](https://www.freertos.org/Documentation/RTOS_book.html)
