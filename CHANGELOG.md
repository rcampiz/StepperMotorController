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
- Harness meter: always-on crossing counters for every harness signal.
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
