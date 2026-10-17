# Brief for a diagramming agent: the system as it is now, and what changed on 2026-10-17

Audience: an agent that will draw diagrams and write a short Markdown review for the
firmware author. Everything below is self-contained; file paths are relative to the repo
root. Read `docs/diagram/01_harness_mechanics.md`, `02_sheets.md` and `03_open_issues.md`
for the longer history; `docs/ARCHITECTURE.md` and `docs/PROTOCOL.md` are generated from
the code and are always current.

## 1. The system in one paragraph

A NUCLEO-F401RE (STM32F401, FreeRTOS) drives one stepper through a powerSTEP01 on the
X-NUCLEO-IHM03A1 shield over SPI1, reads a 4000-count quadrature encoder on TIM4 (sampled
by TIM3 + DMA at 1 kHz), and shows status on a GFX01M2 LCD. A host (Windows PC or
Raspberry Pi) talks to it over the ST-LINK virtual COM port at 115200 baud with a
line-based SCPI-style protocol (protocol version 2, JSON replies). The host side is a
Python package with a shared core, a Qt GUI and a CLI; a "virtual Nucleo" compiles the
firmware's real protocol and service layers for the desktop over TCP; a Fleet layer
drives four boards as a mecanum base.

## 2. Architecture as it is now (draw as a layered board with the harness as nets)

### 2.1 Firmware layers (server/)

The metaphor is an Altium schematic: layers are sheets, interfaces are connectors,
`harness/` holds the nets. Rules (`docs/diagram/01_harness_mechanics.md`): tasks call
services only; a layer calls only the adjacent lower layer through its boundary
interfaces (`layers/Lx/boundary/*.hpp`); the only exception is L5 -> L1 (the UART byte
channel); the Platform is a "power net" everyone may use.

| Layer | Directory | Owns | Talks down through |
|---|---|---|---|
| Tasks (FreeRTOS task sheets) | `server/foundation/F_platform/tasks/` | Motor task (prio 4, 50 ms status poll), Comms task (prio 3), Encoder task (prio 2), Display task (prio 1) | the L3 service interfaces |
| L1 Transport | `server/layers/L1_transport/` | `UartTransport` over an `IByteChannel`; `ITransport` is the L1/L2 connector (`baudRate()` is new) | L5 UART hardware |
| L2 Protocol | `server/layers/L2_protocol/` | `CommandParser` + `handlers/*.cpp` (one file per namespace: MOT, DRV, SYST, SYNC, CTRL, DEV, UI, DIAG), `status_json.hpp` (DIAG:STAT? render, `telemetryPeriodFloorMs()` is new), `ServiceDispatcher` (fan-out to L3 dispatchers) | L3 dispatcher interfaces (`L3_services/boundary/i*_dispatcher.hpp`) |
| L3 Services | `server/layers/L3_services/` | `motion/` (MotorService = Motor task body, MotorController = control loop over IMotorDriver, MotorConfigManager, EncoderService, sysid, speed trim, following supervisor), `dispatch/command_queue/` (SYNC queue: IDLE/ARMED/RUNNING/FAULT/ESTOP), `comms/` (CommsService = Comms task body, telemetry push), `config/device_config/` (DEV:ID / DEV:ROLE on NOR flash), `infra/` (telemetry store seqlock, tick timer, flash image service), `ui/` (LCD screens), `display/` | L4 driver interfaces (`L4_drivers/boundary/`) |
| L4 Drivers | `server/layers/L4_drivers/` | `devices/powerstep01.hpp`, `encoder.cpp`, `lcd_st7789.hpp`, `flash_nor.hpp` (SPI NOR), `internal_flash_journal_store.hpp` (new, replaces `internal_flash_config_store.hpp`), `init/*` (driver init contracts) | L5 board interfaces (`L5_board/boundary/`) |
| L5 Board | `server/layers/L5_board/` | SPI1/SPI2 hardware + SPI manager (mutexes), USART2 ring + ISR, TIM3/DMA encoder sampler, TIM4 counter, EXTI index, internal flash erase/program, pins (`board_pins.hpp`) | registers |
| Platform (power net) | `server/foundation/F_platform/` | FreeRTOS mutexes/queues/clock/task stats, `PlatformResources` (one more mutex: `deviceConfigLock`), early debug UART, boot id | — |
| Harness | `server/harness/` | `defs/harness_l*_l*.hpp` (connector tables, checked by `scripts/arch/check_harness_defs.py`), `pins/` (shared plain types: `MotorCommand`, `TelemetrySnapshot`, `ServiceStatus`, protocol version), `taps/` (trace wrappers), `defs/harness_connect.cpp` (composition root helpers) | — |

