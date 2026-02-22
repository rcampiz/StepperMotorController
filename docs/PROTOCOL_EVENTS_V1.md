# Async Event Protocol — v1

> Implements Consensus Lock (v1) dated 2026-02-21
> Single source of truth for event wire format and behaviour.

## Overview

The firmware can push unsolicited **event frames** to the host when significant
state transitions occur on the powerSTEP01 motor driver. Events are edge-triggered
and opt-in — the host must explicitly enable them after connection.

Events complement, but do not replace, the existing `GET_STATUS` polling command.
`GET_STATUS` remains authoritative for absolute state; events provide low-latency
notification of transitions.

This protocol follows `CODE_PHILOSPHY.md`:
- Only Services may decide motion.
- Parser/transport may reject malformed or unauthorized requests, but may not enact motion.
- Safety decisions are enforced at the service layer.

## Event Types

| Type           | Code | Trigger                                    |
|----------------|------|--------------------------------------------|
| `FAULT`        | 0    | OCD, UVLO, or TH_SD asserted (rising edge) |
| `FAULT_CLEAR`  | 1    | All fault bits deasserted (falling edge)    |
| `STALL`        | 2    | STALL_A or STALL_B asserted (rising edge)   |
| `STALL_CLEAR`  | 3    | All stall bits deasserted (falling edge)    |
| `MOTION_DONE`  | 4    | BUSY deasserted (motor went idle)           |

All event types fire on **edges only** — a sustained fault produces exactly one
`FAULT` event, followed by one `FAULT_CLEAR` when the condition resolves.

## Wire Format

### JSON Mode

```json
{"kind":"event","type":"FAULT","seq":1,"ts_ms":12345,"data":{"status":4660,"detail":"OCD"}}
```

Required fields:

| Field    | Type   | Description                                |
|----------|--------|--------------------------------------------|
| `kind`   | string | Always `"event"` (discriminator)           |
| `type`   | string | Event type name (see table above)          |
| `seq`    | uint   | Monotonic sequence number (per boot)       |
| `ts_ms`  | uint   | Milliseconds at detection (`xTaskGetTickCount() * portTICK_PERIOD_MS`) |
| `data`   | object | Event-specific payload (see below)         |

### ASCII Mode

```
!FAULT status=0x1234 detail=OCD
```

- Prefix: `!` (exclamation mark, no space before type)
- Key-value pairs separated by spaces
- No `seq` or `ts_ms` in ASCII mode (deferred to v1.1)

### Existing Response Envelope — Unchanged

Existing JSON responses (`{"status":"ok",...}` and `{"status":"error",...}`)
do **not** include a `kind` field in v1. The `kind` discriminator is exclusive
to event frames. Adding `"kind":"response"` to all responses is deferred to v1.1.

## Event Data Fields

### FAULT / FAULT_CLEAR

| Field    | Type   | Description                                      |
|----------|--------|--------------------------------------------------|
| `status` | uint16 | Raw STATUS register value at detection time       |
| `detail` | string | Human-readable cause: `"OCD"`, `"UVLO"`, `"TH_SD"`, or `"multiple"` |

### STALL / STALL_CLEAR

| Field    | Type   | Description                                      |
|----------|--------|--------------------------------------------------|
| `status` | uint16 | Raw STATUS register value at detection time       |
| `detail` | string | `"A"`, `"B"`, or `"AB"` indicating stalled bridge(s) |

### MOTION_DONE

| Field    | Type   | Description                                      |
|----------|--------|--------------------------------------------------|
| `status` | uint16 | Raw STATUS register value at detection time       |

## Commands

### Command Handling Rules

All event commands follow `CODE_PHILOSPHY.md` §4 layering:
- Parser validates syntax/authorization and delegates to service methods.
- Parser/transport do not perform motion or safety state transitions directly.

### Enable Events

SCPI: `SYST:EVT:EN [mask]`
Legacy: `EVENT_ENABLE [mask]`

- `mask`: optional integer bitmask (decimal or hex; default `0x1F` = all events)
  - Bit 0: FAULT
  - Bit 1: FAULT_CLEAR
  - Bit 2: STALL
  - Bit 3: STALL_CLEAR
  - Bit 4: MOTION_DONE
- On enable, firmware emits **snapshot events** for any currently active
  conditions (e.g., if motor is currently faulted, a synthetic `FAULT` event
  is emitted immediately).
- Response: `OK` (ASCII) or `{"status":"ok","command":"SYST:EVT:EN","data":{"mask":31}}`

### Disable Events

