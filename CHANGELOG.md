# Changelog

All notable changes to the firmware and the client. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the release number follows
[Semantic Versioning](https://semver.org/). `VERSION` holds the current release; the firmware
(`Harness::FW_VERSION`, `*IDN?`, `!RESET fw=`, `SYST:VER?`, the LCD saver) and the client
(`smc_core.__version__`, `smc --version`) both report it. The wire protocol has its own
number (`PROTOCOL_VERSION`, docs/PROTOCOL.md) and changes only when the protocol does.

Cutting a release: move the Unreleased notes under a new heading, bump `VERSION`,
`client/pyproject.toml` and `client/smc_core/version.py`, commit, then tag `vX.Y.Z`.

## [Unreleased]

### Added
- Protocol 3 (driver boundary step 4): the host configures the motor driver in physical units
  and learns what it can do. `DRV:CAPS?` (modes and resolutions, current / duty / overcurrent /
  stall ranges and steps, speed and acceleration limits, faults); `DRV:CFG:CURRENT` (amps),
  `DUTY` (0..1), `USTEP` (microsteps), `ACCEL` / `DECEL` (full steps/s^2), `MAXSPD` (full steps/s),
  `OVERCURRENT` (amps, plus the volts a voltage-sensing driver compares), `STALL_TH` (0..1),
  `ALARMS` (fault names), each with a `?` query; `DRV:CFG:PHYS?`; `DRV:USTEP` / `DRV:USTEP?`.
  Values are rounded by the driver and the replies show what `DRV:CFG:APPLY` puts in force;
  ranges come from the driver (`ERROR OUT_OF_RANGE`). Telemetry: `motor.faults` (chip-neutral
  bits), `motor.driver_raw`, `control.base_fsps` / `final_fsps` (`CTRL:TRIM?` also `trim_fsps`).
- Client: typed builders for the new commands, `smc_core.driver` (one physical code path for
  protocol 2 and 3 boards), views for `DRV:CAPS?` / `DRV:CFG:PHYS?`, `smc driver caps|show`;
  the fake board speaks protocol 3 (or 2 with `protocol=2`).
- Driver boundary step 5, the proof of swap: a second L4 motor driver for a generic STEP/DIR
  chip (DRV8825-class: MODE pins, nENABLE, nFAULT, current from VREF) with software ramps and
  position count over a new `Board::IStepPulseTimer` (H_L4_L5). The virtual Nucleo runs it with
  `--driver stepdir` (`sim/vnucleo_fleet.py --driver stepdir`); the client's virtual-Nucleo
  tests run every scenario on both drivers. No L3 or L2 change; the firmware still builds the
  powerSTEP01 driver only. Findings in docs/design/DRIVER_BOUNDARY.md ("Step 5 as built").

### Deprecated
- Protocol 2 chip-unit commands, kept one release (marked in docs/PROTOCOL.md and
  `SYST:HELP?`): `DRV:CFG:KVAL`, `TVAL`, `OCD`, `STALL`, `MOTION`, `STEPMODE`, `PRED`, `DECAY`, the
  flag form of `DRV:CFG:FAULT`, `DRV:CFG?`, `DRV:STEP_MODE`, `DRV:STEP_MODE?`; the status fields
  `status_reg`, `stall_a` / `stall_b`, `base_spd` / `trim_spd` / `final_spd`.

### Changed
- `PROTOCOL_VERSION` 3. The client accepts protocol 2 and 3 boards and uses the physical
  commands when the board has them: motor profiles (amps, microsteps), the GUI's driver,
  step-mode and protection panels (ranges and steps from `DRV:CAPS?`, currents in amps,
  overcurrent in amps with the sensed volts, voltage-mode controls hidden when the driver has
  none), scenarios and the CLI. `DRV:CFG:DRIVE` refuses a drive mode the driver lacks.
- Driver boundary step 3: the motor driver's settings are physical (amps, duty, full steps/s^2,
  full steps/s, microsteps) and the driver advertises its capabilities; every powerSTEP01
  register, unit and quirk (Hi-Z-only writes, the current-mode 1/16 cap, the chip-reset watch)
  lives in the L4 driver. Config blob V5; V1-V4 blobs migrate at boot to the same currents,
  ramps and step mode. Protocol 2 commands unchanged (the chip-unit ones are aliases the
  driver translates). The LCD Driver page shows physical values; OCD reads as the VDS
  threshold with its estimated current. The chip-reset watch now runs from boot.