Composition root: `server/system_init.cpp` (boot order: Clock, TickTimer, Platform, SPI
Manager, L3 Services, Motor, Motor task, Encoder, Encoder task, Display [+ device
identity, new], Display task, Comms, Comms task).

Cross-task plumbing worth drawing: the Comms task never touches the motor driver except
for one Hi-Z write on ESTOP (`MotorService::immediateHardHiZ`, sets the stop latch); every
other driver access is a `MotorCommand` queued to the Motor task (`MotorCmdType`: Move,
GoTo, Run, stops, SetStepMode, and now StartAt). Telemetry flows the other way through the
`TelemetryStore` (seqlock) that the Comms task renders into `DIAG:STAT?` / the telemetry
push.

### 2.2 Host side (client/)

```
client/
  smc_core/      transport (serial / tcp / rtt / memory), framing.classify, codec, board.py
                 (Board: reader + worker threads, Liveness dead-man heartbeat, ESTOP fast path,
                 watchdog widening around flash-writing commands), commands.py (typed builders
                 generated against protocol_spec.py), views.py, units.py, sim.py (FakeBoard),
                 scenarios/ (basic.py: bringup, estop_latency, heartbeat_loss,
                 step_mode_readback, sysid; motion.py [new]: motion_scale, step_mode_chip,
                 telemetry_load), runner.py, kinematics.py (mecanum), fleet.py (four boards)
  smc_gui/       PySide6 window; bridge.py is the only Qt seam over Board; panels (motion,
                 driver, step mode, protection, telemetry, sysid, display, tests)
  smc_cli/       smc ports | info | send | telemetry | estop | test list/run | fleet ...
  tests/         pytest: core (fake board), gui sim, identical-output, virtual Nucleo, fleet
sim/vnucleo/     the firmware's L2 + L3 + task bodies compiled for the desktop over TCP with a
                 FakePowerStep and FakeEncoderHw (build-vnucleo/vnucleo.exe)
tools/chassis_cost/   STEP import + BOM costing (own venv)
scripts/         arch/ (checkers + generators), build/, bench/, client/ (launchers),
                 setup/ (bringup.ps1 / bringup.sh [new]), debug/
```

### 2.3 Generated truth

`scripts/arch/gen_protocol.py` reads the L2 handlers and writes `docs/PROTOCOL.md`,
`docs/protocol.json`, the firmware help table and `client/smc_core/protocol_spec.py`;
`gen_architecture.py` writes `docs/ARCHITECTURE.md` and the manifest from the harness
tables; `gen_fake_dispatchers.py` writes the host-test fakes from the boundary headers.
`--check` modes run in the CMake `arch-check` target.

## 3. What changed on 2026-10-17 (first day with the board on the bench)

Context: the GUI's "100 steps" jog at a believed 1/128 microstep turned two revolutions,
and the LCD encoder angle looked wrong. The investigation found the chip ignoring the
step-mode write, then several more things while measuring. Every item below was verified
on the bench (COM5) and on the virtual Nucleo; firmware debug/release builds, 13 host
tests, 4 architecture checkers and 281 client tests are green after each commit.

### 3.1 Firmware (one diagram each; "before -> after")

**A. Step mode through the Hi-Z-safe path** (fbcba12). The powerSTEP01 accepts a
STEP_MODE write only with the bridges in Hi-Z. `MotorController::applyConfig()` wrote it
unconditionally, so with the output enabled the chip kept 1/32 while flash and GUI said
1/128. Now: `applyConfig()` writes STEP_MODE only in Hi-Z and verifies; otherwise
`MotorService::applyConfig()` sends the `SetStepMode` message to the Motor task (Hi-Z,
write, verify, SoftStop), the same path `DRV:STEP_MODE` uses. `DRV:CFG:APPLY` answers
`MOTOR_BUSY` while moving and `VERIFY_FAILED` on mismatch. Draw: Comms task -> applyConfig
-> [Hi-Z? write in place : queue SetStepMode] -> Motor task -> chip.

**B. Telemetry period floored by the link** (7acd0db). The UART TX busy-waits in the
Comms task, which outranks the Encoder task; a 900-byte status push every 50 ms saturated
115200 baud and froze the encoder snapshot for a whole move. `SYST:TLM:RATE` is now
floored at `telemetryPeriodFloorMs(baud)` (one worst-case 1280-byte line may use at most
80 % of the link: 139 ms at 115200), the ack carries `min_ms`, and `ITransport` /
`IByteChannel` report `baudRate()`. Draw: a timeline of the Comms task TX busy-wait
starving the Encoder task, then the same with the floor. The interrupt-driven TX ring
stays for Phase 4.

