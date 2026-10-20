# Changelog: client

The host client in `client/`: the shared core (`smc_core`), the GUI (`smc_gui`) and the CLI
(`smc_cli`). It has its own version, independent of the firmware's (`CHANGELOG.md` at the
repository root); a client and a board work together when the client knows the board's
protocol number (`PROTOCOL_VERSION`). The patch is counted from git: the commits that touched
`client/` (its tests excepted) since the tag `client-vX.Y.0`. `smc --version` and `smc info`
report it. Rules and tooling: docs/VERSIONING.md (`scripts/release/release.py minor client`).

Releases up to 0.5.0 were shared with the firmware: see the root CHANGELOG.md.

## [Unreleased]

### Added
- Fleet test mode: test the base with the controllers you have (1 to 4, in any wheel slots).
  Slots set to "virtual" get a virtual Nucleo; the base opens and the controller drives it, so
  each real motor turns as its wheel would. A port the main window holds is released first;
  Close ends test mode and stops the virtual boards.
- Base driving with a wired Xbox controller (tested with the PowerA 1414135-02): the left stick
  moves the base (vx/vy), the triggers turn it (LT counter-clockwise, RT clockwise, proportional);
  `BaseMap.turn = "right_stick"` brings back the right-stick turn.

### Changed
- The 23HS22-2804-ME1K motor profile carries the firmware's new defaults: run 1.01 A, acc/dec
  1.17 A (were 1.33 / 1.56 A) and the overcurrent guard at 78 A, so applying the profile no
  longer restores the old currents.

## [0.6.0] - 2026-10-20

### Added
- Client: typed builders for the new commands, `smc_core.driver` (one physical code path for
  protocol 2 and 3 boards), views for `DRV:CAPS?` / `DRV:CFG:PHYS?`, `smc driver caps|show`;
  the fake board speaks protocol 3 (or 2 with `protocol=2`).
- Separate client version (docs/VERSIONING.md): `smc_core.version.full_version()` counts the
  client commits since `client-vX.Y.0`; `smc --version` and `smc info` report it.
- The virtual-Nucleo tests run every scenario on both motor drivers (powerSTEP01 and the
  STEP/DIR driver, `vnucleo.boards(driver=...)`).

### Changed
- Protocol 3 boards are configured in physical units (protocol 2 boards still work): motor
  profiles (amps, microsteps), the GUI's driver, step-mode and protection panels (ranges and
  steps from `DRV:CAPS?`, currents in amps, overcurrent in amps with the sensed volts,
  voltage-mode controls hidden when the driver has none), scenarios and the CLI.
- GUI Motor tab: accel/decel in RPM/s and max speed in RPM, and the speed-profile plot in RPM.
- No more firmware/client "release mismatch" warning: the two versions are independent; the
  protocol number decides compatibility (a board on an unknown protocol is refused).

### Fixed
- Client scenarios no longer assume a powerSTEP01: step_mode_readback and step_mode_chip ask
  only for resolutions the driver takes (a finer one must be refused), drive_mode_chip checks
  that a missing drive mode is refused, position_capture compares with the RUN speed when the
  driver has no step-sync output (it read `pulses_per_full_step` 0 as 8).
