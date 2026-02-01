# Host Interface and Synchronization Requirements

## Overview

This document specifies the host interface and synchronization requirements for the STM32 stepper motor controller firmware. The controller is designed to be one of **four** independent units commanded by a Raspberry Pi host to drive a mecanum-wheel platform with synchronized motion.

## System Architecture

```
                              Raspberry Pi (Host)
                                     |
            +------------+-----------+-----------+------------+
            |            |           |           |            |
            v            v           v           v            |
     +----------+  +----------+  +----------+  +----------+   |
     | Wheel FL |  | Wheel FR |  | Wheel RL |  | Wheel RR |   |
     | STM32    |  | STM32    |  | STM32    |  | STM32    |   |
     +----------+  +----------+  +----------+  +----------+   |
                                                              |
     Host connection: USB -> ST-LINK/J-LINK -> MCU            |
     Transport: VCP UART (COM port) or SEGGER RTT             |
```

Each controller must accept motion commands and execute them **synchronously** with the others to achieve coordinated mecanum-wheel motion.

## Core Requirements

### 1. Command Interface (Host -> MCU)

The MCU exposes a deterministic command protocol over the host connection.

#### Motion Commands

| Command | Parameters | Description |
|---------|------------|-------------|
| `MOVE` | steps, direction | Relative move by step count |
| `GOTO` | position | Absolute position move |
| `RUN` | speed, direction | Continuous rotation at speed |
| `STOP` | [hard] | Decelerate to stop (or immediate) |
| `ESTOP` | - | Emergency stop, disable outputs |

#### Configuration Commands

| Command | Parameters | Description |
|---------|------------|-------------|
| `SET_SPEED` | steps/sec | Set maximum speed |
| `SET_ACCEL` | steps/sec^2 | Set acceleration rate |
| `SET_DECEL` | steps/sec^2 | Set deceleration rate |
| `ENABLE` | - | Enable motor outputs |
| `DISABLE` | - | Disable motor outputs (Hi-Z) |

#### Synchronization Commands

| Command | Parameters | Description |
|---------|------------|-------------|
| `QUEUE` | cmd, params... | Add command to execution queue |
| `ARM` | - | Prepare queued commands, await START |
| `START` | - | Begin executing armed commands immediately |
| `START_AT` | tick | Begin execution at specified tick |
| `CLEAR_QUEUE` | - | Discard all queued commands |

#### Timing/Diagnostics Commands

| Command | Parameters | Description |
|---------|------------|-------------|
| `PING` | seq, [host_time] | Request timestamped echo |
| `GET_TICK` | - | Query current MCU tick counter |
| `GET_STATUS` | - | Query controller state and telemetry |
| `CLEAR_FAULT` | - | Clear fault condition, prepare for re-enable |

#### Device Identification Commands

| Command | Parameters | Description |
|---------|------------|-------------|
| `GET_DEVICE_ID` | - | Query device ID and wheel role |
| `SET_DEVICE_ID` | id | Set unique device ID (0-65535) |
| `SET_ROLE` | role | Set wheel role: FL, FR, RL, RR, NONE |

#### Control Mode Commands

| Command | Parameters | Description |
|---------|------------|-------------|
| `GET_MODE` | - | Query current control mode and encoder status |
| `SET_MODE` | mode | Set mode: OPEN_LOOP or CLOSED_LOOP |
| `GET_ENCODER_STATUS` | - | Query encoder availability and data |
| `ENCODER` | - | Query encoder count, velocity, index status |

### 2. Synchronous Start Mechanism

To coordinate four controllers for simultaneous motion, the firmware supports two patterns:

#### Pattern A: ARM + START (Recommended)

