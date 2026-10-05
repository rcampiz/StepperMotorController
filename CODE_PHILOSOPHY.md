# Philosophy & Guidelines — Contract-Driven Client/Server Architecture for the STM32 Motion Controller

Purpose: Provide a clear, reviewable “design intent” document for an AI agent (and for myself) so it
can refactor/extend the codebase with consistent architecture, clear boundaries, orthogonality,
and built-in observability.

This system is distributed:
- SERVER: STM32 stepper motor controller firmware (“motion appliance”)
- CLIENT: any host that speaks the protocol (Python GUI/test harness, Raspberry Pi/CM4 robot brain, etc.)
Client is a ROLE, not a device.

---

## 1) Core Principle: Contract > Implementation

The most important artifact is the CONTRACT:
- the vocabulary (commands / telemetry / faults)
- the behavioral guarantees (what must always be true)
- the safety invariants (what cannot be violated)
- the timing/synchronization semantics (how coordinated motion works)

Implementation details are intentionally replaceable:
- FreeRTOS vs super-loop scheduling
- PowerSTEP01 vs other motor driver hardware
- USB vs UART vs CAN transport
- ASCII vs JSON vs binary encoding
- Mutex vs critical section vs interrupt masking

If implementation details change, the stable contract surface
(command set + state machine + safety invariants + error semantics)
must remain stable.

Architecture exists to preserve this replaceability.

---

## 2) RTOS Orthogonality (Non-Negotiable)

FreeRTOS is an implementation detail — not an architectural dependency.

The system must remain correct if:
- FreeRTOS is removed and replaced with a super-loop scheduler
- FreeRTOS is replaced with a different RTOS
- Interrupt-driven scheduling is substituted

Rules:

- No service-layer logic may depend directly on FreeRTOS types.
- No FreeRTOS headers may leak across architectural boundaries.
- No business logic may assume task semantics.
- Concurrency primitives must be abstracted behind a synchronization interface.
- Determinism must not rely on RTOS behavior.

If a mutex is required (e.g., SPI1 arbitration), it must be provided through
a platform synchronization abstraction, such as:

    ISynchronization
    ILock
    ICriticalSection

The implementation may use:
- FreeRTOS mutex
- interrupt masking
- atomic section
- spinlock
- cooperative scheduler guard

But higher layers must not know which.

The concurrency model must be swappable without modifying:
- Services
- Drivers
- Protocol logic
- Safety logic

RTOS = plug-in scheduler.
Never a structural dependency.

---

## 3) The Altium Analogy (How I Think About Boundaries)

In Altium:
- sheet symbol ports define allowed assumptions
- nets connect ports, not internal circuitry
- ERC verifies connectivity rules

In firmware:
- interfaces + message schemas define allowed assumptions
- modules connect through contracts, not reach-through calls
- compile + tests + runtime checks verify contract correctness

I want the code to feel like “hierarchical schematics”:
connect modules by ports; keep internals hidden.

---

## 4) What a “Boundary” Means in Code

A boundary is a place where assumptions are explicitly limited.

At a boundary:
- higher layers may only call through the boundary contract
- lower layers promise specific guarantees
- no one reaches through to registers or private internals
- no one reaches through to RTOS primitives

Important boundaries:
1) External protocol boundary: client ↔ server (SCPI-like contract)
2) Internal service boundaries: protocol dispatcher ↔ services
3) Driver boundaries: services ↔ device drivers
4) HAL boundary: drivers ↔ HAL/register access
5) Synchronization boundary: services/drivers ↔ platform concurrency
6) Safety boundary: E-STOP/watchdog/fault latch (cross-cutting but explicit)

Only Services may decide motion.
Parser/transport may reject malformed or unauthorized requests,
but may not enact motion.

---

## 5) Layering Rules (Dependency Direction)

Allowed dependency direction:

Transport (bytes) →
Framing/Parser (commands) →
Services (meaning & guarantees) →
Drivers (chip/hardware specifics) →
HAL/Registers (primitive access)

Platform layer (scheduling + synchronization) sits beneath everything,
and is only accessed through abstraction.

Rules:
- Parser must not manipulate hardware.
- Services must not poke registers.
- Drivers may touch HAL/registers only through a small HAL interface.
- No layer may directly call FreeRTOS APIs.
- Synchronization must occur through a platform abstraction.
- UI rendering is a service, not a driver concern.
- Safety enforcement lives at the service layer.

---

## 6) Contracts in Practice: Interface Surface vs Full Contract

- “Interface” (API surface) = callable functions/messages.
- “Contract” includes:
  - legal sequences (state machine)
  - timing semantics
  - safety invariants
  - error/rejection semantics
  - telemetry truth guarantees
  - concurrency guarantees

A contract is explicit when violations are detectable:
- compile-time types (where possible)
- runtime checks + explicit error codes
- state machine enforcing legal transitions
- automated tests (executable spec)

---

## 7) Synchronization Philosophy

Synchronization is a hardware arbitration concern — not a business concern.

Example:
If SPI1 must be shared between contexts:

- The SPI driver owns arbitration.
- Arbitration is implemented via an abstract lock.
- The service layer does not manage mutexes.
- The parser does not manage mutexes.

The lock must:
- Be constant-time
- Avoid heap allocation
- Preserve deterministic behavior
- Be replaceable without architectural changes

If removing FreeRTOS breaks the system,
the architecture is incorrect.

---

## 8) Virtual Interfaces vs Non-Virtual Interfaces

On the STM32 server:
- Prefer static composition and non-virtual interfaces.
- Use layering discipline to enforce boundaries.
- Avoid heap allocation and RTTI.

Virtual interfaces are appropriate when:
- injecting mock drivers
- simulation/testing
- runtime swapping is required

Host/client side may use dynamic polymorphism freely.

---

## 9) Observability: Tapping Interfaces Without Polluting Logic

Tracing must be orthogonal.

Preferred approach:
- Wrap boundary interfaces with decorators.
- Record trace records (timestamp, boundary, event, args, result).
- Forward to real implementation.

Trace sink options:
- Null sink
- Ring buffer
- UI sink
- Multi sink

Constraints:
- No heap allocation
- Constant time
- No behavior modification

Observability must never change semantics.

---

## 10) Stable API vs Bring-Up Debug API

A) Stable “robot-grade” contract:
- motion commands (RUN/MOVE/GOTO/HOLD/STOP/ENABLE/DISABLE)
- safety (ESTOP latch + clear handshake)
- synchronization (QUEUE/ARM/START/START_AT)
- telemetry/status
- identity (DEVICE_ID, ROLE)
- mode control (OPEN_LOOP/CLOSED_LOOP)

Must survive:
- RTOS changes
- hardware changes
- transport changes

B) Bring-up/debug:
- SPI_* / GPIO_* / PS01_* / raw register dumps
- direct driver access

Debug:
- must be gated
- must not bypass safety invariants
- may change without notice

---

## 11) “Chunk Too Large” — When to Split a Module

A module is too large when:
- it has more than one reason to change
- it mixes layers
- it cannot be described in one sentence without “and”
- it cannot be tested in isolation
- feature addition requires unrelated edits
- it encodes multiple implicit states

Heuristic:
If you need “and” to describe it, split it.

---

## 12) Structural Legibility: File Structure Must Mirror Boundaries

Architecture must be visible in the file system.

If boundaries are real, they must appear in:
- directory layout
- file naming
- include relationships
- dependency direction

Top-level = system composition  
Subdirectories = architectural boundaries  
Files within directory = internal circuitry  

If RTOS is orthogonal:
- There must be a platform/ or scheduler/ directory.
- No FreeRTOS headers outside that directory.
- Services must compile without RTOS includes.

