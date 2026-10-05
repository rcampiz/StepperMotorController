# Refactor Plan - SCPI Contract + Service Layering (Corrected)

Agreed 2026-02-16 via FORUM_20260216.md debate.
Participants: User (architect), Claude Opus (reviewer), additional reviewers.

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

### 1A - Widen command token (5 min)

**File**: `Core/Inc/comms/command_parser.hpp`

Change `ParsedCommand.cmd[16]` -> `cmd[24]` (or 32 if you want room for full SCPI strings).

Reason: `UI:DISP:BITMAP:B64` = 19 chars; it must fit without truncation.

### 1B - Namespace prefix dispatch

**File**: `Core/Src/comms/command_parser.cpp` - `dispatch()` method

Replace the monolithic `if/else if` chain with a two-level dispatch:

1. Extract prefix before first `:` (or use full token if no colon)
2. Switch on prefix -> call namespace handler
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
  (none)   -> dispatchLegacy(cmd)
```

New methods on CommandParser (private):
- `dispatchMotion`, `dispatchSystem`, `dispatchSync`, `dispatchDiag`,
  `dispatchCtrl`, `dispatchDevice`, `dispatchUI`, `dispatchDebug`,
  `dispatchDriver`, `dispatchLegacy`.

### 1C - Safety hardening (non-negotiable)

**File**: `Core/Src/comms/command_parser.cpp` - `cmdClearFault()`

Before clearing, read powerSTEP01 STATUS register via `MotorTask_GetDebugInfo`.
If hardware faults still active (OCD, TH_SD, UVLO), return error and do not clear.

Also document explicit power-on safe defaults in `main.cpp`:
- State: IDLE
- Outputs: HiZ (disabled)
- Watchdog: disabled until explicitly set

### 1D - SCPI command entries + legacy aliases

Implement SCPI names in namespace handlers and keep flat commands in `dispatchLegacy()`.
No behavior changes, only new names.

**Note**: If you want to preserve `UI:DISP:BITMAP:B64`, then cmd length must be >= 20.
Do **not** silently shorten to `BMP64` unless the proposal is updated.

### 1E - SCPI mapping table (authoritative)

Use the SCPI map in `COMMAND_REFERENCE.md` as the source of truth for
command-to-handler mappings and aliases. Do not rely on memory or forum notes.

### Acceptance criteria

- `MOT:RUN 100 1` and `RUN 100 1` behave identically
- `SYST:FAULT:CLEAR` and `CLEAR_FAULT` behave identically
- CLEAR_FAULT rejects if hardware fault flags still active
- GUI works with no client changes

---

## Phase 2: Minimal Service Extraction (Motion + Safety + Config)

**Goal**: Parser depends on services, not Tasks/Drivers directly.

### 2A - MotionService (thin wrappers)

**New files**:
- `Core/Inc/services/motion_service.hpp`
- `Core/Src/services/motion_service.cpp`

Use **raw units initially** (no float conversions). Keep behavior identical.

### 2B - SafetyService

**New files**:
- `Core/Inc/services/safety_service.hpp`
- `Core/Src/services/safety_service.cpp`

Wrap `CommandQueue` + heartbeat watchdog, enforce STATUS check in one place.

### 2C - ConfigService (integer units)

**New files**:
- `Core/Inc/services/config_service.hpp`
- `Core/Src/services/config_service.cpp`

Implement ACC/DEC/MAXSPD in physical units using integer math (no floats).
Legacy commands still accept raw register values. SCPI commands accept
steps/s and steps/s^2. Use the same integer conversion pattern as SPEED.

### 2D - Parser refactor

`command_parser.cpp` calls `Services::Motion::*`, `Services::Safety::*`,
`Services::Config::*`.
Keep encoder/display direct calls for now (defer to Phase 4).

### Acceptance criteria

- Parser includes only service headers (except deferred encoder/display).
- No behavioral change in motion/safety responses.
- `MOT:CFG:ACCEL` accepts steps/s^2 using integer conversion.
- Legacy `ACCEL` remains raw (1-4095).

---

## Phase 3: Tracing v1 (selective, constant-time)

**Goal**: Add trace points at Transport + Parser + Motion/Safety.

### 3A - Trace ring buffer

**New files**:
- `Core/Inc/services/trace.hpp`
- `Core/Src/services/trace.cpp`

Static ring buffer, no heap, constant time. Consider 128 entries if RAM allows.

### 3B - Trace decorators

Add `TRACE_ENTRY/TRACE_EXIT` in MotionService + SafetyService + Transport RX.

### 3C - Dump command

`DBG:TRACE:DUMP` -> returns ring buffer contents.

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

## Parallelization Map

```
                        +-------------------------------+
                        | Phase 1: Parser + Safety + SCPI|
                        +-------------------------------+
                                       |
              +------------------------+------------------------+
              |                        |                        |
     +--------+--------+      +--------+--------+      +--------+--------+
     | Agent A         |      | Agent B         |      | Agent C         |
     | Parser infra    |      | Safety hardening|      | SCPI entries     |
     | (1A + 1B)       |      | (1C)            |      | (1D)             |
     +--------+--------+      +--------+--------+      +--------+--------+
              |                        |                        |
              |                        |                        |
              +-----------+------------+                        |
                          |                                     |
                          v                                     |
                 Agent A complete -> Agent C starts             |
```

A and B are parallel. C depends on A's namespace stubs.

---

## Critical Path

Longest path:
- Parser infra -> SCPI entries -> build/test

Parallel path:
- Safety hardening runs alongside parser infra

---

## Summary (non-negotiables)

- Safety hardening is Phase 1.
- GUI continuity via aliases is mandatory.
- Avoid float-based unit conversions.
- No big-bang refactor.

---

## Review History

Counter-review issues (ConfigService timing, parallelization dependency,
SCPI mapping table, integer math) have been resolved and incorporated into
the plan above. See FORUM_20260216.md for the full debate record.

**Status: CONSENSUS REACHED** (2026-02-16)

All reviewers agree on:
- 4-phase structure (Parser+Safety+SCPI -> Services -> Tracing -> Deferred)
- Safety hardening in Phase 1
- ConfigService with integer unit conversion in Phase 2
- Namespace free functions (no vtable)
- Inline trace macros (no wrapper classes)
- Legacy aliases preserved at every phase
- Agent A||B parallel, C sequential after A