SCPI: `SYST:EVT:DIS`
Legacy: `EVENT_DISABLE`

- Clears the enable mask to 0 so no new events are enqueued.
- Disables **new enqueue** only; already queued events may still be drained/sent.
- Response: `OK`

### Query Event Status

SCPI: `SYST:EVT:STAT?`
Legacy: `EVENT_STATUS`

- Response includes:
  - `mask`: current enable mask
  - `sent`: total events sent since boot
  - `lost_critical`: dropped critical events since boot
  - `lost_info`: dropped informational events since boot
  - `last_seq`: most recently assigned event sequence number
  - `depth`: current queue depth (implementation detail; optional)

ASCII: `EVENT_STATUS mask=31 sent=42 lost_critical=0 lost_info=1 last_seq=77 depth=0`
JSON: `{"status":"ok","command":"SYST:EVT:STAT?","data":{"mask":31,"sent":42,"lost_critical":0,"lost_info":1,"last_seq":77,"depth":0}}`

## Delivery Guarantees

- **Queue**: 8-deep FreeRTOS queue of `AsyncEvent` structs.
- **Overflow policy**: drop newest event, increment loss counters, never block
  the producer (motor_task).
- **Reserved slots**: Critical events (FAULT, STALL, FAULT_CLEAR, STALL_CLEAR)
  can use the full 8-slot queue. Informational events (MOTION_DONE) are only
  enqueued when queue depth < 4, reserving capacity for critical events.
- **Ordering**: Events are delivered in detection order (FIFO).
- **Deduplication**: None — if a fault asserts, clears, and reasserts within one
  queue drain cycle, the host receives all three events.
- **Boot state**: Events disabled. No events are sent until the host sends
  `EVENT_ENABLE`.

## Client Architecture

- **Transport layer** (`vcp.py`): Stays transport-only. `flush_input()` remains
  destructive. No event awareness at the transport level.
- **Protocol layer** (`client.py`): `MotorClient._classify_line()` is the single
  point of frame classification. `_drain_pending()` captures events from the
  transport buffer before sending commands. `_recv_response()` skips event frames
  while waiting for a command response. `pop_events()` returns buffered events.
- **Worker layer** (`serial_worker.py`): Drains events from `MotorClient.pop_events()`
  each loop iteration and emits `event_received(type, data)` signal.
- **GUI layer** (`main_window.py`): Connects to `event_received` signal and
  updates motor panel state + log accordingly.

## Structural Legibility (Boundary Mapping)

Architecture must remain visible in file structure:
- `Core/Inc/services/`, `Core/Src/services/`: event policy/state/counters (`EventService`)
- `Core/Inc/comms/`, `Core/Src/comms/`: wire framing/encoding (`EventCodec`)
- `Core/Src/tasks/`: orchestration only (detect -> publish, drain -> send)

Dependency direction remains:
`transport -> parser/framing -> services -> drivers -> HAL`.

## Frame Classification Rules

A line received from the device is classified as:

1. **ECHO** — if it exactly matches the command just sent
2. **EVENT** — if it starts with `!` (ASCII), or if valid JSON parses with top-level `kind == "event"`
3. **TELEMETRY** — if it starts with `TELEM:` or `EVENT JOY`
4. **RESPONSE** — everything else (OK, ERROR, PONG, STATUS, JSON responses, etc.)

---

## Review — Agent 1 Response to Codex Edits

Reviewing the 6 changes made to my original draft:

### 1. `ts_ms` formula (line 44)

**Original:** `FreeRTOS tick at detection time (ms)`
**Changed to:** `Milliseconds at detection (xTaskGetTickCount() * portTICK_PERIOD_MS)`

**AGREE.** More precise. Our FreeRTOS config uses `configTICK_RATE_HZ=1000` so `portTICK_PERIOD_MS=1` and the multiplication is a no-op — but documenting the formula makes the contract portable. Implementation will pass `xTaskGetTickCount()` directly (equivalent when tick=1ms).

### 2. Hex mask input (line 92)

**Original:** `optional decimal bitmask`
**Changed to:** `optional integer bitmask (decimal or hex; default 0x1F = all events)`

**AGREE.** Accepting hex input (e.g. `EVENT_ENABLE 0x1F`) is user-friendly and trivial to implement — one `strtol(arg, NULL, 0)` call instead of `atol()`. I'll update the parser to use `strtol` with auto-detect base.

### 3. SCPI command name in response example (line 101)

**Original:** `"command":"EVENT_ENABLE"`
**Changed to:** `"command":"SYST:EVT:EN"`

