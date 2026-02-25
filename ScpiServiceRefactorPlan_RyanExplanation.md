# What's the Refactor Plan, and Why?

A plain-English explanation of the SCPI + Service Layering refactor plan,
the decisions behind it, and what it means for your firmware.

---

## The Problem

Right now, `command_parser.cpp` is a 3000-line monolith. It directly calls
into MotorTask, CommsTask, encoder functions, display functions, SPI debug
routines, motor config, device config, control mode, and command queue — all
from a single `dispatch()` method with 80+ `strcmp` branches.

This creates three problems:

1. **No stable contract**: A Python GUI client or Raspberry Pi brain has to
   know about internal command names that evolved organically during bringup.
   There's no principled namespace, no versioned API boundary.

2. **Safety gap**: `cmdClearFault()` unconditionally clears the fault latch
   without checking whether the hardware fault (overcurrent, thermal shutdown,
   undervoltage) is actually gone. You could clear a fault while the motor
   driver is still in a fault state.

3. **Coupling**: The parser knows about everything. Adding a new motor feature
   means editing the parser. Changing how heartbeats work means editing the
   parser. The parser is a god object.

---

## The Solution (4 Phases)

### Phase 1: SCPI Names + Safety Hardening

**What**: Add SCPI-style colon-separated command names (`MOT:RUN`, `SYST:ESTOP`,
`DIAG:PING`) alongside every existing flat command (`RUN`, `ESTOP`, `PING`).
Both work identically. Also fix the safety gap in `CLEAR_FAULT`.

**Why SCPI names?**
- They create a *namespace hierarchy* — you instantly know `MOT:RUN` is a motion
  command and `SYST:HB` is a system heartbeat, without reading docs.
- They're the standard for test & measurement equipment. Any engineer seeing
  `*IDN?` or `DIAG:PING` knows what they're looking at.
- They provide a *stable API boundary*. The Pi brain uses `MOT:RUN`. Bringup
  debug commands live under `DBG:`. The GUI can migrate at its own pace because
  `RUN` still works.

**Why safety hardening in Phase 1?**
- This is the highest-value safety fix: check the powerSTEP01 STATUS register
  before allowing a fault clear. If OCD, thermal shutdown, or UVLO bits are
  still active, the clear is rejected with an error.
- It's a small change (one function), but it prevents a dangerous scenario
  where you clear a fault and immediately re-enable a motor that's still in
  a fault condition.
- The architect (you) and both reviewers agreed: safety fixes ship first,
  not after a cleanup phase.

**Why widen cmd[16] to cmd[24]?**
- `UI:DISP:BITMAP:B64` is 19 characters. The current 16-byte buffer would
  silently truncate it to `UI:DISP:BITMAP:` and the command would never match.
  24 bytes gives comfortable headroom for all current SCPI names.

**Why two-level dispatch instead of just adding more strcmp branches?**
- The current `dispatch()` has 80+ sequential `strcmp` calls. Adding SCPI aliases
  would double that to 160+. Two-level dispatch (extract prefix, route to
  namespace handler) keeps each handler small and makes the code navigable.
- It also sets up Phase 2 cleanly — when we extract services, each `dispatchX()`
  handler already groups related commands.

### Phase 2: Service Extraction

**What**: Create three "service" modules that sit between the parser and the
low-level task/driver APIs:

- **MotionService** — wraps `MotorTask_SendCommand()` for run/move/goto/stop/etc.
- **SafetyService** — unifies ESTOP latch, heartbeat watchdog, and fault clearing
  (with STATUS check) in one place.
- **ConfigService** — handles ACC/DEC/MAXSPD with *integer unit conversion*
  for the SCPI commands, while legacy commands keep raw register values.

**Why only 3 services (not 5)?**
- During the debate, one reviewer proposed 5 services (adding Identity and Telemetry).
  We pushed back: Identity is just two registers (device ID + role) — not worth
  a service. Telemetry publishing already exists in comms_task. Creating micro-services
  for these adds files and indirection without solving a real problem.
- Three services cover the actual coupling boundaries: motion control, safety
  invariants, and configuration with unit conversion.

**Why namespace free functions instead of classes with virtual methods?**
- This is a Cortex-M4 with 96KB RAM. Virtual dispatch (vtable) costs:
  - 4 bytes per pointer per method per instance
  - An extra indirection on every call
  - RTTI overhead if you ever need `dynamic_cast`
- Namespace free functions (`Services::Motion::run()`) are zero-overhead.
  The compiler inlines or direct-calls them. For testing, you can swap
  implementations at link time (mock .cpp instead of real .cpp).
- This is the standard embedded C++ pattern. You get the same organizational
  benefit of interfaces without the runtime cost.

**Why integer math for unit conversions?**
- The powerSTEP01 ACC/DEC registers use a conversion factor of ~14.55 steps/s^2
  per LSB. The exact formula: `steps/s^2 = raw * 14552 / 1000`.