```
Host                    MCU (one of four)
  |                           |
  |-- QUEUE MOVE 1000 FWD --->|  (queue command)
  |-- QUEUE MOVE 500 FWD ---->|  (queue another)
  |-- ARM ------------------>|  (prepare, don't move)
  |<-- ACK ARM (state=ARMED)-|
  |                           |
  | (repeat for all 4 MCUs)   |
  |                           |
  |-- START ---------------->|  (all 4 as fast as possible)
  |<-- ACK START ------------|
  |                           |  (motion begins)
```

The host sends `START` to all four controllers in rapid succession. Controllers begin motion immediately upon receiving `START`.

#### Pattern B: START_AT (Timestamp-Based)

```
Host                    MCU
  |                           |
  |-- GET_TICK ------------->|
  |<-- TICK 100000 ----------|
  |                           |
  |-- QUEUE MOVE 1000 FWD --->|
  |-- START_AT 100500 ------>|  (start at tick 100500)
  |<-- ACK ------------------|
  |                           |
  | (MCU waits until tick 100500, then begins motion)
```

The host queries tick counters from all controllers, calculates a future start time, and issues `START_AT` commands. This compensates for communication latency.

### 3. Latency Measurement (PING/PONG)

Repeatable latency measurement enables the host to:
- Measure round-trip time (RTT)
- Estimate one-way delay
- Compensate for jitter when coordinating multiple controllers

#### Protocol

**Request:**
```
PING <seq> [<host_time>]
```

**Response:**
```
PONG <seq> <mcu_rx_tick> <mcu_tx_tick> <status>
```

Where:
- `seq` - Sequence number for matching requests/responses
- `host_time` - Optional host timestamp (echoed back for host convenience)
- `mcu_rx_tick` - MCU tick captured immediately upon receiving PING
- `mcu_tx_tick` - MCU tick captured immediately before transmitting PONG
- `status` - Current controller state

#### Latency Calculation

```
RTT = host_rx_time - host_tx_time
MCU_processing = mcu_tx_tick - mcu_rx_tick
Network_delay = RTT - (MCU_processing * tick_period)
One_way_estimate = Network_delay / 2
```

### 4. Monotonic Tick Timer

The firmware maintains a monotonic tick counter for:
- Timestamping commands and responses
- Scheduling START_AT execution
- Measuring internal latencies

#### Requirements

- **Resolution:** 1 microsecond or better (using hardware timer)
- **Width:** 32-bit minimum (wraps after ~4295 seconds at 1us)
- **Source:** Hardware timer (TIM2 or SysTick), not software counter
- **Access:** Readable via `GET_TICK` command and included in telemetry

#### Implementation

```cpp
// Using TIM5 as 32-bit microsecond counter (84 MHz / 84 = 1 MHz)
uint32_t getTick_us() {
    return TIM5->CNT;
}
```

### 5. Device Identification

Each controller stores a unique identity in non-volatile flash memory, enabling the host to identify and configure individual wheel controllers.

#### Persistent Configuration

Stored in SPI NOR flash (X-NUCLEO-GFX01M2 board):

| Field | Type | Description |
|-------|------|-------------|
| `device_id` | uint16 | Unique device identifier (0-65535) |
| `role` | enum | Wheel position: UNASSIGNED, FRONT_LEFT, FRONT_RIGHT, REAR_LEFT, REAR_RIGHT |
| `default_mode` | enum | Control mode at power-on |

#### Response Formats

**GET_DEVICE_ID:**
```
OK <device_id> <role>
```
Example: `OK 1 FRONT_LEFT`

**SET_DEVICE_ID:**
```
OK ID saved
```

**SET_ROLE:**
```
OK Role=<role>
```

### 6. Control Modes

The controller supports two control modes:

| Mode | Description |
|------|-------------|
| `OPEN_LOOP` | Step counting only, no encoder feedback |
| `CLOSED_LOOP` | Position feedback from encoder (requires encoder hardware) |

#### Encoder Status

| Status | Description |
|--------|-------------|
| `NOT_PRESENT` | Encoder hardware not detected |
| `INITIALIZING` | Encoder hardware being configured |
| `READY` | Encoder operational |
| `FAULT` | Encoder hardware error |

