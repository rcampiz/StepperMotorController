# Communication Architecture

This document describes the communication layer design for upstream control and telemetry between a host (Raspberry Pi or Windows PC) and the STM32F401RE Nucleo board.

For the complete synchronization protocol specification (ARM/START, PING/PONG, command queuing), see [HOST_INTERFACE_AND_SYNC.md](HOST_INTERFACE_AND_SYNC.md).

## Overview

The system supports two transport mechanisms:

1. **VCP UART** - Virtual COM Port over USB via ST-LINK/J-Link debugger
2. **SEGGER RTT** - Real-Time Transfer over SWD debug interface

Both transports implement a common `ITransport` interface, allowing the command parser and telemetry system to work identically regardless of the underlying transport.

## Transport Options

### Option A: VCP UART (Default)

Uses USART2 which is connected to the on-board debugger via solder bridges.

**Hardware Configuration (per ST UM1724):**
- PA2 (USART2_TX) -> ST-LINK RX (via SB13)
- PA3 (USART2_RX) <- ST-LINK TX (via SB14)

**Raspberry Pi Setup:**
```bash
# Device typically appears as /dev/ttyACM0
ls -la /dev/ttyACM*

# Add user to dialout group for permissions
sudo usermod -a -G dialout $USER

# Test connection
minicom -D /dev/ttyACM0 -b 115200
```

**Pros:**
- Works with ST-LINK or J-Link OB
- Simple serial protocol
- No special software required on host

**Cons:**
- Uses UART pins (PA2/PA3)
- Limited to ~1 Mbaud
- Single channel

### Option B: SEGGER RTT

Uses memory-based communication over the SWD debug interface.

**RTT Channel Allocation:**
| Channel | Direction | Purpose |
|---------|-----------|---------|
| 0 | Up (to host) | Console output / responses |
| 0 | Down (from host) | Command input |
| 1 | Up | SystemView events (reserved) |

**Raspberry Pi Setup:**
```bash
# Install J-Link software
# Download from: https://www.segger.com/downloads/jlink/

# Connect via RTT
JLinkRTTClient -device STM32F401RE -if SWD

# Or use JLinkExe
JLinkExe -device STM32F401RE -if SWD -speed 4000 -autoconnect 1
# Then: rtt start
```

**Pros:**
- No UART pins needed
- Faster (~1-2 MB/s)
- Multiple channels (console + SystemView)
- Works alongside debugging

**Cons:**
- Requires J-Link (need ST-LINK conversion)
- Needs J-Link software on host
- More complex host setup

## Transport Interface

```cpp
// Core/Inc/comms/transport_interface.hpp

class ITransport {
public:
    virtual bool init() = 0;
    virtual bool available() = 0;
    virtual size_t read(uint8_t* buffer, size_t maxLen) = 0;
    virtual bool readByte(uint8_t& byte, uint32_t timeoutMs) = 0;
    virtual size_t write(const uint8_t* data, size_t len) = 0;
    virtual size_t print(const char* str) = 0;
    virtual size_t println(const char* str) = 0;
    virtual void flush() = 0;
};
```

## Command Protocol

### Format

ASCII line-based protocol for human readability and easy debugging:

```
Command:  <CMD> [ARG1] [ARG2] ... <CR><LF>
Response: OK: <message><CR><LF>
       or ERR: <message><CR><LF>
       or DATA:<CR><LF><lines...><CR><LF><blank line>
```

### Command Reference

#### Motion Commands

| Command | Arguments | Description |
|---------|-----------|-------------|
| `MOVE` | `<steps> <dir>` | Relative move (steps, direction 0/1) |
| `GOTO` | `<position>` | Absolute move to position |
| `RUN` | `<speed> <dir>` | Continuous rotation at speed |
| `STOP` | `[hard]` | Soft stop (or hard if specified) |
| `ESTOP` | - | Emergency stop, disable outputs |
| `HOME` | - | Return to home position |
| `ZERO` | - | Set current position as zero |

#### Synchronization Commands