- We already proved this pattern works for SPEED conversion in motor_task.cpp:
  `steps/s = raw * 15625 / 1048576` (pure integer, no float).
- Float math on Cortex-M4 works (it has an FPU), but it's unnecessary here and
  adds subtle rounding concerns. Integer division is predictable, deterministic,
  and matches what the existing codebase already does.

**Why do SCPI commands use physical units but legacy commands keep raw?**
- The contract is: the SCPI API speaks the language of *physics* (steps/s,
  steps/s^2). The legacy API speaks the language of *registers* (raw 12-bit
  values). Both coexist.
- This means `ACCEL 100` sets register value 100 (as it always did), while
  `MOT:CFG:ACCEL 1455` sets 1455 steps/s^2 (which happens to be register
  value 100). No existing behavior changes.
- The Pi brain uses `MOT:CFG:ACCEL 5000` and doesn't need to know about
  12-bit register encoding. The debug console can still use `ACCEL 100`
  when you're looking at register values on the scope.

### Phase 3: Tracing

**What**: A static ring buffer (128 entries, no heap allocation) with
`TRACE_ENTRY` / `TRACE_EXIT` macros placed at service call boundaries and
transport RX. Dumpable via `DBG:TRACE:DUMP`.

**Why inline macros instead of traced wrapper classes?**
- One reviewer proposed wrapping each service in a `TracedMotionService` class
  that logs every call. This doubles the number of service files and creates
  a maintenance burden: every time you add a service method, you must also
  update the wrapper.
- Inline macros (`TRACE_ENTRY("MOT:RUN", speed, forward)`) are one line at
  each call site. They compile to nothing when tracing is disabled. No extra
  files, no wrapper maintenance.

**Why Phase 3 and not Phase 1?**
- Tracing is observability infrastructure. It's valuable, but it doesn't change
  external behavior. SCPI names and safety hardening are higher priority because
  they affect the client contract and safety. Tracing can be added incrementally
  after the service boundaries exist (it's more useful when you have clean
  entry/exit points to trace).

### Phase 4: Deferred

Things we discussed but deliberately deferred:
- **Async events** (`!FAULT`, `!STALL`) — push notifications instead of polling
- **Telemetry streaming** — periodic data without polling
- **ISR-driven CommsTask** — replace the 10ms poll loop with interrupt wakeup
- **Debug lockout** — require `DBG:UNLOCK <token>` before debug commands work
- **Display/Encoder service extraction** — lower priority, less coupling pain

These are all good ideas but they're additive features, not architectural fixes.
They can be done in any order after Phase 1-3 without disrupting the plan.

---

## How Agents Can Work in Parallel

Phase 1 is designed to be split across agents:

- **Agent A** (Parser infrastructure — 1A + 1B): Widens the command buffer,
  rewrites `dispatch()` into two-level routing, adds stub namespace handlers.
  Touches `command_parser.hpp` and the dispatch section of `command_parser.cpp`.

- **Agent B** (Safety hardening — 1C): Modifies `cmdClearFault()` to check
  STATUS register. Touches only the `cmdClearFault()` function body. No
  overlap with Agent A.

- **Agent C** (SCPI entries — 1D): Fills in each namespace handler with
  SCPI-to-handler routing. **Must wait for Agent A to finish** — it needs
  the stub `dispatchMotion()`, `dispatchSystem()`, etc. to exist before it
  can populate them.

A and B can run simultaneously. C starts after A completes.

---

## What Doesn't Change

Throughout all phases:
- Every existing flat command (`RUN`, `STOP`, `ESTOP`, `PING`, `MCONFIG`, etc.)
  continues to work exactly as it does today
- The Python GUI doesn't need any changes (it uses flat commands)
- ESTOP latch behavior is unchanged
- Heartbeat watchdog behavior is unchanged
- FreeRTOS task structure is unchanged (same tasks, same priorities, same queues)
- SPI communication is unchanged
- Flash persistence (motor config) is unchanged

---

## Decision Trail

| Decision | Options Considered | Chosen | Why |
|---|---|---|---|
| Service interfaces | Virtual classes vs. free functions | Free functions | No vtable overhead, link-time swap for testing |
| Tracing | Wrapper classes vs. inline macros | Inline macros | No file duplication, compiles out when disabled |
| Number of services | 5 (Motion, Safety, Config, Identity, Telemetry) vs. 3 | 3 | Identity too small, Telemetry already exists |
| Unit conversion math | Float vs. integer | Integer | Proven pattern from SPEED, deterministic, no FPU dependency |
| ConfigService timing | Phase 2 vs. Phase 4 | Phase 2 | Architect decision: clean SCPI contract needs physical units |
| Safety hardening timing | Phase 1 vs. Phase 2 | Phase 1 | Non-negotiable: safety fixes ship before cleanup |
| Action order | Docs first vs. code first | Code first | Delivery-first: working SCPI commands before architecture docs |
| cmd buffer size | 16 → 24 vs. 16 → 32 | 24 | 19 chars max (UI:DISP:BITMAP:B64) + null + margin |
