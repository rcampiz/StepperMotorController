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