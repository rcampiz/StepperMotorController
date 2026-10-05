# SCPI + Service Layering Refactor Plan

**Status**: CONSENSUS REACHED (2026-02-16)
**Source**: FORUM_20260216.md debate + REFACTOR_PLAN.md
**Authoritative SCPI mapping**: COMMAND_REFERENCE.md

---

## Principles

- **Delivery-first**: external contract value before internal cleanup
- **No big-bang**: each phase is independently shippable and testable
- **Safety invariants preserved**: ESTOP latch + watchdog behavior unchanged throughout
- **GUI continuity**: legacy flat commands remain as aliases at every phase
- **Embedded-appropriate**: no vtable, no heap allocation, no RTTI
- **Safety hardening first**: "clear only if safe" and power-on safe defaults are Phase 1

---

## Phase 1: Parser Infrastructure + Safety Hardening + SCPI Aliases

**Goal**: SCPI names work, safety semantics enforced, zero behavior regressions.

### 1A — Widen command token

**File**: `Core/Inc/comms/command_parser.hpp`

Change `ParsedCommand.cmd[16]` to `cmd[24]` (or 32).

Reason: `UI:DISP:BITMAP:B64` = 19 chars + null terminator; it must fit without truncation.

### 1B — Namespace prefix dispatch

**File**: `Core/Src/comms/command_parser.cpp` — `dispatch()` method

Replace the monolithic `if/else if` chain (~80 strcmp branches) with two-level dispatch:

1. Extract prefix before first `:` (or use full token if no colon)
2. Switch on prefix → call namespace handler
3. Within namespace handler, strcmp on suffix

```
Prefix routing:
  "MOT"   -> dispatchMotion(suffix, cmd)
  "SYST"  -> dispatchSystem(suffix, cmd)
  "SYNC"  -> dispatchSync(suffix, cmd)
  "DIAG"  -> dispatchDiag(suffix, cmd)
  "CTRL"  -> dispatchCtrl(suffix, cmd)
  "DEV"   -> dispatchDevice(suffix, cmd)
  "FMT"   -> cmdSetFormat / cmdGetFormat
  "UI"    -> dispatchUI(suffix, cmd)
  "DBG"   -> dispatchDebug(suffix, cmd)
  "DRV"   -> dispatchDriver(suffix, cmd)
  "*IDN?" -> cmdVersion()
  (none)  -> dispatchLegacy(cmd)
```

New private methods on CommandParser:
- `dispatchMotion`, `dispatchSystem`, `dispatchSync`, `dispatchDiag`,
  `dispatchCtrl`, `dispatchDevice`, `dispatchUI`, `dispatchDebug`,
  `dispatchDriver`, `dispatchLegacy`

**Header changes** (`command_parser.hpp`): Add the 10 new private method declarations.

### 1C — Safety hardening (non-negotiable)

**File**: `Core/Src/comms/command_parser.cpp` — `cmdClearFault()`

Before clearing, read powerSTEP01 STATUS register via `MotorTask_GetDebugInfo`.
If hardware faults still active (OCD, TH_SD, UVLO), return error and do NOT clear.

```cpp
void CommandParser::cmdClearFault() {
    Tasks::MotorDebugInfo info;
    if (Tasks::MotorTask_GetDebugInfo(info)) {
        // Check STATUS register fault bits
        bool ocd      = !(info.status & (1 << 12));  // OCD active-low
        bool th_sd    = !(info.status & (1 << 11));   // Thermal shutdown
        bool uvlo     = !(info.status & (1 << 9));    // Undervoltage
        if (ocd || th_sd || uvlo) {
            respondErr("Hardware fault still active - cannot clear");
            return;
        }
    }
    // Existing clear logic...
}
```

Also document power-on safe defaults in `main.cpp`:
- State: IDLE
- Outputs: HiZ (disabled)
- Watchdog: disabled until explicitly set

### 1D — SCPI command entries + legacy aliases

Implement SCPI names in namespace handlers. Keep flat commands in `dispatchLegacy()`.
No behavior changes — only new names routing to existing `cmd*()` handlers.

### 1E — SCPI mapping table