If architecture is invisible in the file tree,
it is not truly enforced.

---

## 13) Desired Refactor Outcomes

- SCPI dispatcher thin and declarative.
- Services encode semantics + safety.
- Drivers adapt hardware.
- Synchronization abstracted.
- Core logic RTOS-independent.
- Observability at boundaries.
- Stable contract documented and enforced.
- Debug separated and gated.

---

## 14) Layer Rules and the Harness Model (Build-Enforced)

Four rules, checked on every build by `scripts/check_includes.py --strict` and
`scripts/check_harness_defs.py --strict` (the `arch-check` CMake target).
Where this section and earlier sections disagree, this section wins.

### Rule 1: Tasks call services

The task sheet (`F_platform/tasks`, `Scheduler::`) owns priorities, periods, stack sizes
and task handles, nothing else. Each task loop *waits* (queue, notification, sleepUntil)
and then calls **one** L3 entry point (`IMotorService`, `ICommsService`, `IEncoderService`,
`IDisplayService`). Tasks never include L1, L2, L4 or L5. The task sheet reaches L3 only
through `H_TASKS_L3`.

### Rule 2: Adjacent layers only

```
L1 Transport <-> L2 Protocol <-> L3 Services <-> L4 Drivers <-> L5 Board
```

Each pair sees the other **only** through that boundary's harness header. No skipping.
A layer's own `boundary/` directory holds the abstract interfaces it *provides*; the
consumer gets them via the harness header, never by including the provider directly.

### Rule 3: The one exception

Host command bytes go L5 -> L1 through `Transport::IByteChannel` (owned by L1, implemented by
`Board::UartHardware` and `Board::RttChannel`). It is declared in
`harness/defs/harness_l5_l1_bytes.hpp`, flagged `exception` in the manifest, and is the only
non-adjacent edge the checker accepts.

### Rule 4: Platform is a power net

`F_platform/boundary` primitives (`IClock`, `IMicrosecondClock`, `ILock`, `IQueue`,
`IScheduler`, `ITaskNotifier`, `ICriticalSection`, task stats) may be used by **any** sheet
without a harness, like VCC on a schematic. Consequence: L3 contains **no RTOS loops** and
builds natively for host tests (`tests/`).

**Second global net: `harness/trace/`.** The `ITRACE()` macros, `ITrace::log()` and the trace
sink are included directly by L2, L3 and the taps, exactly like Platform. They compile to
no-ops unless `ENABLE_INTERFACE_TRACE` is defined (firmware-debug only); the sink is the L3
trace ring, attached by `Trace::initTraceSink()` from the composition root. No harness row
describes this net; `docs/ARCHITECTURE.md` lists its direct includers.

### Rule 5: The composition root may see every sheet

`server/main.cpp` and `server/system_init.cpp` are the wiring diagram, not a layer. They call the
Platform, L5 and L3 init contracts and the task-init contracts (`Scheduler::init*Task`) directly,
create the RTOS tasks over the L3 services, and connect the Platform SPI locks. No shims in L3
front these calls, and no other sheet may do what the root does.

### Directory -> Namespace

| Directory                              | Namespace    | Role |
|----------------------------------------|--------------|------|
| `server/layers/L1_transport/`          | Transport::  | byte stream to/from host |
| `server/layers/L2_protocol/`           | Protocol::   | SCPI parse, all host-facing formatting |
| `server/layers/L3_services/`           | Services::   | business logic + safety, host-testable |
| `server/layers/L4_drivers/`            | Drivers::    | device drivers over Board pin bundles |
| `server/layers/L5_board/`              | Board::      | registers, peripherals, ISRs |
| `server/foundation/F_platform/tasks/`  | Scheduler::  | task loops (rule 1) |
| `server/foundation/F_platform/`        | Platform::   | power net (rule 4) |
| `server/harness/`                      | Harness::    | harness defs, connect(), pins, taps, trace |
| `server/system_init.cpp`               | System::     | composition root |