| Command | Arguments | Description |
|---------|-----------|-------------|
| `QUEUE` | `<cmd> <args>` | Add command to execution queue |
| `ARM` | - | Prepare queued commands, await START |
| `START` | - | Begin executing armed commands |
| `START_AT` | `<tick>` | Begin execution at specified tick |
| `CLEAR_QUEUE` | - | Discard all queued commands |

#### Timing/Diagnostics Commands

| Command | Arguments | Description |
|---------|-----------|-------------|
| `PING` | `<seq> [host_time]` | Latency measurement (returns PONG) |
| `GET_TICK` | - | Query current MCU tick counter |
| `GET_STATUS` | - | Query state, position, velocity, flags |
| `CLEAR_FAULT` | - | Clear fault condition |

#### Configuration Commands

| Command | Arguments | Description |
|---------|-----------|-------------|
| `ENABLE` | - | Enable motor outputs |
| `DISABLE` | - | Disable motor outputs (Hi-Z) |
| `ACCEL` | `<value>` | Set acceleration |
| `DECEL` | `<value>` | Set deceleration |
| `MAXSPD` | `<value>` | Set maximum speed |

#### Query Commands

| Command | Description |
|---------|-------------|
| `ENCODER` | Encoder count, velocity, index state |
| `VER` | Firmware version and build date |
| `HELP` | List available commands |

### Example Session

```
> GET_TICK
OK 1234567

> PING 1
PONG 1 1234600 1234605 IDLE

> GET_STATUS
STATUS IDLE 1234700 0 0 0 0

> QUEUE MOVE 1000 1
OK

> QUEUE MOVE 500 1
OK

> ARM
OK ARMED

> START
OK RUNNING

> GET_STATUS
STATUS RUNNING 1235000 2 500 1200 0

> ENCODER
OK count=2000 vel=4800 idx=0
```

### Synchronized Multi-Controller Example

```python
# Python host coordinating 4 controllers
import serial

controllers = [
    serial.Serial('COM3', 115200),
    serial.Serial('COM4', 115200),
    serial.Serial('COM5', 115200),
    serial.Serial('COM6', 115200),
]

# Queue motion commands to all controllers
for ctrl in controllers:
    ctrl.write(b'QUEUE MOVE 1000 1\n')
    ctrl.readline()  # ACK

# Arm all controllers
for ctrl in controllers:
    ctrl.write(b'ARM\n')
    ctrl.readline()  # ACK ARMED

# Trigger simultaneous start
for ctrl in controllers:
    ctrl.write(b'START\n')

# All four wheels begin motion together
```

## Telemetry System

### Data Structures

```cpp
// Core/Inc/comms/telemetry.hpp

struct MotorTelemetry {
    int32_t position;       // Absolute position (steps)
    int32_t targetPosition; // Target for GoTo
    uint32_t speed;         // Current speed (steps/s)
    uint16_t statusReg;     // Raw STATUS register
    bool busy;              // Motor is moving
    bool hiZ;               // Outputs in Hi-Z
    bool stalled;           // Stall detected
};

struct EncoderTelemetry {
    int32_t count;          // Raw encoder count
    int32_t velocity;       // Counts per second
    bool indexSeen;         // Index pulse detected
    uint32_t indexTick;     // Tick when index seen
};

struct SystemTelemetry {
    uint32_t uptimeTicks;   // FreeRTOS tick count
    uint32_t freeHeap;      // Free heap bytes
    uint8_t cpuLoad;        // CPU load percentage
};
```

### Thread Safety

The `TelemetryManager` class provides thread-safe access:

```cpp
// Update from task (e.g., MotorTask)
Comms::MotorTelemetry telem = {...};
Comms::g_telemetry.updateMotor(telem);

// Read from another task (e.g., CommsTask)
Comms::TelemetrySnapshot snap = Comms::g_telemetry.getSnapshot();
```

Protected by FreeRTOS mutex with 10ms timeout.

## Architecture Diagram