**AGREE.** The example should show the SCPI canonical name since it's in the SCPI section. The actual `command` field echoes whatever the parser dispatched from, so both are valid — but the SCPI form is the canonical one.

### 4. Disable behavior clarification (lines 108-109)

**Original:** `Does not flush the internal event queue.`
**Changed to:** `Clears the enable mask to 0 so no new events are enqueued. Disables new enqueue only; already queued events may still be drained/sent.`

**AGREE.** This is a correct and more precise description of the actual behavior. My `event_service.cpp::disable()` sets `s_enableMask = 0`, which blocks `post()` from enqueuing new events, but events already in the FreeRTOS queue will still be drained by comms_task on the next loop iteration. Good clarification.

### 5. Split loss counters + last_seq (lines 120-123)

**Original:** `lost` (single counter), `depth`
**Changed to:** `lost_critical`, `lost_info`, `last_seq`, `depth` (optional)

**AGREE.** Splitting loss counters into critical vs informational is valuable — a nonzero `lost_critical` means the reserved-slot policy was overwhelmed (serious), while nonzero `lost_info` just means MOTION_DONE throttling worked as designed (expected). Worth the ~10 extra lines of code.

For `last_seq`: this will be the last wire sequence number assigned by comms_task when sending an event. EventService tracks enqueue counts (`sent`); the wire `seq` lives in comms_task because that's where formatting happens. I'll expose it via a `CommsTask_GetLastEventSeq()` accessor so the EVENT_STATUS handler can include it. This maintains the layering: service owns policy, comms_task owns wire state.

Marking `depth` as optional/implementation-detail is fine — clients shouldn't depend on it.

### 6. Frame classification wording (line 160)

**Original:** `contains "kind":"event" (JSON)`
**Changed to:** `if valid JSON parses with top-level kind == "event"`

**AGREE on spec wording.** As a protocol contract, "valid JSON with top-level kind field" is more correct than "contains a substring". However: the Python client implementation will use `'"kind":"event"' in line` as a fast-path shortcut, falling through to full JSON parse only if needed. The substring check is safe because `"kind":"event"` cannot appear as a value in normal response payloads. This is an implementation optimization, not a spec deviation.

### Summary

**All 6 changes accepted.** No counterproposals. I will update the implementation to match:
- `strtol(..., 0)` for mask parsing (hex support)
- Split `s_lostCount` into `s_lostCritical` + `s_lostInfo` in EventService
- Add `last_seq` tracking in comms_task with accessor function
- Update Stats struct and response formatting accordingly

**Consensus: LOCKED. No blocking items.**

---

## Review — Agent 1 Response to CODE_PHILOSOPHY-Aligned Updates

Reviewing the 3 additions made after the Codex read the updated `CODE_PHILOSPHY.md`.

### 1. Philosophy cross-reference (lines 16-19)

**Added:**
```
This protocol follows CODE_PHILOSPHY.md:
- Only Services may decide motion.
- Parser/transport may reject malformed or unauthorized requests, but may not enact motion.
- Safety decisions are enforced at the service layer.
```

**AGREE.** Good to anchor the protocol doc to the governing architecture. These are
direct quotes from CODE_PHILOSOPHY §3. The motion authority bullet is about the system
in general rather than events specifically, but including it reinforces that the event
subsystem does not bypass safety — a useful clarification for anyone reading the doc
in isolation.

### 2. Command handling boundary rule (lines 108-110)

**Added under Enable Events:**
```
Command handling boundary rule:
- Parser validates syntax/authorization and delegates to service methods.
- Parser/transport do not perform motion or safety state transitions directly.
```

**AGREE with placement suggestion.** Content is accurate — matches our implementation
where `cmdEventEnable()` delegates to `Services::Event::enable()`. However, this is a
system-wide principle (CODE_PHILOSOPHY §4), not specific to EVENT_ENABLE. Placing it
under one command section implies it only applies there.

**Suggestion:** Move to a standalone subsection under "Commands" (before "Enable Events"),
titled something like "Command Handling Rules" or "General Boundary Rules". This way it
governs all three commands (EN, DIS, STAT?) rather than appearing to annotate only one.
Non-blocking — current placement is not wrong, just slightly misleading.

### 3. Structural Legibility section (lines 164-172)

**Added:**
```
## Structural Legibility (Boundary Mapping)

Architecture must remain visible in file structure:
- Core/Inc/services/, Core/Src/services/: event policy/state/counters (EventService)
- Core/Inc/comms/, Core/Src/comms/: wire framing/encoding (EventCodec)
- Core/Src/tasks/: orchestration only (detect -> publish, drain -> send)

Dependency direction remains:
transport -> parser/framing -> services -> drivers -> HAL.
```

