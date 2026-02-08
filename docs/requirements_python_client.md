# Requirements: Python Client & GUI Application

This document specifies requirements for the Python CLIENT application that controls the STM32 motion controller SERVER.

## Overview

The Python client provides:
- Cross-platform GUI for motor control and testing
- Transport abstraction for VCP and RTT communication
- Device introspection and telemetry display
- Display testing and image transfer tools

## Platform Support

### REQ-CLI-001: Operating Systems

The client MUST run on:
| Platform | Version | Notes |
|----------|---------|-------|
| Raspberry Pi OS | Bookworm (64-bit) | Primary deployment target |
| Windows | 10/11 | Development and testing |
| Ubuntu | 22.04+ | Optional Linux desktop |

### REQ-CLI-002: Python Version

- Minimum: Python 3.10
- Recommended: Python 3.11+
- Must work with system Python on Raspberry Pi OS

### REQ-CLI-003: Installation

- MUST use virtual environment (venv)
- MUST provide `requirements.txt` for pip install
- SHOULD support Poetry as alternative (optional)
- MUST NOT require system-wide package installation

## GUI Framework

### REQ-CLI-010: Framework Selection

Recommended: **PySide6 (Qt for Python)**

| Option | Pros | Cons | Recommendation |
|--------|------|------|----------------|
| PySide6 | Full-featured, professional look, good docs | Large dependency (~150MB) | **Preferred** |
| Tkinter | Built-in, lightweight | Basic appearance, limited widgets | Fallback |
| DearPyGui | Fast, modern, GPU-accelerated | Less mature, fewer widgets | Alternative |

### REQ-CLI-011: GUI Capabilities

The GUI framework MUST support:
- Standard controls: buttons, toggles, sliders, spinboxes, dropdowns
- Data display: tables, labels, graphs/plots
- Image display: preview pane for display testing
- Responsive layout: resizable windows
- Threading: non-blocking UI during I/O operations

## Application Architecture

### REQ-CLI-020: Layered Architecture

```
+--------------------------------------------------+
|                    GUI Layer                      |
|  (PySide6 widgets, event handlers, display)      |
+--------------------------------------------------+
                        |
+--------------------------------------------------+
|               Controller Layer                    |
|  (Command orchestration, state management)       |
+--------------------------------------------------+
                        |
+--------------------------------------------------+
|                Protocol Layer                     |
|  (JSON parsing, command formatting)              |
+--------------------------------------------------+
                        |
+--------------------------------------------------+
|               Transport Layer                     |
|  (VcpTransport, RttTransport)                    |
+--------------------------------------------------+
```

### REQ-CLI-021: Component Separation

- GUI code MUST NOT contain protocol logic
- Transport MUST be injectable/swappable
- State management MUST be centralized
- All I/O MUST be non-blocking (threaded or async)

## Motor Control Panel

### REQ-CLI-030: Mode Selection

The GUI MUST provide mode selection:

| Mode | Description | Encoder Telemetry |
|------|-------------|-------------------|
| Open-Loop | Step commands only | Hidden/grayed |
| Closed-Loop | Encoder feedback enabled | Displayed |

Mode change MUST send `SET_MODE OPEN_LOOP` or `SET_MODE CLOSED_LOOP` to server.

### REQ-CLI-031: Motion Commands

Required controls:

| Control | Widget | Command |
|---------|--------|---------|
| Spin CW | Button + Rate input | `RUN <speed> 1` |
| Spin CCW | Button + Rate input | `RUN <speed> 0` |
| Stop | Button | `STOP` or `STOP hard` |
| E-Stop | Large red button | `ESTOP` |
| Move Absolute | Position input + Go | `GOTO <position>` |
| Move Relative | Steps input + Dir + Go | `MOVE <steps> <dir>` |
| Home | Button | `HOME` |
| Zero | Button | `ZERO` |

### REQ-CLI-032: Rate Control

Speed input MUST support BOTH:
- Slider/dial for quick adjustment
- Numeric spinbox for precise entry
- Both controls MUST stay synchronized

Range: 0 to server-reported `speed_max` (from `GET_LIMITS`)

### REQ-CLI-033: Configuration Commands

| Control | Widget | Command |
|---------|--------|---------|
| Acceleration | Spinbox | `ACCEL <value>` |
| Deceleration | Spinbox | `DECEL <value>` |
| Max Speed | Spinbox | `MAXSPD <value>` |
| Enable | Button | `ENABLE` |
| Disable | Button | `DISABLE` |

Limits MUST be enforced client-side based on `GET_LIMITS` response.

### REQ-CLI-034: Safety Display

The GUI MUST display:
- Current enforced limits (from `GET_LIMITS`)
- Current fault state (from `GET_STATUS`)
- Last fault reason (if any)
- Visual indicator: green=OK, yellow=warning, red=fault