Use `COMMAND_REFERENCE.md` as the source of truth for command-to-handler mappings.
The complete SCPI-to-legacy mapping is below for agent reference.

### Acceptance criteria

- `MOT:RUN 100 1` and `RUN 100 1` behave identically
- `SYST:FAULT:CLEAR` and `CLEAR_FAULT` behave identically
- `CLEAR_FAULT` / `SYST:FAULT:CLEAR` rejects if hardware fault flags still active
- GUI works with no client changes (all legacy flat names still work)

---

## Phase 2: Minimal Service Extraction (Motion + Safety + Config)

**Goal**: Parser depends on services, not Tasks/Drivers directly.

### 2A — MotionService (thin wrappers)

**New files**:
- `Core/Inc/services/motion_service.hpp`
- `Core/Src/services/motion_service.cpp`

Namespace free functions (no class, no vtable):

```cpp
// Core/Inc/services/motion_service.hpp
#pragma once
#include <stdint.h>

namespace Services::Motion {
    enum class Result : uint8_t { OK, QUEUE_FULL, INVALID_PARAM, FAULT };

    Result run(uint32_t stepsPerSec, bool forward);
    Result move(int32_t steps);
    Result goTo(int32_t position);
    Result stop(bool hard);
    Result enable();
    Result disable();
    Result home();
    Result zero();
}
```

Implementation wraps `MotorTask_SendCommand()`. Raw units initially. No float conversions.

### 2B — SafetyService

**New files**:
- `Core/Inc/services/safety_service.hpp`
- `Core/Src/services/safety_service.cpp`

Unifies ESTOP latch + heartbeat watchdog + fault clear with STATUS check:

```cpp
namespace Services::Safety {
    enum class Result : uint8_t { OK, FAULT_ACTIVE, INVALID_STATE };

    Result emergencyStop();
    Result clearFault();           // checks STATUS before clearing
    Result setHeartbeatTimeout(uint32_t ms);
    void   heartbeatReceived(uint32_t seq);
    void   getStatus(/* out params */);
}
```

### 2C — ConfigService (integer units)

**New files**:
- `Core/Inc/services/config_service.hpp`
- `Core/Src/services/config_service.cpp`

Integer unit conversion for ACC/DEC/MAXSPD:

```cpp
namespace Services::Config {
    // SCPI commands use physical units (steps/s, steps/s^2)
    // Legacy commands continue to use raw register values

    // ACC/DEC conversion (same integer pattern as SPEED):
    //   raw = stepsPerSecSq * 1000 / 14552
    //   stepsPerSecSq = raw * 14552 / 1000
    Result setAccelPhysical(uint32_t stepsPerSecSq);
    Result setDecelPhysical(uint32_t stepsPerSecSq);
    Result setMaxSpeedPhysical(uint32_t stepsPerSec);

    // Raw register access (legacy)
    Result setAccelRaw(uint16_t raw);
    Result setDecelRaw(uint16_t raw);
    Result setMaxSpeedRaw(uint16_t raw);
}
```

### 2D — Parser refactor

`command_parser.cpp` calls `Services::Motion::*`, `Services::Safety::*`,
`Services::Config::*` instead of Tasks:: directly.

Keep encoder/display direct calls for now (defer to Phase 4).

### Acceptance criteria

- Parser includes only service headers (except deferred encoder/display)
- No behavioral change in motion/safety responses
- `MOT:CFG:ACCEL` accepts steps/s^2 using integer conversion
- Legacy `ACCEL` remains raw (1-4095)

---

## Phase 3: Tracing v1 (selective, constant-time)

**Goal**: Add trace points at Transport + Parser + Motion/Safety.

### 3A — Trace ring buffer

**New files**:
- `Core/Inc/services/trace.hpp`
- `Core/Src/services/trace.cpp`

Static ring buffer, no heap, constant time. 128 entries if RAM allows.

```cpp
// Core/Inc/services/trace.hpp
#pragma once
#include <stdint.h>

#define TRACE_ENTRY(tag, ...) Trace::record(Trace::ENTRY, tag, __VA_ARGS__)
#define TRACE_EXIT(tag, ...)  Trace::record(Trace::EXIT,  tag, __VA_ARGS__)

namespace Trace {
    enum Dir : uint8_t { ENTRY, EXIT };
    struct Entry {
        uint32_t tick;
        const char* tag;
        uint32_t arg0;
        Dir dir;
    };
    void record(Dir d, const char* tag, uint32_t arg0 = 0);
    void dump(/* transport callback */);
}
```