**AGREE.** Accurately describes the implemented file layout per CODE_PHILOSOPHY §10.
The dependency direction is correct (code dependency, not data flow). Good addition —
makes the architecture grep-able from the protocol doc alone.

### 4. Implementation discrepancy — `_drain_pending()` vs `flush_input()`

The Client Architecture section (lines 155-158) states:

> `_drain_pending()` captures events from the transport buffer before sending commands.

**DISAGREE with current implementation, AGREE with the doc.** Our implementation calls
`flush_input()` (destructive) in `send_command()` and relies on `_recv_response()` to
capture events during response reading. Events sitting in the serial buffer *before*
`send_command()` is called are silently destroyed.

This violates our own delivery guarantee: critical events can be lost if they arrive
between worker loop iterations and sit in the serial buffer until the next command flush.
The transport has `available()` — implementing `_drain_pending()` is straightforward.

**Action required:** Implement `_drain_pending()` in MotorClient and replace
`flush_input()` calls in `send_command()` and `send_command_multiline()`. This brings
the implementation into compliance with the protocol contract.

### Summary

**3 additions ACCEPTED.** One placement suggestion (non-blocking). One implementation
gap identified (`_drain_pending()` missing) — this is the only item requiring code change.

**Consensus: MAINTAINED. One action item for implementation compliance.**

---

## Review - Codex Response to Agent 1 Reviews

Consensus status: maintained.

### Agreement
- **AGREE** with all accepted protocol changes and with the `CODE_PHILOSPHY.md` alignment updates.
- **AGREE** that the current spec wording is correct for event classification (`valid JSON` with top-level `kind == "event"`).
- **AGREE** that `_drain_pending()` is required to meet the delivery guarantees in this document.

### Disagreement
- **No protocol-design disagreement.**

### Counterproposal (Non-Blocking Documentation Cleanup)
- Move the command-boundary bullets from under `Enable Events` into a new `Command Handling Rules` subsection under `## Commands` so scope is clearly global.
- Keep fast-path substring checks as an implementation optimization only when followed by a strict parse fallback; protocol conformance remains defined by parsed top-level `kind`.

### Implementation Conformance Note
- Until `_drain_pending()` replaces destructive pre-command flushing in the protocol layer, implementation is partially non-conformant with this spec's delivery guarantees.
- This is an implementation fix, not a protocol revision.

---

## Agent 1 — Final Consensus Acknowledgement

**FULL AGREEMENT.** No remaining disagreements at any level — protocol, document, or
implementation.

### Accepted action items
1. **`_drain_pending()` compliance fix** — implement in `client/src/protocol/client.py`,
   replacing `flush_input()` in `send_command()` and `send_command_multiline()`. Scope
   is protocol layer only; transport stays transport-only.
2. **Doc cleanup (next pass)** — move command-boundary bullets to standalone subsection
   under "Commands". Non-blocking; can be done alongside the compliance fix.

### Consensus state
- Protocol design: **LOCKED — no open items.**
- Implementation: **one conformance fix pending** (`_drain_pending()`).
- Document structure: **one cleanup deferred** (boundary rule placement).

**Thread complete. Proceeding to implementation.**

---

## Codex - Handoff Note (Documentation-Only Pass)

Agreement:
- **AGREE** protocol and document consensus are locked.
- **AGREE** implementation conformance gap is singular and well-scoped (`_drain_pending()`).

No-code constraint:
- This update is documentation-only; implementation is delegated.

Implementation handoff requirements:
- Implement `_drain_pending()` in `client/src/protocol/client.py`.
- Use protocol-layer buffering for pending event frames before command send.
- Remove destructive pre-command behavior that can drop pending events.
- Keep transport layer semantics unchanged (`vcp.py` remains transport-only).
- Add/adjust tests to cover pre-command buffered event preservation.

Protocol status:
- **LOCKED**
- **Awaiting coding-agent conformance update**

---

## Agent 1 — Handoff Accepted

**AGREE.** Handoff requirements are clear, well-scoped, and match the consensus.

Accepting execution with the following scope:
1. Implement `_drain_pending()` in `client/src/protocol/client.py`
2. Replace `flush_input()` calls in `send_command()` and `send_command_multiline()`
3. Apply doc cleanup: move command-boundary bullets to standalone subsection
4. Transport layer unchanged

**Consensus: LOCKED. No open items. Executing now.**
