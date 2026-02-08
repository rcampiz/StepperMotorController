# Requirements: Transport & Protocol Specification

This document specifies requirements for CLIENT-SERVER communication, including transport options and the command/response protocol.

## Terminology

| Term | Definition |
|------|------------|
| **SERVER** | STM32F401RE + shields (motion controller firmware) |
| **CLIENT** | Raspberry Pi or Windows PC application |
| **VCP** | Virtual COM Port (UART over USB via debugger) |
| **RTT** | SEGGER Real-Time Transfer (memory-based I/O over SWD) |

## Transport Options

The system MUST support two transport mechanisms:

### Option 1: UART Virtual COM Port (VCP)

**Description:** USART2 routed through the on-board debugger USB interface.

**Hardware Configuration (per ST UM1724):**
- PA2 (USART2_TX) -> ST-LINK RX (via SB13)
- PA3 (USART2_RX) <- ST-LINK TX (via SB14)

**Client Detection:**
| Platform | Device Path |
|----------|-------------|
| Linux | `/dev/ttyACM*` or `/dev/ttyUSB*` |
| Windows | `COM3`, `COM4`, etc. |

**Connection Parameters:**
| Parameter | Default | Configurable |
|-----------|---------|--------------|
| Baud Rate | 115200 | Yes |
| Data Bits | 8 | No |
| Parity | None | No |
| Stop Bits | 1 | No |
| Flow Control | None | Yes (RTS/CTS optional) |

### Option 2: SEGGER RTT

**Description:** Memory-based bidirectional channels over SWD debug interface.

**Advantages:**
- No UART pins required
- Higher throughput (~1-2 MB/s)
- Coexists with debugging
- Multiple channels (console + SystemView)

**RTT Channel Allocation:**

| Channel | Direction | Purpose | Notes |
|---------|-----------|---------|-------|
| 0 | Up (to host) | Command responses, telemetry | JSON output |
| 0 | Down (from host) | Command input | ASCII or binary |
| 1 | Up | SystemView events | Reserved for tracing |

**Client Connection:**
- Requires J-Link OB firmware on debugger
- Uses JLinkRTTClient or programmatic RTT API
- Must specify device (`STM32F401RE`) and interface (`SWD`)

## Transport Abstraction

### Server-Side (Firmware)

The server firmware MUST implement a transport abstraction:

```cpp
// Already implemented: Core/Inc/comms/transport_interface.hpp
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

**Implementations:**
- `UartTransport` - VCP via USART2 (implemented)
- `RttTransport` - SEGGER RTT Channel 0 (implemented)

### Client-Side (Python)

The Python client MUST implement a matching abstraction:

```python
# Required interface
class ITransport(ABC):
    @abstractmethod
    def open(self) -> bool:
        """Open the transport connection."""
        pass

    @abstractmethod
    def close(self) -> None:
        """Close the transport connection."""
        pass

    @abstractmethod
    def send(self, data: bytes) -> int:
        """Send data, return bytes written."""
        pass

    @abstractmethod
    def recv(self, timeout_ms: int = 1000) -> bytes:
        """Receive data with timeout."""
        pass

    @abstractmethod
    def available(self) -> int:
        """Return number of bytes available to read."""
        pass