### 3B — Trace decorators

Add `TRACE_ENTRY`/`TRACE_EXIT` in MotionService + SafetyService + Transport RX.
Inline macros at call sites — no wrapper classes.

### 3C — Dump command

`DBG:TRACE:DUMP` → returns ring buffer contents over transport.

---

## Phase 4: Deferred (Future Sprints)

- Async events (`!FAULT`, `!MOTION:COMPLETE`, `!STALL`)
- Telemetry streaming (`DIAG:TELEM:EN/DIS/RATE`)
- Protocol version negotiation (`SYST:VER:PROTO?`)
- Display service extraction
- Encoder service extraction
- Debug command lockout (`DBG:UNLOCK`/`DBG:LOCK`)
- Driver swap abstraction
- ISR-driven CommsTask wake (replace 10ms poll)

---

## SCPI Command Mapping (Complete)

This is the authoritative mapping from SCPI names to legacy names and handler methods.
See also `COMMAND_REFERENCE.md` for legacy command details.

### Identity / Version

| SCPI Name | Legacy Alias | Handler Method |
|---|---|---|
| `*IDN?` | `VER`, `VERSION` | `cmdVersion()` |
| `DEV:ID?` | `GET_DEVICE_ID` | `cmdGetDeviceId()` |
| `DEV:ID` | `SET_DEVICE_ID` | `cmdSetDeviceId()` |
| `DEV:ROLE?` | — | `cmdGetDeviceId()` (role field) |
| `DEV:ROLE` | `SET_ROLE` | `cmdSetRole()` |

### Control Mode / Encoder

| SCPI Name | Legacy Alias | Handler Method |
|---|---|---|
| `CTRL:MODE?` | `GET_MODE` | `cmdGetMode()` |
| `CTRL:MODE` | `SET_MODE` | `cmdSetMode()` |
| `CTRL:ENC:STAT?` | `GET_ENCODER_STATUS` | `cmdGetEncoderStatus()` |
| `CTRL:ENC?` | `ENCODER`, `ENC` | `cmdEncoder()` |
| `CTRL:ENC:DBG?` | `ENC_DEBUG` | `cmdEncDebug()` |

### System / Heartbeat / Timing

| SCPI Name | Legacy Alias | Handler Method |
|---|---|---|
| `SYST:TICK?` | `GET_TICK` | `cmdGetTick()` |
| `SYST:HB` | `HEARTBEAT` | `cmdHeartbeat()` |
| `SYST:HB:TIMEOUT` | `SET_HEARTBEAT` | `cmdSetHeartbeat()` |
| `SYST:HB:STAT?` | `GET_HEARTBEAT_STATUS` | `cmdGetHeartbeatStatus()` |

### Safety

| SCPI Name | Legacy Alias | Handler Method |
|---|---|---|
| `SYST:ESTOP` | `ESTOP` | `cmdEstop()` |
| `SYST:FAULT:CLEAR` | `CLEAR_FAULT` | `cmdClearFault()` |

### Diagnostics

| SCPI Name | Legacy Alias | Handler Method |
|---|---|---|
| `DIAG:PING` | `PING` | `cmdPing()` |
| `DIAG:STAT?` | `GET_STATUS` | `cmdGetStatus()` |

### Motion Commands

| SCPI Name | Legacy Alias | Handler Method |
|---|---|---|
| `MOT:EN` | `ENABLE` | `cmdEnable()` |
| `MOT:DIS` | `DISABLE` | `cmdDisable()` |
| `MOT:STOP` | `STOP` | `cmdStop()` |
| `MOT:HOME` | `HOME` | `cmdHome()` |
| `MOT:ZERO` | `ZERO` | `cmdZero()` |
| `MOT:MOVE` | `MOVE` | `cmdMove()` |
| `MOT:GOTO` | `GOTO` | `cmdGoTo()` |
| `MOT:RUN` | `RUN` | `cmdRun()` |

### Motion Configuration