## Telemetry Panel

### REQ-CLI-040: Motor Telemetry

Always displayed:
| Field | Source | Update Rate |
|-------|--------|-------------|
| Position (steps) | `motor.position` | 10 Hz |
| Speed (steps/s) | `motor.speed` | 10 Hz |
| Busy | `motor.busy` | 10 Hz |
| Hi-Z | `motor.hi_z` | 10 Hz |

### REQ-CLI-041: Encoder Telemetry

Displayed in Closed-Loop mode only:
| Field | Source | Update Rate |
|-------|--------|-------------|
| Count | `encoder.count` | 10 Hz |
| Velocity | `encoder.velocity` | 10 Hz |
| Index Seen | `encoder.index_seen` | 10 Hz |

### REQ-CLI-042: Driver Telemetry (Future)

To be implemented when server exposes:
- powerSTEP01 STATUS register bits
- Supply voltage (if ADC available)
- Driver temperature (if available)
- Specific fault flags (overcurrent, thermal, etc.)

### REQ-CLI-043: Telemetry Display Options

- Numeric labels (default)
- Optional: strip chart / graph view for position/velocity over time
- Optional: position dial/gauge visualization

## Device Modes

### REQ-CLI-050: Mode Commands

The GUI MUST be able to switch server modes:

| Mode | Command | Description |
|------|---------|-------------|
| Normal | `SET_DEVICE_MODE NORMAL` | Standard operation |
| Test | `SET_DEVICE_MODE TEST` | Test sequences enabled |
| Display Terminal | `UI_MODE REMOTE` + text commands | Remote text display |
| Display Image | `UI_MODE REMOTE` + image commands | Remote image display |

### REQ-CLI-051: Mode Indicators

- Current mode MUST be clearly displayed in GUI
- Mode-specific panels MUST show/hide based on active mode
- Mode change MUST require confirmation if motion is active

## Display Testing Panel

### REQ-CLI-060: Text Test

- Text input area (multi-line)
- "Send" button sends text via `DISP_TEXT` commands
- Clear button sends `DISP_CLEAR`
- Font size / color selection (if server supports)

### REQ-CLI-061: Image Test

Controls:
- File picker to select image
- Preview pane showing selected image
- Resolution indicator (must match 240x240)
- "Send" button transfers image to server
- Progress indicator during transfer

Built-in test patterns:
- Checkerboard (8x8, 16x16)
- Color bars (vertical RGB)
- Gradients (horizontal, vertical)
- Text overlay ("TEST 240x240")

### REQ-CLI-062: Image Responsibility

**CLIENT responsibilities:**
- Resize/crop images to exactly 240x240 pixels
- Convert to RGB565 format (or base64 as required)
- Display local preview before sending

**SERVER responsibilities:**
- Validate incoming image dimensions
- Reject oversized/invalid images with JSON error
- Never scale or reformat images

## Connection Panel

### REQ-CLI-070: Transport Selection

- Radio buttons: VCP / RTT
- Transport-specific settings shown based on selection

### REQ-CLI-071: VCP Settings

| Setting | Widget | Default |
|---------|--------|---------|
| Port | Dropdown (auto-populated) | First detected |
| Baud Rate | Dropdown | 115200 |
| Refresh | Button | Re-scan ports |

Port auto-detection:
- Linux: Scan `/dev/ttyACM*`, `/dev/ttyUSB*`
- Windows: Query COM ports, filter by VID/PID if possible

### REQ-CLI-072: RTT Settings

| Setting | Widget | Default |
|---------|--------|---------|
| J-Link Serial | Dropdown/Text | Auto-detect |
| Target Device | Text | STM32F401RE |
| Speed (kHz) | Spinbox | 4000 |

### REQ-CLI-073: Connection Status

- Connected/Disconnected indicator
- Connect/Disconnect buttons
- Auto-reconnect option (checkbox)
- Connection log/history (collapsible)

## Device Information Panel

### REQ-CLI-080: Device Info Display

On connect, query and display:

| Field | Command | Display |
|-------|---------|---------|
| Device ID | `GET_DEVICE_INFO` | Label |
| Firmware Version | `GET_DEVICE_INFO` | Label |
| Hardware Revision | `GET_DEVICE_INFO` | Label |
| Build Timestamp | `GET_DEVICE_INFO` | Label |

### REQ-CLI-081: Motor Info Display

| Field | Command | Display |
|-------|---------|---------|
| Manufacturer | `GET_MOTOR_INFO` | Label |
| Part Number | `GET_MOTOR_INFO` | Label |
| Steps/Rev | `GET_MOTOR_INFO` | Label |
| Rated Current | `GET_MOTOR_INFO` | Label |
| Max RPM | `GET_MOTOR_INFO` | Label |

## Launcher Scripts

