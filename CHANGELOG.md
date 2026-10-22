# Changelog: firmware

The firmware (and the project as a whole): the STM32 firmware in `server/`, its build and the
virtual Nucleo. The client (GUI, CLI, core) has its own version and changelog,
`client/CHANGELOG.md`. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
the version follows [Semantic Versioning](https://semver.org/) with the patch counted from git:
the commits that touched the firmware since its release tag (`0.6.3` = three firmware commits
after `fw-v0.6.0`). The firmware reports it in `*IDN?`, `!RESET fw=`, `SYST:VER?` and on the LCD
saver. The wire protocol has its own number (`PROTOCOL_VERSION`, docs/PROTOCOL.md), which is
what decides whether a client and a board work together.

Rules and tooling: docs/VERSIONING.md. Notes go under Unreleased as work lands;
`scripts/release/release.py minor fw` (or `major fw`) stamps them, bumps `VERSION`, commits and
tags.

## [Unreleased]

### Added
- `encoder.rip` in `DIAG:STAT?` and both telemetry pushes: the resonance as a number. The
  Encoder task measures each DMA sample's position against the mean of the 33 samples centred on
  it (a straight run cancels), rms over ~100 ms, in 0.1 counts: ~1 running quietly, ~5 in the
  bench motor's resonance without the avoider, ~2 with it (bench 2026-10-21, 80 RPM). About
  0.2 % CPU, 132 bytes. 50 Hz telemetry can show the 100-150 Hz resonance this way.
- `DRV:CFG:SCALE <25-200 %>` / `DRV:CFG:SCALE?`: one number for every current level. Hold, run,
  accel and decel (the duties in voltage mode) go in force x the scale at `DRV:CFG:APPLY`; the
  stored levels stay as given (the LCD and the GUI edit those); `DRV:CFG:PHYS?` adds
  `current_scale_pct` and the `applied_*_a` levels. The resonance avoider follows the scaled run
  current (bench: 130 % moved the main band 82.8 -> 94.7 RPM by itself). Stored in the config
  record's last spare byte.
- The resonance avoider (`CTRL:RES?`, `CTRL:RES:CFG`, `CTRL:RES:SAVE`; stored in the config
  record's spare bytes, off in older records): the mechanical resonance follows the run current
  (bench f_n = 137 Hz x I^0.51), so the Motor task moves the run current live (TVAL_RUN alone,
  `IMotorDriver::setRunCurrentLive`, no flash write) to keep it off the speed in use. Least
  sacrifice first: the smallest raise (up to +40 %), else the smallest cut (up to -30 %; only
  at steady speed, so a joystick never gets one while it changes the speed, and only with an
  encoder: dropped when the steady lag grows by 0.35 step), else unchanged; back to the run
  current with hysteresis once clear. Bench 2026-10-21 (docs/bench/resonance_avoider_2026-10-21.png):
  worst ripple 7.0 -> 2.5 counts over 40-120 RPM; a 64 s joystick-like session, 63 current
  changes, no guard trip, no lost step.
- A tracking observer for the encoder velocity (`CTRL:ENC:FILT:OBS <0|1> [bandwidth 1-200 Hz]`,
  `observer_enabled` / `observer_bw_hz` in `CTRL:ENC:FILT?`, saved with the chain): a
  critically damped second-order loop on every DMA sample, whose velocity replaces the
  measurement window's count difference as the chain's input. The Encoder task now gives each
  sample of a batch its own instant (1 / rate apart). Bench 2026-10-21 against the true
  velocity: 10 Hz 32 ms lag / 0.39 RPM noise (the 10 Hz Butterworth: 38 ms / 0.42), 20 Hz
  14 ms / 1.1 RPM, 50 Hz 8 ms / 6.0 RPM; the 4000-count encoder sets the noise. Off by default.
- `motor.enc_at_pos` in `DIAG:STAT?` and both telemetry pushes: the encoder count read at the
  same instant as `motor.position` (`IEncoder::countNow()`: the last sampled count plus the
  hardware counter's movement since). `encoder.count` is the Encoder task's own, up to 10 ms
  old, and the position is polled every 50 ms, so slip computed from the two swung by tens of
  degrees while turning (bench 2026-10-21, 150 RPM: -22..+41 degrees); from the pair it is
  -1.6..+0.6 degrees, under one full step (docs/bench/slip_paired_2026-10-21.png). The control
  loop's following error uses the same pair.

### Fixed
- `IEncoder::countNow()` stays live through a capture readout (it returned the frozen count, so
  the paired lag jumped while the rings were read).
- The release image boots: the startup code ran the C++ static constructors before
  `SystemInit`, which enables the FPU, so any of them touching a float register faulted before
  the UART was up (the optimised build did; so did the observer's first, debug, version).
  `SystemInit` now runs first, as in ST's startup. Bench 2026-10-21: the release image with the
  old order prints nothing, with the new one boots and answers.
- `CTRL:ENC:FILT:SAVE` keeps the whole filter chain: the Butterworth cutoff, the Pade gain and
  limit, the notch centre and Q and the Holt factors are stored too (in the config record's
  spare bytes; a record saved before reads them as 0, each stage's default, as boot always
  applied). Before, only the flags, EMA alpha, SMA window, window and rate were kept, so a
  20 Hz cutoff came back as 10 Hz after a restart (bench 2026-10-21: 20 Hz now survives one).

### Changed
- LCD saver: with the title, a pixel-art stepper hovers over the scene: the bench's StepperOnline
  23HS22-2804-ME1K from behind (light caps, black body, the encoder's raised cover on the rear
  cap with its green / brown / yellow wires through their clip), drawn after the owner's CAD
  renders, 78 x 87 at the panel's resolution, cyan and hot-pink rim lights. It bobs on the clock
  (Hotline Miami's hover, +-3 px over 1.6 s) with a shadow on the floor that narrows as it rises,
  fades with the title, and repaints on the frames the title leaves. scripts/lcd/gen_motor_sprite.py
  makes the sprite; scripts/lcd/step_sprite.py renders one from a STEP file instead (cascadio,
  trimesh) for when the manufacturer's model is to hand.
- LCD saver: the bands follow the clock (IScreen::setNowMs, from the ScreenManager) at 1.5x the
  indicator's pace, so they keep their speed while the title slows the frames; the title's box
  repaints every other frame again (every frame held the whole saver to ~11 frames/s; now 13-15
  with the title, 20 without); at the bottom, on a dark plate, "SCREEN SAVER" / "PRESS ANY
  BUTTON TO EXIT". The E-STOP page's bands follow the clock too.
- LCD: the VHS tracking bands (`graphics/vhs_bands/vhs_bands.hpp`, shared) roll over the
  synthwave saver (the whole scene, title included) and the E-STOP / FAULT page (the hazard
  stripes, the grid, the page and behind the title's letters; never over the status panel, the
  prompt, the clear bar or a message). Each screen composes the rows the bands left and reached.
  The saver's title comes up 1 s into a scene and fades in over 0.4 s (was 5 s and 0.8 s).
- LCD saver, faster (bench, synthwave): 20 frames/s without the title (was ~9) and 10-11 with it
  (was ~5, the title moving every other frame). The sky is composed a row at a time by runs (its
  colour, the sun's span, the scene a pixel only where mountains or buildings reach the row), the
  title stepped along the row and repainted every frame, the floor only on the rows a grid line
  moved through (it was the whole floor, 40-80 KB a frame), rows streamed four a call
  (`ScratchRow::ROWS`). The pixel composers (saver, E-STOP page, motion indicator) build -O2 in
  every build: at -Og the saver's title box cost ~3.4 us a pixel.
- LCD motion indicator: holding CENTER fills the "hold CENTER 2s: menu" plate from the left in
  hot pink (the 2 s to the menu), so the press is seen to register; letting go clears it.
  `IIndicatorRenderer::setHoldProgress()`, fed by the DisplayService's long-press timer.
- LCD motion indicator: a VHS / synthwave look. A violet backdrop with scanlines and speckle,
  and two tracking bands (cyan and pink bars, a bright line, a tear and noise that re-roll)
  rolling down behind the dial; hot-pink rings with a yellow inner line, yellow chevrons, a white
  arrow with a cyan / red split that fills with the speed; the readout and the "hold CENTER 2s"
  hint in the 8 x 12 font on dark plates (the 6 x 8 hint was hard to read on the panel). One row
  composer draws every pixel; a tick repaints only what changed plus the bands' rows (~5k px),
  the bands without the tearing-edge wait, every tick (24 and 36 px/s). Bench, arrow sweeping:
  18.2 frames/s with the bands (16.0 before, with the driver change below); arrow still: the
  bands take 16-20 % of the Display task's time (lowest priority).
- LCD driver: display inversion off, pixels sent as they are. It was INVON (0x21) with every
  pixel inverted in software to cancel it: the same picture, but each streamed byte copied into
  a 512-byte buffer and flipped on the CPU before its DMA. Streams, columns and raw bitmaps now
  go out by DMA straight from the caller's buffer: 0.9 ms a streamed block instead of 2.1 ms,
  every screen draws faster and SPI1 (shared with the powerSTEP01) is held less. The host
  panel simulator models INVON / INVOFF, so a path sent with the wrong polarity fails the
  screen tests.
- LCD motion indicator: the rotation chevrons span all three rings (22 px base centred on the
  ring band, 20 px long; they were 14 px on the outer ring alone and looked tiny).
- The default encoder velocity filter is the tracking observer alone at 10 Hz (32 ms lag,
  0.39 RPM noise against the 10 Hz Butterworth's 38 ms / 0.42 RPM; bench 2026-10-21). A board
  with a saved chain keeps it (`CTRL:ENC:FILT:RESET` + `CTRL:ENC:FILT:SAVE` moves it over).
- The Motor task reads the driver every 5 ms (was 50): position, speed, faults and the encoder
  count paired with them reach telemetry and the fault / stall / motion-done edges 10x sooner;
  slip carries a new value in 96 % of 20 ms telemetry samples (40 % before). The control laws
  (sysid, speed trim, following supervisor) keep their 50 ms clock: their gains, slew limits
  and the sysid time base are per 50 ms tick. The task waits only for the rest of its period
  (a whole period after each pass made it period + pass).
- The powerSTEP01 status poll holds the shared SPI1 bus once for its three exchanges (it took
  it three times, each a possible wait for a ~2.4 ms LCD chunk and a bus mode switch).
- The LCD starts its next frame at once after a late one (it slept a whole 50 ms period after
  it: every frame over 50 ms took 100). Bench 2026-10-21, motor at 150 RPM and the arrow
  sweeping: 17.7 frames/s with the 5 ms poll, 14.1 before at 50 ms.
- The default encoder velocity filter is one 10 Hz Butterworth low-pass on the 40 ms window
  (config defaults, `CTRL:ENC:FILT:RESET`): 36 ms of lag instead of 208 ms for 0.5 RPM of
  steady-state noise instead of 0.1 (bench tune 2026-10-21, against the true velocity from the
  unfiltered count; docs/bench/encoder_filter_tune_2026-10-21.png). A board with a saved filter
  keeps it; `CTRL:ENC:FILT:RESET` then `CTRL:ENC:FILT:SAVE` moves it to the new default.
- The serial link runs clean up to 2 Mbaud: the USART2 receive ISR runs at NVIC 1, above the
  FreeRTOS critical sections and the SEGGER RTT lock, and defers its one RTOS call (the Comms
  task's wake) to a spare vector at NVIC 6. At NVIC 6 it lost bytes received back to back
  from 921600 up (blamed on the ST-LINK before). `SYST:BAUD` adds 1000000 and 2000000 (exact
  dividers). The out-of-band ESTOP byte is handled in the same ISR as before.
- `UI:ESTOP:HOLD <pct>`: the host's clear hold fills the E-STOP page's bar, as the board's own
  CENTER hold does (a pad's START held for 3 s); the bar empties when the updates stop.
- The LCD arrow appears pointing where the base goes: from a stop it swung round from up (the
  stopped base's angle); later turns still ease.
- The LCD motion indicator keeps up: it eases on the clock (the same speed at any frame rate;
  it eased per frame and lagged at the ~5 frames/s a moving arrow got) and a repaint is one
  window streamed ~20 rows per call (was a window per span, ~380 transfers a frame). Bench, debug
  image: 148 ms -> 47 ms a frame, 4.9 -> 14.7 frames/s while the arrow moves. The lit rings and
  chevrons no longer lag a frame behind.

## [0.6.0] - 2026-10-20

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
- Driver boundary step 5, the proof of swap: a second L4 motor driver for a generic STEP/DIR
  chip (DRV8825-class: MODE pins, nENABLE, nFAULT, current from VREF) with software ramps and
  position count over a new `Board::IStepPulseTimer` (H_L4_L5). The virtual Nucleo runs it with
  `--driver stepdir` (`sim/vnucleo_fleet.py --driver stepdir`); the client's virtual-Nucleo
  tests run every scenario on both drivers. No L3 or L2 change; the firmware still builds the
  powerSTEP01 driver only. Findings in docs/design/DRIVER_BOUNDARY.md ("Step 5 as built").
- Separate firmware and client versions (docs/VERSIONING.md): `VERSION` is the firmware's
  release; the patch is the count of firmware commits since `fw-vX.Y.0`, computed at build time
  (`Harness::fwVersion()`). `scripts/release/release.py`: `status`, `check` (a release is due
  when the protocol version, the stored config format or the command set changed),
  `minor|major fw|client` (bump, changelog, commit, tag) and `which fw|client <version>`.
- Open source: MIT License, THIRD_PARTY_NOTICES.md. The LCD fonts are rendered from Cascadia
  Mono (SIL OFL 1.1; files and licence in scripts/lcd/fonts), replacing Consolas, which is not
  licensed for redistribution.
- LCD: per-row help text on the Driver, Motion, Screen Care and Device Info pages; the Driver
  page shows accel/decel in RPM/s and max speed in RPM (shaft, independent of microstepping).

### Deprecated
- Protocol 2 chip-unit commands, kept one release (marked in docs/PROTOCOL.md and
  `SYST:HELP?`): `DRV:CFG:KVAL`, `TVAL`, `OCD`, `STALL`, `MOTION`, `STEPMODE`, `PRED`, `DECAY`, the
  flag form of `DRV:CFG:FAULT`, `DRV:CFG?`, `DRV:STEP_MODE`, `DRV:STEP_MODE?`; the status fields
  `status_reg`, `stall_a` / `stall_b`, `base_spd` / `trim_spd` / `final_spd`.

### Changed
- Defaults: motion currents 25 % lower (run 1.014 A, acc/dec 1.17 A; hold 0.312 A kept) and
  the overcurrent guard at 78 A (OCD_TH 2), the lowest that stayed clean in every bench soak
  (26 A flags on switching transients while running).
- `PROTOCOL_VERSION` 3. `DRV:CFG:DRIVE` refuses a drive mode the driver lacks.
- Driver boundary step 3: the motor driver's settings are physical (amps, duty, full steps/s^2,
  full steps/s, microsteps) and the driver advertises its capabilities; every powerSTEP01
  register, unit and quirk (Hi-Z-only writes, the current-mode 1/16 cap, the chip-reset watch)
  lives in the L4 driver. Config blob V5; V1-V4 blobs migrate at boot to the same currents,
  ramps and step mode. Protocol 2 commands unchanged (the chip-unit ones are aliases the
  driver translates). The LCD Driver page shows physical values; OCD reads as the VDS
  threshold with its estimated current. The chip-reset watch now runs from boot.
- RAM: static use 88.9 % -> 80.5 % of 96 KB (the Display renderers share one scratch row and
  one title mask, Motor task stack 1024 -> 512 words, SystemView RTT buffer 4 -> 1 KB); main
  stack reserve 1 -> 2 KB; Display task stack 640 -> 1024 words (measured peak 502). Task stack
  sizes live in F_platform/tasks/task_stacks.hpp.
- Portability (a later RP2350 or other MCU): the trace ring claims slots with one atomic add
  instead of masking interrupts; the config journal's flash region comes from the board
  (`IInternalFlash::configRegion()`), so L4 holds no flash addresses.

### Fixed
- On a driver without powerSTEP01 chip units, the protocol 2 aliases (`DRV:CFG:KVAL`, `TVAL`,
  `OCD`, `STALL`, `MOTION`, `PRED`, `DECAY`) are refused with `INVALID_STATE` naming the
  protocol 3 command; they answered "set" and changed nothing.
- `DRV:CAPS?` gains `current_settable`: a driver whose current is set in hardware (a STEP/DIR
  chip's VREF pot) reports `current_mode=1 current_settable=0` instead of no current mode;
  `DRV:CFG:CURRENT` says the current is set in hardware, the LCD Driver page shows "hardware".
- `*IDN?` and the `!RESET` line had 96-byte buffers that a long version string would truncate
  into invalid JSON (now 128).

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