**C. Device identity store initialised** (e44c6a5). `g_deviceConfig.init()` had not been
called since Feb 2026, so `DEV:ID` / `DEV:ROLE` setters dereferenced a null lock.
`initDisplaySubsystem` now initialises it on the NOR flash it probes (reserved sectors
0x000000..0x001FFF below the image slots) with a new `deviceConfigLock`; boot log prints
`Device identity... OK | DEFAULTS`; setters refuse instead of crashing without a flash.
Draw: the NOR flash map (identity sectors, image slots) and the boot order.

**D. Encoder velocity in counts per second** (57c4069). `EncoderService` reported
counts per sample (delta / window samples); every consumer (sysid, speed trim, following
supervisor, LCD, GUI) takes ticks per second, a factor 1000 off, and at 100 steps/s the
value fell into the ±2 deadband and read 0. Now `delta x sampleRateHz / windowSamples`.
Bench: 100 / 400 / 800 steps/s read 1987 / 7944 / 15885 counts/s against 2000 / 8000 /
16000 (the chip runs 99 / 399 / 799 steps/s). Draw: the sampling window arithmetic.

**E. Config saves append to a flash journal** (94e7495). Every apply or save erased the
128 KB internal sector 7 from flash-resident code, freezing the whole MCU (ESTOP
included) for 1 to 2 s. New L4 `InternalFlashJournalStore`: the sector holds 1984
64-byte slots; `write()` appends into blank flash, `read()` returns the newest record,
`remaining()` reports free slots; the pre-journal blob at the sector's last 4 KB is the
first-boot fallback. `MotorConfigManager` erases only when the journal is full: at boot
when fewer than 16 saves are left, or at run time only if the erase gate (motor stopped,
installed by the composition root) allows it; otherwise `STORE_FULL` ("Config store
full: stop the motor and save again", a new `ServiceStatus`). `configSaveToFlash()` now
returns a `ServiceStatus`. The L5 program routine unlocks the flash itself. Bench: a
save answers in about 35 ms end to end instead of 1 to 2 s, at rest and while running.
Draw: sector 7 layout (slots, legacy area) and the save decision tree.

**F. `SYNC:START:AT` runs on the Motor task** (4f8b707). It used to sleep and spin on
the Comms task until the start tick, so no command (ESTOP included) was parsed
meanwhile. Now `CommandQueue::startAt` validates the tick (past: `TIMEOUT_EXPIRED`;
more than 2 s ahead: `INVALID_PARAM`), queues a `StartAt` command and answers
`OK SCHEDULED` at once; `MotorService::handleStartAt` sleeps in 1 ms steps, spins the
last 1.5 ms and flushes the armed queue through the new `IScheduledStart` interface,
abandoning the wait on the ESTOP latch or a cleared queue. Bench: reply in 10.6 ms, a
ping answered in 11.9 ms during the wait, motion running at the 800 ms target. Draw: a
sequence diagram Comms task / CommandQueue / Motor task / chip, before and after.

### 3.2 Client

**G. Reply correlation** (7dd7d0d, ebd08ef). Two ways a reply could be handed to the
wrong request, each shifting every later reply by one: a timed-out request's late reply
(the JSON `command` name differs from the request name for 23 commands; now matched
through `CommandSpec.json_command`), and the tail of a telemetry line cut in half when
the port opens (now recorded as junk and dropped while a single-reply request is in
flight). Draw: the one-in-flight request/reply pipeline in `board.py` with the two traps.

**H. Motion behaviour scenarios** (baca894). `client/smc_core/scenarios/motion.py`:
`motion_scale` (ABS_POS and encoder per N microsteps, REV form, counts per revolution
measured), `step_mode_chip` (CFG + APPLY reaches the chip with the output disabled and
enabled), `telemetry_load` (the encoder snapshot keeps advancing during a move at a
given push period). They run from `smc test run`, the GUI Tests tab, the fake board and
the virtual Nucleo; the fakes were made honest (FakePowerStep ignores STEP_MODE outside
Hi-Z and leaves Hi-Z on a stop; FakeBoard has timed moves and an encoder in counts per
revolution).

**I. GUI step-mode tab** (37a414a). After apply the tab reads `DRV:STEP_MODE?` (the
chip) instead of trusting the saved config, and flags a config/chip mismatch in red.

**J. Current mode vs voltage mode** (firmware + GUI). The powerSTEP01 can regulate coil
current (CM_VM bit of STEP_MODE, writable only in Hi-Z) instead of driving a KVAL duty.
Config blob V3 stores `driveMode` and four TVAL references in the bytes that were reserved
(V2 blobs migrate: voltage mode, 0.4 A hold / 0.8 A run); `applyConfig()` writes TVAL or
KVAL into the shared 0x09-0x0C registers and carries the CM_VM bit through the same Hi-Z
path as the microstep bits, so a step-mode change never flips the mode. New commands
`DRV:CFG:DRIVE VOLTAGE|CURRENT` and `DRV:CFG:TVAL <h> <r> <a> <d>` (hex, 78 mA per unit
over the shield's 0.1 ohm sense); `DRV:CFG?` gained `Drive:` and `TVAL:` lines and
`DRV:STEP_MODE?` reports `drive_mode` from the chip. GUI driver tab: a drive-mode combo and
four TVAL faders in amps next to the KVAL faders. Scenario `drive_mode_chip` toggles the
chip in Hi-Z and with the output enabled (passes on the board and the virtual Nucleo).
Bench at 200 steps/s (1/128): voltage 3970 counts/s (std 15.2), current with the default
0.8 A run 3969 counts/s (std 15.6): same speed, same ripple. Draw: the STEP_MODE register
bit map (SYNC[7:4], CM_VM[3], STEP_SEL[2:0]) and which config bytes feed which register
in each mode.

### 3.3 Tooling and docs

**J. Fresh-clone bring-up** (8ceaddc). `scripts/setup/bringup.ps1` (Windows, winget)
and `bringup.sh` (Debian / Raspberry Pi OS, apt) verify or install every tool, fetch the
FreeRTOS submodule, create the Python venvs, and queue whatever needs administrator
rights or a manual download (ST-LINK USB driver, winget itself) with link and steps;
`-OpenLinks` opens the pages. Earlier the same day: `scripts/` reorganised into `arch/`,
`build/`, `bench/`, `client/`, `setup/`, `debug/` (febbb2e); Phase 4 Step 0 bench
baseline recorded in `docs/bringup/PHASE4_STEP0_BASELINE.md` (27b1164).

`docs/diagram/03_open_issues.md` carries a status row per item; `docs/MULTI_BOARD.md`
section 7 marks items 1, 3, 4 and 6 done; `docs/review/LINK_BUDGET.md` findings 1, 2 and
4 resolved.

## 4. Still open (for the "what is next" box)

- Phase 4 fast ESTOP: 0x18 out-of-band byte in the USART2 ISR, STBY/RESET pin pulse,
  interrupt-driven UART TX ring, SPI1 held during remote bitmaps (`03_open_issues.md`
  items 1, 2, 3, 6). Needs the author's logic-analyzer numbers, the LCD Task Monitor
  stack marks and the shield's STBY resistor check from Step 0.
- `build=` in the `!RESET` line is captured at CMake configure time, so it can lag the
  commit after a plain rebuild.
- Chassis cost tool: waits for the author's STEP export, then the catalog CSV and the
  costing GUI.

## 5. Commit list (newest first)

```
4f8b707 firmware: SYNC:START:AT runs on the Motor task, the Comms task no longer blocks
94e7495 firmware: config saves append to a flash journal instead of erasing the sector
57c4069 firmware: encoder velocity in counts per second
e44c6a5 firmware: initialise the device identity store (DEV:ID / DEV:ROLE crashed on a null lock)
37a414a gui: step-mode tab shows the chip's readback and reports a refused apply
ebd08ef client: drop the tail of a line cut at port open instead of taking it as a reply
7acd0db firmware: floor SYST:TLM:RATE at what the link can carry
fbcba12 firmware: DRV:CFG:APPLY changes STEP_MODE through the Hi-Z-safe path
8ceaddc setup: fresh-clone bring-up for Windows and Linux with a queue for admin items
baca894 client: motion behaviour scenarios (motion_scale, step_mode_chip, telemetry_load)
7dd7d0d client: match late replies by their JSON command name
febbb2e scripts/: subfolders by purpose; dead client launchers replaced
27b1164 bench: Phase 4 Step 0 baseline, serial part
```

## 6. Suggested diagrams

1. Layer stack with the harness connectors and the four tasks (section 2.1), marking the
   files touched today in a second colour.
2. Motor-task message bus: `MotorCmdType` producers and the two new consumers
   (SetStepMode via APPLY, StartAt via SYNC:START:AT).
3. Sector 7 journal layout and the save decision tree (E).
4. Comms/Motor/Encoder task timeline for the telemetry floor (B).
5. Sequence diagram for SYNC:START:AT before and after (F).
6. Client request/reply pipeline with the two correlation traps (G).
7. Test pyramid: host tests, fake board, virtual Nucleo, bench scenarios, and which
   change each level catches.