### Fixed
- Client scenarios no longer assume a powerSTEP01: step_mode_readback and step_mode_chip ask
  only for resolutions the driver takes (a finer one must be refused), drive_mode_chip checks
  that a missing drive mode is refused, position_capture compares with the RUN speed when the
  driver has no step-sync output (it read `pulses_per_full_step` 0 as 8).
- On a driver without powerSTEP01 chip units, the protocol 2 aliases (`DRV:CFG:KVAL`, `TVAL`,
  `OCD`, `STALL`, `MOTION`, `PRED`, `DECAY`) are refused with `INVALID_STATE` naming the
  protocol 3 command; they answered "set" and changed nothing.
- `DRV:CAPS?` gains `current_settable`: a driver whose current is set in hardware (a STEP/DIR
  chip's VREF pot) reports `current_mode=1 current_settable=0` instead of no current mode;
  `DRV:CFG:CURRENT` says the current is set in hardware, the LCD Driver page shows "hardware".

## [0.5.0] - 2026-10-19

### Added
- Versioning: one `VERSION` file read by every build; the build id (`git describe`, `+dirty`)
  is regenerated on every build; this changelog; the client warns when the board's release
  differs in major or minor, and `smc info` reports both.
- LCD: cyberpunk theme with a shared chrome on every screen; the selected row animates
  (Hotline Miami); encoder dial and velocity scope, telemetry graph, architecture, tasks,
  trace and dispatcher screens; Device Info sets the wheel; Motion in RPM with jogs; Driver
  edits currents and idle release.
- LCD screen care: synthwave screen saver (new default) with the rocking STEPPER MOTOR
  CONTROLLER title and the release under it, hotline and blank styles, retention recovery,
  panel sleep after 15 min of saver.
- LCD E-STOP: double-tap CENTER stops and is reported upstream; an E-STOP page with
  hold-to-clear.
- Harness meter: always-on crossing counters for every harness signal (`DIAG:HARNESS?`), shown
  live on the LCD Architecture screen (HARNESS view; the matrix shows tasks x harnesses).
- Memory page on the LCD and in the GUI (Diagnostics > Memory): flash, static RAM, the RAM
  split and every stack's peak (`DIAG:MEM?`).
- Motor: powerSTEP01 current mode (TVAL) with predictive current control as the default,
  current-regulation settings, ADC_OUT supply sense, gate driver programmed for the IHM03A1,
  idle release of the coils.
- Position capture: encoder and commanded step sampled together by DMA up to 10 kHz.
- Protocol: compact telemetry push (`SYST:TLM:FMT COMPACT`), out-of-band ESTOP byte 0x18,
  base64 upload payloads, reset cause in `!RESET rst=`.
- Client: `smc_core` shared core (transports, framing, codec, typed commands generated from
  PROTOCOL.md, scenarios and runner), the `smc` CLI (including `smc flash`), the GUI as a
  frontend on it (Fleet page for four boards, Encoder tab, dark/light mode, motor profiles as
  JSON, Xbox controller).
- Virtual Nucleo: the firmware's L2, L3 and task bodies on a desktop over TCP; multi-board
  groundwork; chassis cost tool.

### Changed
- Protocol 2 (protocol pass P2 to P10): machine-readable error codes, escaped JSON strings,
  JSON telemetry push at a host-set rate, terminated multi-line replies, `SYST:HELP?`
  generated from the dispatch tables, echo off in JSON mode.
- Config saves append to a flash journal instead of erasing the sector.
- Encoder velocity in counts per second.
- Default currents for the 23HS22-2804, then 25 % lower.
- Interrupt-driven UART transmit; `SYNC:START:AT` runs on the Motor task; SPI1 locked per
  chunk during remote bitmaps.
- The build id is taken at build time; it used to be taken at configure time and could name
  an older commit.

### Fixed
- A powerSTEP01 that reset behind the firmware's back is re-initialised.
- The watchdog can be disarmed only in Hi-Z, and uploads keep it fed.
- The configured MAX_SPEED is restored after a move's speed override.
- `DEV:ID` / `DEV:ROLE` no longer crash on an uninitialised identity store.
- `DRV:CFG:APPLY` changes STEP_MODE through the Hi-Z-safe path.
- `SYST:TLM:RATE` is floored at what the link can carry.
- Current mode runs at 1/16 microstep at most.
- Clearing an E-STOP from the LCD turns off a dead host's watchdog.

## [0.4.0] - 2026-10-06

First numbered firmware release, introduced with the `!RESET` line, the `*IDN?` handshake
and the version constants (protocol pass P1). Not tagged; earlier history is in `git log`.