| SCPI Name | Legacy Alias | Handler Method | Units |
|---|---|---|---|
| `MOT:CFG:ACCEL` | — (new) | `cmdAccel()` via ConfigService | steps/s^2 (integer) |
| `MOT:CFG:DECEL` | — (new) | `cmdDecel()` via ConfigService | steps/s^2 (integer) |
| `MOT:CFG:MAXSPD` | — (new) | `cmdMaxSpd()` via ConfigService | steps/s (integer) |
| `ACCEL` (legacy) | — | `cmdAccel()` | raw 12-bit (1-4095) |
| `DECEL` (legacy) | — | `cmdDecel()` | raw 12-bit (1-4095) |
| `MAXSPD` (legacy) | — | `cmdMaxSpd()` | raw 10-bit (1-1023) |

### Synchronization

| SCPI Name | Legacy Alias | Handler Method |
|---|---|---|
| `SYNC:QUEUE` | `QUEUE` | `cmdQueue()` |
| `SYNC:ARM` | `ARM` | `cmdArm()` |
| `SYNC:START` | `START` | `cmdStart()` |
| `SYNC:START:AT` | `START_AT` | `cmdStartAt()` |
| `SYNC:CLEAR` | `CLEAR_QUEUE` | `cmdClearQueue()` |

### Response Format

| SCPI Name | Legacy Alias | Handler Method |
|---|---|---|
| `FMT` | `SET_FORMAT` | `cmdSetFormat()` |
| `FMT?` | `GET_FORMAT` | `cmdGetFormat()` |

### UI / Display

| SCPI Name | Legacy Alias | Handler Method |
|---|---|---|
| `UI:MODE` | `UI_MODE` | `cmdUIMode()` |
| `UI:MODE?` | `UI_GET_MODE` | `cmdUIGetMode()` |
| `UI:DISP:CLEAR` | `DISP_CLEAR` | `cmdDispClear()` |
| `UI:DISP:TEXT` | `DISP_TEXT` | `cmdDispText()` |
| `UI:DISP:RECT` | `DISP_RECT` | `cmdDispRect()` |
| `UI:DISP:LINE` | `DISP_LINE` | `cmdDispLine()` |
| `UI:DISP:BITMAP` | `DISP_BITMAP` | `cmdDispBitmap()` |
| `UI:DISP:BITMAP:B64` | `DISP_BITMAP_B64` | `cmdDispBitmapB64()` |

### Motor Configuration (powerSTEP01 registers)

| SCPI Name | Legacy Alias | Handler Method |
|---|---|---|
| `DRV:CFG?` | `MCONFIG` | `cmdMotorConfigShow()` |
| `DRV:CFG:SAVE` | `MCONFIG_SAVE` | `cmdMotorConfigSave()` |
| `DRV:CFG:LOAD` | `MCONFIG_LOAD` | `cmdMotorConfigLoad()` |
| `DRV:CFG:RESET` | `MCONFIG_RESET` | `cmdMotorConfigReset()` |
| `DRV:CFG:KVAL` | `MCONFIG_KVAL` | `cmdMotorConfigKval()` |
| `DRV:CFG:OCD` | `MCONFIG_OCD` | `cmdMotorConfigOcd()` |
| `DRV:CFG:STALL` | `MCONFIG_STALL` | `cmdMotorConfigStall()` |
| `DRV:CFG:FAULT` | `MCONFIG_FAULT` | `cmdMotorConfigFault()` |
| `DRV:CFG:MOTION` | `MCONFIG_MOTION` | `cmdMotorConfigMotion()` |
| `DRV:CFG:APPLY` | `MCONFIG_APPLY` | `cmdMotorConfigApply()` |

### Debug (Bringup — not stable contract)

| SCPI Name | Legacy Alias | Handler Method |
|---|---|---|
| `DBG:SPI:*` | `SPI_DEBUG`, `SPI_MODE_TEST`, etc. | Various inline handlers |
| `DBG:GPIO:*` | `GPIO_STATE`, `GPIO_DUMP`, etc. | Various inline handlers |
| `DBG:PS01:*` | `PS01_DIAG`, `PS01_RESET`, etc. | Various inline handlers |
| `DBG:MOTOR` | `MOTOR_DEBUG` | `cmdMotorDebug()` |
| `DBG:TRACE:DUMP` | — (Phase 3) | New trace dump handler |

