# Phase 4, Step 0: hardware baseline before the fast-ESTOP work

Status 2026-10-05: **blocked on hardware**. No ST-LINK is attached to the build machine
(OpenOCD `open failed`; the only serial ports are COM1 and two Bluetooth links). Everything
that can be prepared without the board is below: the datasheet facts the later steps need,
the exact measurement procedure, a scenario driver script, and the results table to fill in.

## 0. What to flash

Commit `c9a35e5` is the code baseline. Every commit after it on `master` is documentation
only, so the current `build/StepperMotorController.elf` (firmware-debug) is code-identical.
To be exact:

```powershell
git checkout c9a35e5
. scripts\env.ps1
cmake --preset firmware-debug ; cmake --build --preset firmware-debug
cmake --build --preset firmware-debug --target flash     # OpenOCD + ST-LINK
git checkout master
```

Debug preset is the one to measure: it is what has been built and host-tested, and it
includes the interface trace (RTT), which is the pessimistic case for latency.

## 1. Datasheet facts (powerSTEP01, DS9836 Rev 7)

Extracted from the datasheet text; these drive Steps 1 and 2.

| Item | Value | Where |
|---|---|---|
| STBY/RESET pin | pin 19, logic input, "Low logic level puts the device in standby mode and reset logic. If not used, it should be connected to VREG." | §Pin description |
| Internal pull | **pull-down, RPDRST = 450 kΩ to ground** ("STBY/RESET input is internally pulled down to ground") | §7.2 Logic I/O, electrical table |
| tSTBY,min | 0.5 ms minimum low time to guarantee the switch to standby | §Standby table |
| tlogicwu | 0.5 ms logic power-on / wake-up | §Standby table |
| tcpwu | 1 ms charge-pump power-on / wake-up (CFLY 10 nF, CBOOT 220 nF, VCC 15 V) | §Standby table |
| While low | all MOSFETs off (high impedance), charge pump stopped, SPI and control logic disabled, **registers reset to default, all protections disabled** | §7.13 |
| On exit | all MOSFETs off, HiZ flag high; "a delay must be given before applying a new command to allow proper oscillator and charge pump startup" | §7.13 |

Consequences already decided into Phase 4:

- Step 2's CLEAR sequence must wait **≥ 1.5 ms** (tlogicwu + tcpwu; use 2 ms) after releasing
  PA9 before the first SPI command, and must re-apply the whole configuration because the
  registers are back at defaults.
- Step 1's pin pulse must stay low for **≥ 0.5 ms** (tSTBY,min). The design keeps it low until
  CLEAR, so this is automatically satisfied.
- The SPI `HardHiZ` sent while the chip is in standby is ignored (SPI disabled). That is
  why Step 2 sends it again after wake-up.

## 2. The STBY/RESET pull resistor on the X-NUCLEO-IHM03A1 (the hardware item)

The powerSTEP01 itself pulls STBY/RESET **down** (450 kΩ). If the shield adds nothing, a
reset or hung MCU, whose PA9 returns to a floating input, lets the chip fall into standby and
the bridges turn off: fail-safe. If the shield adds a pull-up to 3V3/VDD, the opposite holds
and a dead MCU leaves the bridges enabled.