### REQ-CLI-090: Windows Scripts

| Script | Purpose |
|--------|---------|
| `scripts/windows/run_gui.ps1` | PowerShell launcher |
| `scripts/windows/run_gui.bat` | Batch file wrapper (optional) |

### REQ-CLI-091: Linux Scripts

| Script | Purpose |
|--------|---------|
| `scripts/linux/run_gui.sh` | Bash launcher |

### REQ-CLI-092: Script Behavior

1. Check Python version (>= 3.10)
2. Create venv if not exists (`client/.venv`)
3. Activate venv
4. Install/update requirements (`pip install -r requirements.txt`)
5. Launch GUI with provided arguments

### REQ-CLI-093: Command Line Arguments

| Argument | Description | Example |
|----------|-------------|---------|
| `--transport` | Transport type | `--transport=vcp` or `--transport=rtt` |
| `--port` | Serial port (VCP) | `--port=COM5` or `--port=/dev/ttyACM0` |
| `--baud` | Baud rate (VCP) | `--baud=115200` |
| `--jlink-serial` | J-Link serial (RTT) | `--jlink-serial=123456` |
| `--auto-connect` | Connect on startup | `--auto-connect` |

## Project Structure

```
client/
├── requirements.txt
├── pyproject.toml          # Optional Poetry config
├── src/
│   ├── __init__.py
│   ├── main.py             # Entry point
│   ├── transport/
│   │   ├── __init__.py
│   │   ├── interface.py    # ITransport ABC
│   │   ├── vcp.py          # VcpTransport
│   │   └── rtt.py          # RttTransport
│   ├── protocol/
│   │   ├── __init__.py
│   │   ├── commands.py     # Command builders
│   │   └── responses.py    # JSON response parsing
│   ├── controller/
│   │   ├── __init__.py
│   │   ├── device.py       # Device state management
│   │   └── motor.py        # Motor control logic
│   ├── gui/
│   │   ├── __init__.py
│   │   ├── main_window.py  # Main application window
│   │   ├── motor_panel.py  # Motor control panel
│   │   ├── telemetry_panel.py
│   │   ├── display_panel.py
│   │   ├── connection_panel.py
│   │   └── resources/      # Icons, images
│   └── utils/
│       ├── __init__.py
│       └── image.py        # Image processing utilities
└── tests/
    ├── test_transport.py
    ├── test_protocol.py
    └── test_controller.py
```

## Dependencies

### Required (requirements.txt)

```
PySide6>=6.5.0
pyserial>=3.5
pylink-square>=1.0.0    # For RTT transport
Pillow>=10.0.0          # Image processing
```

### Optional

```
matplotlib>=3.7.0       # For telemetry graphs
pytest>=7.0.0           # Testing
black>=23.0.0           # Formatting
mypy>=1.0.0             # Type checking
```

## VS Code Integration

### REQ-CLI-100: Tasks

Add to `.vscode/tasks.json`:

| Task | Description |
|------|-------------|
| `run-gui` | Launch Python GUI |
| `run-gui-vcp` | Launch with VCP transport |
| `run-gui-rtt` | Launch with RTT transport |
| `client-install` | Install client dependencies |
| `client-lint` | Run linting on client code |
| `client-test` | Run client unit tests |

### REQ-CLI-101: Python Settings

Add to `.vscode/settings.json`:
- Python interpreter path (venv)
- Formatter (black)
- Linter (mypy, pylint)

## Implementation Checklist

### Phase 1: Project Setup
- [ ] Create `client/` directory structure
- [ ] Create `requirements.txt`
- [ ] Create launcher scripts (Windows + Linux)
- [ ] Add VS Code tasks

### Phase 2: Transport Layer
- [ ] Implement `ITransport` interface
- [ ] Implement `VcpTransport`
- [ ] Implement `RttTransport`
- [ ] Add transport factory

### Phase 3: Protocol Layer
- [ ] Implement command builders
- [ ] Implement JSON response parser
- [ ] Add error handling

### Phase 4: GUI Skeleton
- [ ] Create main window
- [ ] Create connection panel
- [ ] Create device info panel

### Phase 5: Motor Control
- [ ] Create motor control panel
- [ ] Create telemetry panel
- [ ] Wire to protocol layer

### Phase 6: Display Testing
- [ ] Create display test panel
- [ ] Implement image processing
- [ ] Add test patterns

### Phase 7: Polish
- [ ] Add icons and styling
- [ ] Implement auto-reconnect
- [ ] Add logging/diagnostics
- [ ] Write unit tests

## References

- [PySide6 Documentation](https://doc.qt.io/qtforpython-6/)
- [pyserial Documentation](https://pyserial.readthedocs.io/)
- [pylink Documentation](https://pylink.readthedocs.io/)
- [Pillow Documentation](https://pillow.readthedocs.io/)