```

**Required Implementations:**
- `VcpTransport` - Serial port via pyserial
- `RttTransport` - RTT via pylink or JLinkRTTClient subprocess

## Protocol Specification

### Command Format (Client -> Server)

Commands are ASCII line-based:

```
<COMMAND> [ARG1] [ARG2] ... <CR><LF>
```

**Examples:**
```
MOVE 1000 1
GOTO 5000
RUN 500 0
GET_STATUS
PING 42
```

### Response Format (Server -> Client)

**REQ-PROTO-001: JSON Mode (Required)**

All structured responses MUST be JSON when JSON mode is enabled:

```json
{"status": "ok", "data": {...}}
{"status": "error", "code": "INVALID_ARG", "message": "speed out of range"}
```

**REQ-PROTO-002: ASCII Mode (Legacy/Debug)**

ASCII mode is retained for backward compatibility and debugging:

```
OK [message]
ERR: [message]
PONG <seq> <rx_tick> <tx_tick> <state>
STATUS <state> <tick> <queue_depth> <position> <speed> <flags>
```

**REQ-PROTO-003: Mode Selection**

The client MUST be able to select response format:

| Command | Effect |
|---------|--------|
| `SET_FORMAT JSON` | Enable JSON responses |
| `SET_FORMAT ASCII` | Enable ASCII responses (default) |
| `GET_FORMAT` | Query current format |

### JSON Response Schema

#### Success Response

```json
{
  "status": "ok",
  "command": "<echoed_command>",
  "data": { ... }
}
```

#### Error Response

```json
{
  "status": "error",
  "command": "<echoed_command>",
  "code": "<ERROR_CODE>",
  "message": "<human_readable_message>"
}
```

#### Error Codes

| Code | Description |
|------|-------------|
| `INVALID_COMMAND` | Unknown command |
| `INVALID_ARG` | Argument out of range or malformed |
| `MISSING_ARG` | Required argument not provided |
| `QUEUE_FULL` | Command queue is full |
| `INVALID_STATE` | Command not valid in current state |
| `FAULT` | Device is in fault state |
| `TIMEOUT` | Operation timed out |

### Command Reference (JSON Responses)

#### GET_STATUS

```json
{
  "status": "ok",
  "command": "GET_STATUS",
  "data": {
    "state": "IDLE",
    "tick": 1234567,
    "queue_depth": 0,
    "motor": {
      "position": 0,
      "speed": 0,
      "busy": false,
      "hi_z": false,
      "stalled": false
    },
    "encoder": {
      "count": 0,
      "velocity": 0,
      "index_seen": false
    },
    "faults": []
  }
}
```

#### GET_DEVICE_INFO

```json
{
  "status": "ok",
  "command": "GET_DEVICE_INFO",
  "data": {
    "device_id": "motor_node_01",
    "device_model": "NUCLEO-F401RE",
    "firmware_version": "1.0.0",
    "hardware_revision": "B",
    "build_timestamp": "2026-02-02T12:00:00Z"
  }
}
```

#### GET_MOTOR_INFO

```json
{
  "status": "ok",
  "command": "GET_MOTOR_INFO",
  "data": {
    "manufacturer": "StepperOnline",
    "part_number": "23HS22-2804-ME1K",
    "steps_per_rev": 200,
    "rated_current_ma": 2800,
    "rated_voltage_v": 3.2,
    "max_rpm": 1000
  }
}
```

#### GET_LIMITS

```json
{
  "status": "ok",
  "command": "GET_LIMITS",
  "data": {
    "position_min": -2097152,
    "position_max": 2097151,
    "speed_max": 15625,
    "accel_min": 1,
    "accel_max": 4095,
    "maxspd_min": 1,
    "maxspd_max": 1023
  }
}
```

#### PING Response

```json
{
  "status": "ok",
  "command": "PING",
  "data": {
    "seq": 42,
    "rx_tick": 1234600,
    "tx_tick": 1234605,
    "state": "IDLE"
  }
}
```

#### Motion Command Response

```json
{
  "status": "ok",
  "command": "MOVE 1000 1",
  "data": {}
}
```

#### Error Example

```json
{
  "status": "error",
  "command": "MOVE 9999999 1",
  "code": "INVALID_ARG",
  "message": "steps out of range (0-2097151)"
}
```

### Telemetry Stream (Optional)

When telemetry streaming is enabled, the server sends periodic JSON updates:

```json
{
  "type": "telemetry",
  "tick": 1234700,
  "motor": {
    "position": 500,
    "speed": 1200,
    "busy": true
  },
  "encoder": {
    "count": 502,
    "velocity": 4800
  }
}
```

Enable/disable with:
```
TELEMETRY ON [interval_ms]
TELEMETRY OFF
```

### Event Messages

Asynchronous events use a distinct format:

```json
{
  "type": "event",
  "event": "JOY",
  "data": {
    "direction": "LEFT",
    "action": "pressed"
  }
}
```

```json
{
  "type": "event",
  "event": "FAULT",
  "data": {
    "code": "STALL_DETECTED",
    "message": "Motor stall detected at position 1234"
  }
}
```

## Implementation Requirements

### Server Firmware Changes

1. **REQ-PROTO-010: Add Format State**
   - Add `ResponseFormat` enum: `ASCII`, `JSON`
   - Store current format in CommandParser
   - Default to `ASCII` for backward compatibility

2. **REQ-PROTO-011: JSON Response Helpers**
   - Add `respondJsonOk(const char* command, const char* dataJson)`
   - Add `respondJsonErr(const char* command, const char* code, const char* message)`
   - Use minimal JSON formatting (no external library, snprintf-based)

3. **REQ-PROTO-012: New Commands**
   - `SET_FORMAT JSON|ASCII`
   - `GET_FORMAT`
   - `GET_DEVICE_INFO`
   - `GET_MOTOR_INFO`
   - `GET_LIMITS`
   - `TELEMETRY ON|OFF [interval]`

### Client Implementation

1. **REQ-PROTO-020: Transport Factory**
   - Factory function to create transport based on config
   - Auto-detection of available transports

2. **REQ-PROTO-021: JSON Parsing**
   - Parse all responses as JSON when in JSON mode
   - Handle both `status: ok` and `status: error`

3. **REQ-PROTO-022: Async Event Handling**
   - Background thread/async for receiving events
   - Callback registration for event types

## Migration Strategy

### Phase 1: Add JSON Mode
- Implement `SET_FORMAT` command
- Add JSON response helpers
- Update existing commands to emit JSON when enabled

### Phase 2: Add Introspection Commands
- `GET_DEVICE_INFO`
- `GET_MOTOR_INFO`
- `GET_LIMITS`

### Phase 3: Add Telemetry Streaming
- `TELEMETRY ON/OFF`
- Background telemetry emission

### Phase 4: Client Implementation
- Python transport abstraction
- VCP and RTT implementations
- JSON parsing layer

## References

- [SEGGER RTT](https://www.segger.com/products/debug-probes/j-link/technology/about-real-time-transfer/) - RTT documentation
- [ST UM1724](https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf) - NUCLEO-F401RE VCP routing
- [pyserial](https://pyserial.readthedocs.io/) - Python serial library
- [pylink](https://pylink.readthedocs.io/) - Python J-Link library

## Implementation Checklist

- [ ] Add `ResponseFormat` enum to CommandParser
- [ ] Implement `SET_FORMAT` and `GET_FORMAT` commands
- [ ] Add JSON response helper functions
- [ ] Update all command handlers to support JSON mode
- [ ] Implement `GET_DEVICE_INFO` command
- [ ] Implement `GET_MOTOR_INFO` command
- [ ] Implement `GET_LIMITS` command
- [ ] Implement `TELEMETRY` streaming command
- [ ] Create Python `ITransport` interface
- [ ] Implement Python `VcpTransport`
- [ ] Implement Python `RttTransport`
- [ ] Create protocol wrapper with JSON parsing
