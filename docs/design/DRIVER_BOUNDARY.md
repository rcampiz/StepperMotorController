# Design: a swappable motor driver (H_L3_L4 in physical units)

Status: **approved 2026-10-19, in progress**: steps 1-4 done (state, motion units, settings,
protocol 3 and the client; see "Step 3 as built" and "Step 4 as built"); step 5 (a second driver)
is next.

Decisions (owner, 2026-10-19): the next chip is unknown, so nothing may assume one: the
boundary is capability-driven (`DriverCaps`), and every chip-specific fact lives in its L4
driver. Voltage mode stays, as a capability. Units: full steps/s and full steps/s^2 for
speed and acceleration, microsteps for position. Old chip-unit commands stay one release,
routed through the driver, then go.

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

## Step 3 as built (2026-10-19)

- **Types** (`server/harness/pins/driver_settings.hpp`, shared by L2/L3/L4): `DriverCaps`
  (drive modes and their finest resolution, stall detect, standby pin, hardware positioning,
  sync pulses per full step, reportable/alarmable faults, current/duty/overcurrent/stall
  ranges and steps, speed and speed-ceiling limits, acceleration range, position range),
  `DriverSettings` (microsteps asked for, drive mode, fault alarms as `DriverFault` bits,
  currents in amps and duties 0..1 kept side by side so switching mode restores the other,
  accel/decel in full steps/s^2, speed ceiling in full steps/s, overcurrent threshold in
  amps, stall threshold 0..1, and an opaque 12-byte `DriverTuning` block the driver owns),
  `ApplyResult`, `effectiveMicrosteps()`. `DriverSettings` is persisted as-is.
- **Overcurrent:** the powerSTEP01 senses OCD as a MOSFET drain-source *voltage*
  ((OCD_TH+1) x 31.25 mV). The setting is an estimated current = volts / RDS(on), with the
  IHM03A1's STL220N6F7 taken as 1.2 mOhm (an estimate, a constant in the L4 driver);
  `DriverCaps::overcurrentOhms` says so, and the LCD shows both ("0.69V ~573A").
- **Driver API** (`IMotorDriver`): `caps()`, `quantize()` (pure), `apply()` (writes everything
  the chip takes now; the Hi-Z-only STEP_MODE/CONFIG bits only when the outputs are already off,
  else `NEEDS_OUTPUTS_OFF`), `readSettings()`, `checkReset()` (the GATECFG1 watch, now in L4 and
  armed from boot), `diagnostics(view)` (the register dump as opaque text), `setAcceleration`,
  `setDeceleration`, `setMark`; `getParam`/`setParam` and `MotorReg` are gone from the boundary.
  L3 releases the outputs (HardHiZ, apply, SoftStop) when the driver asks for it.
- **powerSTEP01 engine:** `L4_drivers/devices/powerstep01_settings.hpp` (register image,
  conversions, apply, read-back, reset watch, diagnostics, protocol 2 units), templated over a
  register port so the driver, the virtual Nucleo's fake chip and the host fakes share it.
- **Protocol 2 aliases:** `LegacyDriverUnits` (L4 boundary) carries the old chip codes;
  `toLegacyUnits`/`fromLegacyUnits` translate. `DRV:CFG:KVAL/TVAL/OCD/STALL/MOTION/PRED/DECAY/FAULT`
  and `DRV:CFG?` read the physical settings as codes, edit, and write them back through the driver.
- **Persistence:** blob V5 (128 bytes, same journal) holds `DriverSettings` at offset 8 plus the
  non-driver fields. V1-V4 blobs are migrated at load via `fromLegacyUnits` (not rewritten until
  the next save, so a rollback still finds its V4 record); a driver without those units keeps
  its defaults and the non-driver fields.

## Step 4 as built (2026-10-19)

- **Protocol 3** (`PROTOCOL_VERSION` 3, docs/PROTOCOL.md section 7). New commands, in
  `L2_protocol/handlers/driver_settings_handlers.cpp`: `DRV:CAPS?` (every `DriverCaps` field, units
  in the key names, fault lists by chip-neutral name, `overcurrent_sense` voltage/current);
  `DRV:CFG:CURRENT/DUTY/USTEP/ACCEL/DECEL/MAXSPD/OVERCURRENT/STALL_TH/ALARMS` and their `?`
  queries (same handler); `DRV:CFG:PHYS?`; `DRV:USTEP` / `DRV:USTEP?` (now, through the Hi-Z safe
  step-mode path, replacing `DRV:STEP_MODE`). `OCD` and `STALL` kept their protocol 2 meaning
  (codes), so the physical ones are named `OVERCURRENT` and `STALL_TH`.
- **Semantics:** the setters edit the stored settings (RAM) like every `DRV:CFG` setter;
  `DRV:CFG:APPLY` puts them in force and saves. Replies show `quantize()`d values, i.e. what
  APPLY writes (bench values: 0.312/1.326/1.56/1.56 A, 1/128 asked = 1/16 in force, ACC/DEC 344 =
  5005.89 fsps2, MAX_SPEED 87 = 1327.51 fsps, OCD_TH 21 = 0.6875 V ~ 572.917 A). Ranges:
  `Harness::*WithinCaps` (half a step of slack) in `driver_settings.hpp`, used by L2 (error text
  with the range) and by L3 (`IMotorConfigDispatcher::configSetDriverSettings` checks the
  changed fields only). L3 gained `driverSettings()`, `driverSettingsInForce()`,
  `configSetDriverSettings()`.
- **Replies without printf floats:** `L2_protocol/kv_reply.hpp/.cpp` streams named values to the
  transport (JSON object, one `OK <WHAT> k=v` line, or `k=v` lines + `OK <n>`), with
  `formatReal` (6 significant digits) and `formatFixed`. Release text +14.2 KB, data/bss
  unchanged.
- **Telemetry:** `motor.faults` (DriverFault bits), `motor.driver_raw`, `control.base_fsps` /
  `final_fsps` (`trim_fsps` only in `CTRL:TRIM?`: the worst-case status line stays at 1200 of
  1280 bytes, so the rate floor is unchanged). Every protocol 2 field stays.
- **Deprecation:** a `// @deprecated <replacement> (protocol 3; kept one release)` line in a
  handler body; `gen_protocol.py` carries it into PROTOCOL.md, protocol.json, `SYST:HELP?` and
  the client's `CommandSpec.deprecated`. Deprecated: KVAL, TVAL, OCD, STALL, MOTION, STEPMODE,
  PRED, DECAY (chip tuning, no replacement), the FAULT flags, `DRV:CFG?`, `DRV:STEP_MODE[?]`.
- **Client:** `smc_core.driver` is the one physical code path: `DriverAccess` (caps, settings,
  resolution, change sets) sends the physical commands to a protocol 3 board and the chip codes
  to a protocol 2 one (the powerSTEP01 translation and caps live there and go with the aliases).
  Board accepts protocols 2 and 3. Motor profiles store microsteps / amps (older step_mode / OCD
  codes still load). GUI driver, step-mode and protection panels take their ranges from
  `DRV:CAPS?` (a refusal = protocol 2 fallback); the fake board speaks protocol 3 or 2.
- **Not changed:** the LCD Driver page (it already edits physical settings in L3).
