# System Requirements — STM32F401RE Motion-Control Device

This document defines **system-level requirements** for the STM32F401RE (NUCLEO-F401RE)–based motion-control device.
The STM32 + attached hardware is the **SERVER**. A Raspberry Pi or Windows PC application is the **CLIENT**.
Communication is over USB via the on-board debugger (ST-LINK or J-Link OB).

These requirements are **architectural, safety, protocol, and tooling** requirements. They must be captured and later enforced
in firmware and client software. **Do not implement code here.**

================================================================
SECTION 1 — SAFETY & HARDWARE LIMITS (MANDATORY)
================================================================

1.1 Hardware-Derived Motion Limits
- The firmware MUST enforce absolute safety limits derived from:
  - Stepper motor electrical limits (current, voltage, max step rate)
  - Motor mechanical limits (steps per revolution, max RPM)
  - PowerSTEP01 driver limits
- These limits are SERVER-ENFORCED and MUST NOT be overridable by the client.
- Any motion command exceeding safe bounds must be rejected with an explicit error response.

1.2 Encoder-Aware Safety
- If encoder feedback is enabled:
  - The server must detect position/speed divergence beyond a configurable threshold.
  - On fault, the server must immediately stop motion and report a fault condition.

1.3 Test Mode Safety
- A dedicated TEST MODE must exist.
- Entering test mode:
  - Disables external motion commands unless explicitly allowed.
  - Clearly indicates test mode on the local display.
  - Enforces conservative safety limits.

================================================================
SECTION 2 — DEVICE IDENTITY & FLASH-STORED METADATA
================================================================

2.1 Flash-Resident Identity Block
The firmware MUST define a flash-stored metadata region containing at least:

- device_id              (string, unique or user-defined)
- device_model           (string)
- firmware_version       (string or semver)
- hardware_revision      (string or integer)
- manufacturer           (string)
- build_timestamp        (string or epoch)

2.2 Connected Motor Identification (REQUIRED)
Flash MUST also store information about the currently connected stepper motor:

- motor_manufacturer     = "StepperOnline"
- motor_part_number      = "23HS22-2804-ME1K"
- motor_steps_per_rev
- motor_rated_current
- motor_rated_voltage
- motor_max_rpm (optional but recommended)

These fields must be readable by the client.

2.3 Flash Access Rules
- Flash writes must be explicit, deliberate operations.
- Client must never write flash implicitly.
- Flash schema versioning must exist to allow future expansion.

================================================================
SECTION 3 — CLIENT ↔ SERVER COMMUNICATION MODEL
================================================================

3.1 Transport
- Communication occurs over USB via:
  - UART Virtual COM Port (VCP) OR
  - SEGGER RTT (over J-Link OB)
- Transport selection must be abstracted behind a common interface.

3.2 Message Format
- All structured responses MUST be JSON.
- Commands may be ASCII or framed binary, but responses must be JSON.

Example response:
{
  "status": "ok",
  "device_id": "motor_node_01",
  "motor": {
    "manufacturer": "StepperOnline",
    "part_number": "23HS22-2804-ME1K"
  }
}

3.3 Introspection Commands (REQUIRED)
The server MUST support client queries for:
- Device identity
- Firmware version
- Hardware revision
- Connected motor metadata
- Current operating mode (normal/test/fault)
- Safety limits currently enforced

================================================================
SECTION 4 — DISPLAY & CLIENT-DRIVEN UI MODEL
================================================================

4.1 Client-Driven Display Content
- The CLIENT is responsible for generating all display images or text.
- The SERVER only renders validated content.

4.2 Image Handling Rules
- The CLIENT must size images to exactly match the display resolution.
- The SERVER must:
  - Reject images that exceed supported size or format.
  - Return a clear error message when rejecting content.
- The SERVER must NOT attempt to scale or reformat images.

4.3 Menu Model
- The client may implement menus by:
  - Sending full images representing each menu state.
  - Updating images as navigation inputs occur.
- Local buttons/joystick events are forwarded to the client as navigation events.

================================================================
SECTION 5 — MODES OF OPERATION
================================================================

5.1 Normal Mode
- Client may issue motion commands.
- Display may be driven by either:
  - Local firmware UI OR
  - Client-provided UI

5.2 Test Mode
- Entered via explicit command.
- Display switches to a test-specific layout.
- Enables controlled test sequences (e.g., step test, encoder test).
- Intended for factory validation and diagnostics.

5.3 Fault Mode
- Motion is disabled.
- Display indicates fault condition.
- Client may query fault details but cannot command motion.

================================================================
SECTION 6 — PYTHON CLIENT & GUI REQUIREMENTS
================================================================

6.1 Platform Support
- Python client must run on:
  - Linux (Raspberry Pi)
  - Windows

6.2 GUI Capabilities
The GUI must be able to:
- Discover and connect to the device
- Query and display device + motor metadata
- Enter/exit test mode
- Issue motion commands (within enforced limits)
- Run automated tests (step response, encoder tracking, etc.)
- Display live telemetry (position, velocity, faults)

6.3 Test Harness
- Tests are CLIENT-ORCHESTRATED.
- The server exposes test hooks and reports results.
- Test results must be retrievable in structured (JSON) form.

================================================================
SECTION 7 — NON-NEGOTIABLE DESIGN PRINCIPLES
================================================================

- Safety enforcement always lives on the SERVER.
- The CLIENT is powerful but never trusted.
- Flash-stored identity and configuration are first-class features.
- Display rendering is SERVER-side, but layout responsibility is CLIENT-side.
- All extensibility must assume future motors, displays, and clients.

================================================================
DELIVERABLES EXPECTED LATER (NOT NOW)
================================================================

- Markdown requirements files
- Flash layout definition
- JSON protocol specification
- Transport abstraction design
- FreeRTOS task interaction diagram
- Python GUI architecture
