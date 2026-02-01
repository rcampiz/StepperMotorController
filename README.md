# STM32F401RE Stepper Motor Controller

## Project Vision

A **precision stepper motor controller** for a **mecanum-wheel robotic platform**, where **four identical controllers** (one per wheel) are coordinated by a Raspberry Pi host for synchronized motion.

**Target Application:** Mecanum-wheel robot with:
- **Synchronized multi-axis control** - Four controllers start motion simultaneously
- **Closed-loop positioning** with quadrature encoder feedback
- **Sub-millisecond coordination** via ARM/START protocol or timestamp-based sync
- **Latency measurement** for host-side compensation (PING/PONG with timestamps)
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
| **Display Board** | X-NUCLEO-GFX01M2 | ST7789 LCD, NOR flash, 5-way joystick |
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

| Command | Description |
|---------|-------------|
| `MOVE <steps> <dir>` | Relative move |
| `GOTO <position>` | Absolute move |
| `RUN <speed> <dir>` | Continuous rotation |
| `STOP [hard]` | Decelerate or immediate stop |
| `QUEUE <cmd> <params>` | Add to command queue |
| `ARM` | Prepare queued commands |
| `START` | Begin execution |
| `PING <seq>` | Latency measurement |
| `GET_TICK` | Query tick counter |
| `GET_STATUS` | Query controller state |
| `ESTOP` | Emergency stop |

See [docs/HOST_INTERFACE_AND_SYNC.md](docs/HOST_INTERFACE_AND_SYNC.md) for complete protocol specification.

## Project Status

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Project Reorganization | Complete |
| 2 | Build Configuration (CMake) | Complete |
| 3 | SEGGER Source Download | Optional |
| 4 | SystemView Integration Prep | Complete |
| 5 | ST-LINK to J-Link Conversion | Optional |
| 6 | main.cpp Integration | Complete |
| 7 | Build Verification | Complete |
| 8 | Tick Timer Service | Complete |
| 9 | Command Queue + ARM/START | Complete |
| 10 | PING/PONG Timestamps | Complete |
| 11 | State Machine (IDLE/ARMED/RUNNING) | Complete |

**Current State:** Core synchronization features complete. Hardware drivers (powerSTEP01, LCD, UART transport) need implementation.

## Quick Start

### Prerequisites

1. **ARM GCC Toolchain** (14.x recommended)
   - Download: https://developer.arm.com/downloads/-/gnu-rm

2. **CMake** (3.20+) and **Ninja**
   - CMake: https://cmake.org/download/
   - Ninja: https://ninja-build.org/

3. **VS Code Extensions** (recommended)
   - C/C++ (Microsoft)
   - Cortex-Debug

### Building

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
```

## Documentation

| Document | Description |
|----------|-------------|
| [docs/HOST_INTERFACE_AND_SYNC.md](docs/HOST_INTERFACE_AND_SYNC.md) | **Synchronization protocol and requirements** |
| [docs/PROJECT_STRUCTURE.md](docs/PROJECT_STRUCTURE.md) | Directory layout and design rationale |
| [docs/TASK_ARCHITECTURE.md](docs/TASK_ARCHITECTURE.md) | FreeRTOS task design and priorities |
| [docs/COMMUNICATION_ARCHITECTURE.md](docs/COMMUNICATION_ARCHITECTURE.md) | Transport layer design |
| [docs/PIN_ASSIGNMENTS.md](docs/PIN_ASSIGNMENTS.md) | Hardware pin mappings |
| [docs/IMPLEMENTATION_SUMMARY.md](docs/IMPLEMENTATION_SUMMARY.md) | Implementation notes |

## Next Steps

### Phase 8-11: Synchronization Features

1. **Tick timer service** - Configure TIM5 as 32-bit microsecond counter
2. **Command queue** - FIFO buffer with ARM/START gating
3. **PING/PONG** - Timestamp capture at RX/TX points
4. **State machine** - IDLE/ARMED/RUNNING/FAULT transitions

### Hardware Drivers

5. **UartTransport** - Complete USART2 initialization
6. **powerSTEP01 driver** - SPI command sequences
7. **LCD driver** - ST7789 initialization

## License

This project is provided as-is for educational and development purposes.

## References

- [STM32F401RE Reference Manual (RM0368)](https://www.st.com/resource/en/reference_manual/rm0368-stm32f401xbc-and-stm32f401xde-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [NUCLEO-F401RE User Manual (UM1724)](https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf)
- [X-NUCLEO-IHM03A1 User Manual](https://www.st.com/resource/en/user_manual/um2032-getting-started-with-the-xnucleoihm03a1-high-power-stepper-motor-driver-expansion-board-based-on-powerstep01-for-stm32-nucleo-stmicroelectronics.pdf)
- [powerSTEP01 Datasheet](https://www.st.com/resource/en/datasheet/powerstep01.pdf)
- [FreeRTOS Documentation](https://www.freertos.org/Documentation/RTOS_book.html)