#### Mode Switching Rules

- `OPEN_LOOP` is always available
- `CLOSED_LOOP` requires encoder status = `READY`
- If encoder fails while in `CLOSED_LOOP`, system reverts to `OPEN_LOOP`
- System boots in `OPEN_LOOP` if encoder unavailable

#### Response Formats

**GET_MODE:**
```
OK <mode> encoder=<encoder_status>
```
Example: `OK OPEN_LOOP encoder=NOT_PRESENT`

**SET_MODE (success):**
```
OK <mode>
```

**SET_MODE (failure):**
```
ERROR Cannot enter CLOSED_LOOP: encoder <status>
```

**GET_ENCODER_STATUS:**
```
OK status=<status> [count=N vel=N idx=0|1]
```
When encoder is READY, includes current encoder data.

**ENCODER:**
```
OK count=<N> vel=<N> idx=<0|1> idx_tick=<N>
```
Or if encoder unavailable:
```
ERROR Encoder not available
```

### 7. Deterministic Step Generation

Motion execution is driven by hardware timers, not host timing.

#### Requirements

- Step pulses generated by timer interrupt or DMA
- Consistent pulse width regardless of CPU load
- Acceleration/deceleration profiles computed locally
- Host commands set parameters; MCU handles real-time execution

#### Implementation Notes

The powerSTEP01 driver handles step generation internally. The MCU:
1. Receives motion command from host
2. Configures powerSTEP01 registers (speed, accel, etc.)
3. Issues powerSTEP01 motion command (MOVE, GOTO, RUN)
4. powerSTEP01 generates step pulses autonomously
5. MCU monitors BUSY/FLAG for completion/faults

### 6. Status and Telemetry (MCU -> Host)

#### Controller States

| State | Description |
|-------|-------------|
| `IDLE` | No motion, ready for commands |
| `ARMED` | Commands queued, awaiting START |
| `RUNNING` | Motion in progress |
| `STOPPING` | Decelerating to stop |
| `FAULT` | Error condition, requires CLEAR_FAULT |
| `ESTOP` | Emergency stop, outputs disabled |

#### Status Response Format

```
STATUS <state> <tick> <queue_depth> <mode> <encoder_status> <position> <velocity>
```

Where:
- `state` - Current controller state (IDLE, ARMED, RUNNING, FAULT, ESTOP)
- `tick` - Current tick counter value (microseconds)
- `queue_depth` - Number of commands in queue
- `mode` - Control mode (OPEN_LOOP or CLOSED_LOOP)
- `encoder_status` - Encoder status (NOT_PRESENT, INITIALIZING, READY, FAULT)
- `position` - Current motor position (steps)
- `velocity` - Current velocity (steps/sec)

#### Telemetry Stream (Optional)

When enabled, the controller periodically transmits:
```
TELEM <tick> <position> <velocity> <state> <flags>
```

### 7. Safety and Recovery

#### Emergency Stop (ESTOP)

- Immediately halts step pulses
- Disables motor outputs (Hi-Z)
- Sets state to `ESTOP`
- Clears command queue
- Requires explicit recovery sequence

#### Recovery Sequence

```
Host                    MCU
  |                           |
  | (ESTOP condition)         |
  |                           |
  |-- CLEAR_FAULT ---------->|
  |<-- ACK (state=IDLE) -----|
  |                           |
  |-- ENABLE --------------->|
  |<-- ACK ------------------|
  |                           |
  | (ready for motion commands)
```

#### Error Codes

| Code | Description |
|------|-------------|
| `E_OK` | No error |
| `E_INVALID_CMD` | Unknown command |
| `E_INVALID_PARAM` | Parameter out of range |
| `E_QUEUE_FULL` | Command queue overflow |
| `E_QUEUE_EMPTY` | No commands to execute |
| `E_NOT_ARMED` | START without ARM |
| `E_ALREADY_RUNNING` | Motion command while running |
| `E_DRIVER_FAULT` | powerSTEP01 fault detected |
| `E_STALL` | Stall detected |
| `E_OVERCURRENT` | Overcurrent detected |

