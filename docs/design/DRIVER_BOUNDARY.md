# Design: a swappable motor driver (H_L3_L4 in physical units)

Status: **proposal, awaiting go-ahead** (2026-10-19). No code changes yet.

## Goal

Swap the stepper driver chip (powerSTEP01 today; a Trinamic TMC5160/TMC2209, an L6470, a
plain STEP/DIR driver tomorrow) by writing a new L4 driver, with **no edits in L3, L2 or the
client**. The motor-driver harness signal speaks physical quantities; everything a specific
chip needs (register map, units, status bits, quirks) lives behind it in L4.

## Today: powerSTEP01 knowledge above L4

| Knowledge | Where (outside L4) |
|---|---|
| Register addresses (`Drivers::MotorReg::*`, ~60 uses: KVAL/TVAL, ACC/DEC, MAX_SPEED, STEP_MODE, CONFIG, OCD_TH, STALL_TH, T_FAST, GATECFG1, ABS_POS, SPEED) | `L3 motor_controller.cpp`, `L3 telemetry_store.cpp` |
| STATUS bit layout (`MotorStatus` bit accessors, raw `statusReg` decoded by hand: bit 13 OCD, bit 9 UVLO, bits 11-12 thermal, 14-15 stall) | L3: motor_controller, motor_event_service, safety_service, comms_service, debug_service, telemetry_store, estop_screen; L2: event_codec, system_handlers |
| Chip units (SPEED steps/tick x 2^28, MAX_SPEED x 15.2588, ACC x 14.55, OCD 375 mA steps, TVAL 78 mA steps, KVAL duty) | L3 config_service (conversions), motion_service, motor_controller, supervisor, speed trim, sysid, LCD config screen; L2 command_parser_internal |
| Chip behaviour (STEP_MODE writable only in Hi-Z; current mode caps at 1/16; CM_VM bit; gate drive and OC_SD only set by init; reset detection via GATECFG1) | L3 motor_controller, motor_service, motor_config |
| Persisted config = chip register values (V4 blob: raw ACC/DEC/MAX_SPEED, KVAL/TVAL units, OCD steps, STEP_MODE code) | L3 motor_config |
| Host protocol in chip units (`DRV:CFG:TVAL 04 11 14 14`, `KVAL`, `OCD`, raw step-mode codes; telemetry `status_reg`) | L2 handlers, client `smc_core` (commands, motors profiles, GUI driver panel) |

## Target: the motor-driver signal

`Drivers::IMotorDriver` is replaced by a chip-agnostic `Drivers::IStepperDriver` (same
harness, H_L3_L4; same E-STOP guarantees). Units: positions in **microsteps** at the
resolution in force, speeds in **full steps/s**, accelerations in **full steps/s^2**,
currents in **amps** (floats are fine: Cortex-M4F, never in ISRs).

```cpp
struct DriverCaps {             // what this chip can do (L3 adapts, the GUI greys out)
    uint16_t maxMicrosteps;     // 128 (powerSTEP voltage mode), 16 in current mode, 256 (TMC)...
    bool currentControl, voltageControl, stallDetect, standbyPin, hardwarePositioning;
    float maxCurrentA, maxSpeedFsps;
};
struct DriverSettings {         // physical, persisted as-is
    uint16_t microsteps;        // requested; driver reports the effective one
    float holdA, runA, accA, decA;
    float accelFsps2, decelFsps2, maxSpeedFsps;
    float overcurrentA, stallThreshold01;   // 0..1 sensitivity, chip maps it
    DriveMode mode;             // CURRENT / VOLTAGE (if caps allow)
};
struct DriverState {            // decoded by the driver, never raw bits upstream
    bool outputsEnabled, busy, forward;
    bool overcurrent, undervoltage, thermalWarning, thermalShutdown, stalled, commandError;
    bool wasReset;              // the chip lost its settings (supply drop) since last read
    int32_t positionUsteps; float speedFsps;
    uint32_t rawStatus;         // opaque, diagnostics only (logs, DBG)
};
class IStepperDriver {
    init(); caps(); apply(const DriverSettings&) -> effective settings (handles Hi-Z-only writes itself)
    run(dir, fsps); move(usteps, maxFsps); goTo(usteps, maxFsps); softStop(); hardStop();
    disableOutputs(soft|hard); resetPosition(); home()/mark() (if caps allow)
    readState(DriverState&);   // one call per poll
    setStandby(bool);          // ISR-safe, unchanged (out-of-band E-STOP)
};
```

Raw register access stays only on the bring-up path (`IDebugAccess`, `DBG:*`), never in logic.

## What moves where

- **Into the powerSTEP01 driver (L4):** register map, unit conversions (config_service's
  raw<->physical), STATUS decoding, current-mode 1/16 cap, Hi-Z-only STEP_MODE/CONFIG writes
  (`setStepModeSafe`, the APPLY retry), init-only settings and the reset watch (GATECFG1),
  KVAL/TVAL selection by drive mode.
- **L3 keeps:** motion logic, closed loop, safety latch and E-STOP flow, jobs, telemetry,
  UI; all in physical units and `DriverState` flags.
- **L2 / protocol:** commands in physical units (`DRV:CFG:CURRENT 0.31 1.33 1.56 1.56`,
  `DRV:CFG:ACCEL <fsps2>`, `DRV:CFG:USTEP <n>`, `DRV:CAPS?`); telemetry flags (already
  sent) plus `driver_raw` for diagnostics. Old chip-unit commands kept one release as
  aliases routed to the debug path, then removed.
- **Persistence:** config blob V5 stores `DriverSettings`; V4 blobs migrate once (the
  powerSTEP conversions run in the migration, inside L4's helper).
- **Client:** motor profiles already store amps; the GUI driver panel and `smc_core`
  switch to the physical commands and `DRV:CAPS?`.

## Migration (each step builds, all tests green, bench-checked)

1. **State:** `DriverState` decoded in L4; L3/L2 read flags instead of bits (no behaviour change).
2. **Units:** move conversions into the driver; motion/supervisor/sysid/LCD speak physical units.
3. **Settings:** `apply(DriverSettings)`; `MotorReg` leaves L3; Hi-Z handling and the reset
   watch move into the driver.
4. **Persistence + protocol:** blob V5 with migration; physical commands, `DRV:CAPS?`; client.
5. **Proof of swap:** a second, non-powerSTEP driver (a generic STEP/DIR fake in the virtual
  Nucleo) passes the same scenarios with no L3/L2 change.

## Risks

- E-STOP: the out-of-band stop (STBY pin from the USART ISR) and `hardHiZ` must stay
  ISR-safe and as fast as today (microseconds); covered by the existing estop scenarios.
- Rounding: physical round-trips must not drift saved values (store physical, convert once).
- Protocol users (GUI, Pi): keep aliases for a release; bump protocol to 3.

## Questions for the owner

1. Which chips matter next (TMC5160 SPI, TMC2209 UART, STEP/DIR)? It decides the caps set.
2. Keep voltage mode (KVAL) at all, or current mode only?
3. Units: full steps for speed/accel and microsteps for position (proposed), or revolutions?
4. Protocol: keep the old chip-unit commands for one release (proposed) or break now?