---

## Parallelization Map

```
                    +-------------------------------+
                    | Phase 1: Parser + Safety + SCPI|
                    +-------------------------------+
                                   |
          +------------------------+------------------------+
          |                        |                        |
 +--------+--------+     +--------+--------+     +--------+--------+
 | Agent A          |     | Agent B          |     | Agent C          |
 | Parser infra     |     | Safety hardening |     | SCPI entries     |
 | (1A + 1B)        |     | (1C)             |     | (1D)             |
 +--------+--------+     +--------+--------+     +--------+--------+
          |                        |                        |
          |                        |                        |
          +-----------+------------+                        |
                      |                                     |
                      v                                     |
             Agent A complete -----> Agent C starts --------+
```

**Dependencies**:
- A and B are **parallel** (no shared files)
- C depends on A's namespace dispatch stubs being in place
- C can start as soon as A completes (B can still be running)

### Agent A scope (Parser infra — 1A + 1B)
- Widen `ParsedCommand.cmd[16]` to `cmd[24]` in header
- Replace dispatch `if/else if` chain with prefix extraction + namespace handler calls
- Each `dispatchX()` method initially just falls through to `dispatchLegacy()` (stub)
- Add 10 private method declarations to `command_parser.hpp`

### Agent B scope (Safety hardening — 1C)
- Modify `cmdClearFault()` to check STATUS register before clearing
- Add power-on safe defaults documentation in `main.cpp`
- No parser structural changes (operates on existing handler)

### Agent C scope (SCPI entries — 1D)
- Fill in each `dispatchX()` with SCPI name → existing handler routing
- Use mapping table above as source of truth
- Keep `dispatchLegacy()` with all existing flat names (backward compat)

---

## Critical Path

Longest path: Parser infra (A) → SCPI entries (C) → build/test

Parallel path: Safety hardening (B) runs alongside parser infra (A)

---

## Key Files Affected

### Phase 1
| File | Changes |
|---|---|
| `Core/Inc/comms/command_parser.hpp` | Widen cmd[16]→cmd[24], add 10 dispatch method declarations |
| `Core/Src/comms/command_parser.cpp` | Rewrite dispatch(), add namespace handlers, harden cmdClearFault() |
| `Core/Src/main.cpp` | Document power-on safe defaults (comments) |

### Phase 2
| File | Changes |
|---|---|
| `Core/Inc/services/motion_service.hpp` | **NEW** — namespace free functions |
| `Core/Src/services/motion_service.cpp` | **NEW** — wraps MotorTask_SendCommand |
| `Core/Inc/services/safety_service.hpp` | **NEW** — unifies safety APIs |
| `Core/Src/services/safety_service.cpp` | **NEW** — wraps CommsTask + CommandQueue + STATUS check |
| `Core/Inc/services/config_service.hpp` | **NEW** — integer unit conversion |
| `Core/Src/services/config_service.cpp` | **NEW** — ACC/DEC/MAXSPD with physical units |
| `Core/Src/comms/command_parser.cpp` | Replace Tasks:: calls with Services:: calls |

### Phase 3
| File | Changes |
|---|---|
| `Core/Inc/services/trace.hpp` | **NEW** — ring buffer + macros |
| `Core/Src/services/trace.cpp` | **NEW** — static ring buffer impl |
| `Core/Src/services/motion_service.cpp` | Add TRACE_ENTRY/EXIT |
| `Core/Src/services/safety_service.cpp` | Add TRACE_ENTRY/EXIT |
| `Core/Src/comms/uart_transport.cpp` | Add TRACE_ENTRY on RX |

---

## Non-Negotiables

1. Safety hardening is Phase 1 — CLEAR_FAULT must check STATUS before clearing
2. GUI continuity via aliases is mandatory — every legacy flat command still works
3. No float-based unit conversions — integer math only (proven pattern from SPEED conversion)
4. No big-bang refactor — each phase ships independently
5. No vtable / no heap / no RTTI — namespace free functions, static buffers
6. Inline trace macros, not wrapper classes