### 8. Command Queue

The firmware maintains a command queue for buffered execution.

#### Requirements

- Minimum depth: 8 commands
- FIFO ordering
- Can be populated while IDLE or RUNNING
- Cleared on ESTOP or CLEAR_QUEUE
- Queue state reported in STATUS

#### Queue Commands

| Command | Behavior |
|---------|----------|
| `QUEUE <cmd> <params>` | Add command to queue |
| `ARM` | Lock queue, prepare for START |
| `START` | Begin executing queue |
| `CLEAR_QUEUE` | Discard all queued commands |

## Message Framing

Commands and responses use ASCII text with newline termination:

```
<COMMAND> [<param1>] [<param2>] ...\n
```

Responses:
```
OK [<data>]\n
ERROR <code> [<message>]\n
```

## Development Environment

### Windows Testing

The firmware is testable from Windows via:
- **VCP UART:** Appears as COM port, accessible from Python/terminal
- **SEGGER RTT:** Via J-Link RTT Viewer or JLinkRTTClient

A Python test harness can:
- Send commands via serial port
- Queue motion commands
- Trigger synchronized starts
- Measure latency using PING/PONG
- Query status and tick counters

### Example Test Sequence (Python)

```python
import serial
import time

# Connect to controller
ser = serial.Serial('COM3', 115200, timeout=1)

# Query current tick
ser.write(b'GET_TICK\n')
tick = int(ser.readline().decode().split()[1])
print(f"Current tick: {tick}")

# Measure latency
ser.write(b'PING 1\n')
t_tx = time.perf_counter_ns()
response = ser.readline().decode()
t_rx = time.perf_counter_ns()
# Parse PONG response for mcu_rx_tick, mcu_tx_tick
print(f"RTT: {(t_rx - t_tx) / 1e6:.3f} ms")

# Queue motion and start
ser.write(b'QUEUE MOVE 1000 1\n')
ser.write(b'ARM\n')
ser.write(b'START\n')

# Monitor status
ser.write(b'GET_STATUS\n')
print(ser.readline().decode())
```

## Implementation Status

| Feature | Status |
|---------|--------|
| Command parser framework | **Complete** |
| Motion commands (MOVE/STOP) | Stubbed (powerSTEP01 integration pending) |
| Command queue | **Complete** - FIFO with 8-command depth |
| ARM/START sync | **Complete** - ARM/START/START_AT |
| Tick timer service | **Complete** - TIM5 @ 1MHz (1µs resolution) |
| PING/PONG timestamps | **Complete** - RX/TX tick capture |
| Status/telemetry | **Complete** - State, tick, queue depth, mode |
| ESTOP/recovery | **Complete** - Emergency stop and CLEAR_FAULT |
| Device identification | **Complete** - Persistent storage in NOR flash |
| Control modes | **Complete** - OPEN_LOOP/CLOSED_LOOP |
| Encoder abstraction | **Complete** - Optional encoder, graceful degradation |

## Next Steps

1. **powerSTEP01 integration** - Connect motion commands to motor driver
2. **Closed-loop control** - Implement PID control using encoder feedback
3. **Multi-controller testing** - Test synchronized start across 4 controllers
4. **Tuning and calibration** - Accel/decel profiles for mecanum kinematics

## References

- [docs/COMMUNICATION_ARCHITECTURE.md](COMMUNICATION_ARCHITECTURE.md) - Transport layer design
- [docs/TASK_ARCHITECTURE.md](TASK_ARCHITECTURE.md) - FreeRTOS task structure
- [powerSTEP01 Datasheet](https://www.st.com/resource/en/datasheet/powerstep01.pdf) - Motor driver commands