```
+-------------------------------------------------------------+
|                      Raspberry Pi                           |
|  +------------------+         +------------------+          |
|  |  Control App     |         |  SystemView      |          |
|  |  (Python/C++)    |         |  (optional)      |          |
|  +--------+---------+         +--------+---------+          |
|           | /dev/ttyACM0               | J-Link             |
+-----------+----------------------------+--------------------+
            |                            |
            | USB                        | SWD
            v                            v
+------------------------------------------------------------+
|              NUCLEO-F401RE (ST-LINK/J-Link OB)             |
|  +------------------------------------------------------+  |
|  |                    STM32F401RE                        |  |
|  |                                                       |  |
|  |  +--------------+    +---------------------------+    |  |
|  |  | CommsTask    |<---| ITransport                |    |  |
|  |  | (Pri: 3)     |    |  +-- UartTransport (VCP)  |    |  |
|  |  |              |    |  +-- RttTransport (RTT)   |    |  |
|  |  +------+-------+    +---------------------------+    |  |
|  |         |                                             |  |
|  |         | motorCmdQueue                               |  |
|  |         v                                             |  |
|  |  +--------------+    +--------------+                 |  |
|  |  | MotorTask    |    | EncoderTask  |                 |  |
|  |  | (Pri: 4)     |    | (Pri: 2)     |                 |  |
|  |  +------+-------+    +------+-------+                 |  |
|  |         |                   |                         |  |
|  |         |     TelemetryManager (mutex)                |  |
|  |         +--------->+-------+-------+<-----------------+  |
|  |                    |    Shared     |                  |  |
|  |                    |   Telemetry   |                  |  |
|  |                    +-------+-------+                  |  |
|  |                            |                          |  |
|  |                    +-------+-------+                  |  |
|  |                    | DisplayTask   |                  |  |
|  |                    | (Pri: 1)      |                  |  |
|  |                    +---------------+                  |  |
|  +-------------------------------------------------------+  |
+-------------------------------------------------------------+
```

## Windows Compatibility

The VCP UART transport is designed for cross-platform use:

**Windows:**
- Appears as COM port (e.g., `COM3`)
- Use Device Manager to find port number
- Test with PuTTY, Tera Term, or Python `serial` module

**Raspberry Pi / Linux:**
- Appears as `/dev/ttyACM0` (or similar)
- Add user to `dialout` group for permissions

**Python Example (works on both):**
```python
import serial
import serial.tools.list_ports

# Find ST-LINK VCP
for port in serial.tools.list_ports.comports():
    if 'STM' in port.description or 'ST-Link' in port.description:
        print(f"Found: {port.device}")

ser = serial.Serial('COM3', 115200, timeout=1)  # Windows
# ser = serial.Serial('/dev/ttyACM0', 115200, timeout=1)  # Linux
```

## Implementation Status

| Component | Status |
|-----------|--------|
| `ITransport` interface | Complete |
| `UartTransport` | Stub (TODO: USART2 init) |
| `RttTransport` | Stub (needs SEGGER sources) |
| `CommandParser` | Protocol skeleton with sync commands |
| `TelemetryManager` | Complete |
| `CommsTask` | Stub with init framework |
| Tick timer service | Not implemented |
| Command queue | Not implemented |
| ARM/START state machine | Not implemented |
| PING/PONG timestamps | Not implemented |

## Next Steps

1. **Implement tick timer service** - TIM5 as 32-bit microsecond counter
2. **Implement command queue** - FIFO with ARM/START gating
3. **Add PING/PONG** - Timestamp capture at RX/TX
4. **Implement UartTransport** - USART2 configuration
5. Wire `CommandParser` dispatch to `MotorTask_SendCommand()`
6. Implement state machine (IDLE/ARMED/RUNNING/FAULT)

## References

- [HOST_INTERFACE_AND_SYNC.md](HOST_INTERFACE_AND_SYNC.md) - Complete sync protocol specification
- [SEGGER J-Link Software](https://www.segger.com/downloads/jlink/) - RTT sources and utilities
- [ST UM1724](https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf) - NUCLEO-F401RE User Manual (VCP pinout)