### One header per boundary

| Harness      | Header                                   | Connect function |
|--------------|------------------------------------------|------------------|
| H_L1_L2      | `harness/defs/harness_l1_l2.hpp`         | `connectL1L2()` |
| H_L2_L3      | `harness/defs/harness_l2_l3.hpp`         | `connectL2L3()` |
| H_L3_L4      | `harness/defs/harness_l3_l4.hpp`         | `connectL3L4()` |
| H_L4_L5      | `harness/defs/harness_l4_l5.hpp`         | `connectL4L5()` |
| H_L5_L1      | `harness/defs/harness_l5_l1_bytes.hpp`   | `connectL5L1Bytes()` (rule 3) |
| H_TASKS_L3   | `harness/defs/harness_tasks_l3.hpp`      | task handles only |

Each header is a struct of references plus a `constexpr SignalDesc[]` table that states, per
signal: provider side, flow direction, timing (sync / async-queue / async-flag), calling
context (task / ISR / pre-scheduler) and ownership. `scripts/gen_architecture.py` turns these
tables into `docs/ARCHITECTURE.md` and `docs/architecture_manifest.json`; the docs are
generated, never hand-edited.

All `connect*()` bodies live in `harness/defs/harness_connect.cpp`. That file is the
**only** place a tap (`harness/taps/`) is applied, via `Harness::applyTap<>()`. Taps exist only
when `ENABLE_INTERFACE_TRACE` is defined (firmware-debug preset); release builds compile the
raw interfaces straight through.

### What the harness is not

- It defines no interfaces: those belong to the providing layer's `boundary/`.
- It holds no adapters: if consumer and provider disagree, fix the interface, do not bridge it.
- It holds no state beyond the one static harness struct per boundary.
- `harness/pins/` holds only shared PODs (commands, snapshots, pin bundles) and init contracts.

### Include rules (as enforced)

| Sheet       | May include |
|-------------|-------------|
| L1          | `L1_transport/`, `harness_l1_l2.hpp`, `harness_l5_l1_bytes.hpp`, `harness/pins/`, `F_platform/boundary/` |
| L2          | `L2_protocol/`, `harness_l1_l2.hpp`, `harness_l2_l3.hpp`, `harness/pins/`, `F_platform/boundary/` |
| L3          | `L3_services/`, `harness_l2_l3.hpp`, `harness_l3_l4.hpp`, `harness/pins/`, `F_platform/boundary/` |
| L4          | `L4_drivers/`, `harness_l3_l4.hpp`, `harness_l4_l5.hpp`, `harness/pins/`, `F_platform/boundary/` |
| L5          | `L5_board/`, `harness_l4_l5.hpp`, `harness_l5_l1_bytes.hpp`, `harness/pins/`, `F_platform/boundary/`, CMSIS, middlewares |
| Tasks       | `F_platform/`, `harness_tasks_l3.hpp`, `harness/pins/` |
| Platform    | `F_platform/`, CMSIS, middlewares |
| Harness     | everything (it is the wiring) |
| System      | every sheet (rule 5) |

Only `Board::` (and Platform for runtime support) may include CMSIS device headers.

### Bring-up debug path

`DBG:*` commands reach `Services::DebugService` (L3) only after `DBG:UNLOCK`; it forwards raw
register/bus commands to `Drivers::IDebugAccess` -> `Board::IBoardDebug`. Debug never bypasses
the layers; it is a service like any other, just locked by default.

---

## Summary

This firmware should behave like a hierarchical schematic:

- Stable ports (contracts)
- Clear boundaries (directories + interfaces)
- Hidden internals (replaceable implementations)
- Orthogonal RTOS
- Abstract synchronization
- Clean layering
- Built-in observability
- Visible architecture in file structure

The STM32 is a real-time motion server.
The client requests motion.
The server enforces safety, executes motion,
and reports truth via telemetry and faults.