The board-level answer could not be verified from here: the UM1857 PDF on st.com and its
mirrors timed out or returned HTML. **Check the schematic in UM1857 (section "Schematic
diagrams", sheet with CN9 / D8 / STBY\RESET) and record the resistor designator, value and
rail in the table below.** If it is a pull-up, flag it as a hardware change (remove it or
add a stronger pull-down) before relying on the pin path as the primary stop.

Pin mapping on this build (from `server/layers/L5_board/board_pins.hpp`): STBY/RESET =
**PA9** (Arduino D8, CN9), FLAG = PA10 (D2), BUSY = PB5 (D4), CS = PC8, SPI1 SCK/MISO/MOSI =
PA5/PA6/PA7.

## 3. Bring-up list (from `docs/diagram/03_open_issues.md` item 5)

Connect over the ST-LINK VCP at 115200 8N1. Each line below is one command; the expected
reply is in brackets.

1. Reset the board, capture the boot banner and the init phase lines on the early-debug
   output (same COM port) [all phases `OK`, Encoder `OK` or `SKIP`].
2. `DIAG:PING 1` [`PONG 1 …`]; `DIAG:STAT?` [JSON/ASCII snapshot].
3. `MOT:RUN 300 1` → motor turns; `DIAG:STAT?` → `busy`, not `hiZ`.
4. `SYST:ESTOP` during the run [`OK`]; `DIAG:STAT?` → `hiZ` true; `MOT:RUN 300 1` [`ERROR … Fault`];
   `SYST:FAULT:CLEAR` [`OK`]; `MOT:RUN 300 1` [`OK`]; `MOT:STOP`.
5. `DRV:CFG:STEPMODE 4` [readback 4]; `DRV:CFG:STEPMODE 7`
   [readback 7]; restore the default.
6. `DBG:UNLOCK`; `SPI_STOP` (flat bring-up names need no prefix once unlocked) [Motor task paused]; `DIAG:STAT?` telemetry stops updating;
   `SPI_START`; `LCD_DISABLE` [display rendering disabled, driver reinitialised]; `DBG:LOCK`.
7. Encoder: turn the shaft through one index pulse; `GET_STATUS` → `indexSeen` true,
   `revolutions` ±1, `indexPeriodUs` plausible; `DIAG:STAT?` → `indexSeen` true; clear the flag via the encoder command and
   confirm it reads false until the next pulse.
8. Task monitor: on the LCD, long-press CENTER (1 s) → Main Menu → "Task Monitor" → read
   the stack high-water mark of every task. Record the **Encoder** row.

## 4. ESTOP latency measurement

Instrument (logic analyzer, ≥ 10 MS/s):

| Channel | Signal | Pin |
|---|---|---|
| 0 | USART2 RX (host → MCU) | PA3 (ST-LINK VCP TX) |
| 1 | STBY/RESET | PA9 |
| 2 | SPI1 SCK | PA5 |
| 3 | SPI1 MOSI | PA7 |
| 4 | powerSTEP01 CS | PC8 |
| 5 | (optional) USART2 TX (MCU → host) | PA2 |

Trigger on the **stop bit of the last byte of `ESTOP\n`** on channel 0 (0x0A). Measure to:

- t_pin: first falling edge on PA9 (not expected at baseline: nothing drives PA9 low today;
  record "no edge").
- t_spi: the `HardHiZ` byte **0xA8** on MOSI with CS low on SPI1 (the first 0xA8 after the
  trigger). This is the baseline "bridges off" moment.

Scenarios (run each three times, report min/median/max):

| Case | Setup | Expected today |
|---|---|---|
| (a) idle | motor stopped, display in LOCAL mode | ~0.5–1 ms |
| (b) during RUN | `MOT:RUN 300 1` first | similar to (a); may add one 50 ms status poll's SPI time if the Motor task holds SPI1 |
| (c) during full-screen bitmap | `UI_MODE REMOTE`, then `DISP_BITMAP 0 0 240 320`, start sending pixel bytes, send `ESTOP\n` **inside** the byte stream | the `ESTOP` bytes are consumed as pixels; the stop is only parsed after all 153,600 bytes: **seconds** (13.3 s at 115200 baud from stream start) |

`scripts/estop_baseline.py` drives the three scenarios and prints host-side timestamps for
cross-checking; the analyzer provides the real numbers.

## 5. Results table (the "before" column)

| Measurement | Value | Notes |
|---|---|---|
| Boot banner / phases | | |
| ESTOP (a) idle: last bit → 0xA8 on SPI1 | | PA9: no edge expected |
| ESTOP (b) during RUN | | |
| ESTOP (c) during bitmap: last bit → 0xA8 | | expect ≈ remaining stream time |
| Encoder task stack high-water mark (words free) | | from Task Monitor |
| Other tasks' high-water marks | | |
| STBY/RESET shield resistor (designator, value, rail) | | from UM1857 schematic |
| Step-mode readback (4, 7) | | |
| Debug pause / LCD disable observed | | |
| Index pulse: pulses / revolutions / period | | |
