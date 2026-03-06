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

## 14) Namespace Boundary Enforcement (Build-Enforced)

Every cross-namespace dependency goes through `Harness::` interfaces.
No direct includes between namespaces. Always through `server/harness/pins/`.

### Adjacency Graph

```
System::    ↔ Harness ↔ Services::
Services::  ↔ Harness ↔ Scheduler::

Transport:: ↔ Harness ↔ Protocol::
Protocol::  ↔ Harness ↔ Services::
Services::  ↔ Harness ↔ Drivers::
Drivers::   ↔ Harness ↔ Board::
```

### Directory → Namespace Mapping

| Directory                       | Namespace    |
|---------------------------------|-------------|
| `server/layers/L1_transport/`   | Transport:: |
| `server/layers/L2_protocol/`    | Protocol::  |
| `server/layers/L3_services/`    | Services::  |
| `server/layers/L4_drivers/`     | Drivers::   |
| `server/layers/L5_board/`       | Board::     |
| `server/foundation/F_platform/tasks/` | Scheduler:: |
| `server/foundation/F_platform/` | Platform::  |
| `server/harness/`               | Harness::   |
| `server/system_init.cpp`        | System::    |

### Include Rules

Each namespace may only include:

| Namespace   | Allowed includes                                      |
|-------------|-------------------------------------------------------|
| Transport:: | `L1_transport/`, `harness/`                           |
| Protocol::  | `L2_protocol/`, `harness/`                            |
| Services::  | `L3_services/`, `harness/`                            |
| Drivers::   | `L4_drivers/`, `harness/`                             |
| Board::     | `L5_board/`, `harness/`, `X_vendor/`, `X_middlewares/`|
| Scheduler:: | `F_platform/`, `harness/`                             |
| Platform::  | `F_platform/`, `harness/`, `X_vendor/`, `X_middlewares/` |
| Harness::   | Everything (it IS the bridge)                         |
| System::    | `harness/`, `L3_services/`                            |

### CMSIS Rule

Only `Board::` (`L5_board/`) may include `X_vendor/CMSIS/stm32*.h` or `core_cm*.h`.
Platform:: (`F_platform/`) may include CMSIS for low-level runtime support (interrupt guard, runtime stats).
All other namespaces: CMSIS includes = violation.

### Flow Direction

Dependencies always flow in layer order — never skipping layers:

```
L1 → L2 → L3 → L4 → L5   (toward hardware)
L5 → L4 → L3 → L2 → L1   (toward transport)
```

Init cascades and business logic are always **triggered through Services:: (L3)**.
L3 is the entry point. It cascades down to L4 → L5 for hardware, and down to L2 → L1 for comms.

System:: (composition root) calls L3. L3 drives everything else.

### SCPI Command Flow

SCPI commands flow through the stack in strict layer order:

```
Client → Transport (L1) → Protocol (L2) → Services (L3) → Drivers (L4) → Board (L5)
```

1. **Transport (L1)** receives raw bytes from the client (UART, RTT, etc.)
2. **Protocol (L2)** frames, parses, and dispatches the command via a command dispatcher
3. **Services (L3)** receives the dispatched service request and executes business logic
4. **Drivers (L4)** are called by services to interact with hardware through abstractions
5. **Board (L5)** performs the actual register/peripheral access

The dispatcher lives in L2 (Protocol). It maps command namespaces to service handlers.
Services decide what to do. Drivers decide how to do it. Board touches hardware.

No layer may skip another. Protocol never touches drivers. Transport never touches services.

### Harness Role

Harness:: is a wiring harness — it connects interfaces, it does not define them.

The harness has three jobs:
1. **Connect** — wire provider interfaces to consumer interfaces across namespace boundaries
2. **Adapt** — reconcile mismatches when provider and consumer interfaces differ
3. **Tap** — tee off data flow for tracing and system snapshots

The harness does NOT contain business logic, state, or decisions.
It does NOT own interface definitions — those belong to the namespaces that provide them.

### Interface Ownership

**Interfaces belong to the namespace that provides them — not to the harness.**

In an Altium hierarchical schematic:
- Each sheet defines its own ports (what it exposes, what it expects)
- Nets connect ports between sheets — the net has no definition, it's just a wire
- Each sheet owns its port definitions

In firmware:
- Each namespace defines its own boundary interfaces at its edge
- The harness connects interfaces between namespaces — like nets on a schematic
- Each namespace owns its interface definitions

On each side of a harness boundary is an interface:

```
L3_services/boundary/  ← "here's what Services needs from a motor driver"
        ↕ harness connects + taps
L4_drivers/boundary/   ← "here's what Drivers provides as motor hardware"
```

The harness connector:
- Is synchronous (pass-through) or asynchronous (queue-mediated)
- May adapt when provider and consumer interfaces don't match exactly
- Tees data to the trace/snapshot system for observability
- Contains zero domain knowledge

**What lives WHERE:**

| Location | Contains |
|---|---|
| `<layer>/boundary/` | Abstract interfaces (pure virtual classes) this namespace provides |
| `harness/pins/` | Signal types (structs, enums shared across boundaries), init contracts, connector wiring |
| `harness/taps/` | Traced wrappers (observation proxies at boundaries) |
| `harness/trace/` | Trace recording infrastructure |

**When both sides need the same interface** (common case): the provider owns the
single definition. The consumer accesses it through the harness connection.

**When interfaces differ**: each side defines its own, and the harness adapter
bridges the gap. Neither side includes the other's headers directly.

### Enforcement

`scripts/check_includes.py` scans all `#include` directives and reports violations.
`scripts/check_interface_ownership.py` reports abstract interfaces in `harness/pins/`
that should be owned by their providing namespace.
Run both. Fix violations before they accumulate.

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