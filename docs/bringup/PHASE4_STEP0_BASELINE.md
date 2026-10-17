# Phase 4, Step 0: hardware baseline before the fast-ESTOP work

Status 2026-10-17: **complete**. The board was attached on 2026-10-17 (COM5, drag-and-drop
flashing). Every row of the results table in section 5 is filled: the serial bring-up list,
the ESTOP latencies (self-timed on the chip through `latency_us` in the `SYST:ESTOP` reply,
no logic analyzer needed), the task stack marks (`DIAG:TASKS?`), the debug gate, and the
STBY/RESET pull direction (`STBY_SENSE`: a pull-up on the shield). Phase 4 can start.

Status 2026-10-05 (historical): blocked on hardware. Everything that could be prepared
without the board is below: the datasheet facts the later steps need, the exact measurement
procedure, a scenario driver script, and the results table.

## 0. What to flash

Commit `c9a35e5` is the code baseline. Every commit after it on `master` is documentation
only, so the current `build/StepperMotorController.elf` (firmware-debug) is code-identical.
To be exact:

```powershell
git checkout c9a35e5
. scripts\build\env.ps1
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

Board-level answer (UM1910 "Getting started with the X-NUCLEO-IHM03A1", figure 3
schematic, read 2026-10-17; the earlier reference to UM1857 was the wrong document number):
the powerSTEP01 STBY\RESET pin 19 is the net `RESET_IC`, routed straight to CN5 pin 1
(Arduino D8 = PA9) and to CN10 pin 21. **There is no resistor on that net on the shield;
the only part is C6 = 100 nF to ground.** (R26 39 kΩ, the resistor next to it on the
sheet, is the BUSY LED pull-up on BUSY_SYNC.) So when the MCU lets go, the node discharges
through the chip's internal 450 kΩ pull-down with a time constant of about 45 ms, and the
chip enters standby: fail-safe, but tens of milliseconds, not microseconds. For the pin
path this means the MCU must drive PA9 low actively (the GPIO discharges C6 in
microseconds), which is what Phase 4 Step 1 does. No hardware change is needed.

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

`scripts/bench/estop_baseline.py` drives the three scenarios and prints host-side timestamps for
cross-checking; the analyzer provides the real numbers.

## 5. Results table (the "before" column)

Bench session 2026-10-17. Board on COM5 (ST-LINK VCP). Flashed by copying the `.bin` to the
NUCLEO's `NOD_F401RE` drive (the ST-LINK debug interface has no driver on this PC, so the
OpenOCD `flash` target fails; `docs/bringup` memory: drag-and-drop works). c9a35e5 built
in a worktree with the debug preset: text 201,136, data 2,004, bss 50,384. Serial items
run with `scripts/bench/bringup_step0.py COM5 --no-debug` and `scripts/bench/estop_baseline.py COM5`.

| Measurement | Value | Notes |
|---|---|---|
| Boot banner / phases | all phases `OK`: Clock, TickTimer, Platform, SPI Manager, L3 Services, Motor, Motor task, Encoder, Encoder task, Display, Display task, Comms, Comms task; first byte 0.7 s after programming | banner says v0.1.0, `SYST:VER?` says v0.3.0 (stale banner string at c9a35e5) |
| Host round trip `DIAG:PING` → `PONG` | min 11.6 / median 16.2 / max 17.5 ms (n=10) | USB + two UART lines; the floor for host-side numbers |
| ESTOP (a) idle, host side: write → `OK ESTOP` received | min 12.5 / median 14.7 / max 17.4 ms (n=10) | same as a ping: the firmware adds nothing measurable at host resolution |
| ESTOP (a) idle, **on-chip** (self-timed: USART ISR stamp of the line's last byte → `HardHiZ` SPI write returned, `latency_us` in the reply) | min 664 / median 697 / max 1574 µs (n=10) | measured 2026-10-17 on the current firmware instead of with an analyzer; the 1.5 ms outliers are one Motor-task status poll holding SPI1 |
| ESTOP (a') idle, ESTOP sent in the same millisecond as `MOT:EN` | min 682 / median 14246 / max 14836 µs (n=6) | the Motor task is executing the enable (HardStop + GET_STATUS + telemetry) and holds SPI1: the Hi-Z write waits a full Motor-task pass. This is the "SPI1 held" family (03_open_issues items 1 and 3), the number Phase 4's STBY pin path removes |
| ESTOP (b) during RUN, host side | min 10.6 / median 14.0 / max 15.6 ms (n=5) | |
| ESTOP (b) during `MOT:RUN 300`, on-chip | min 678 / median 692 / max 1546 µs (n=6); at `MOT:RUN 1000`: 672 / 690 / 703 µs (n=4) | the run itself adds nothing; the floor (~670 µs) is the Comms task wake-up, line parse and one SPI transaction under the mutex in the debug build |
| ESTOP (c) during full-screen bitmap | ESTOP text injected at +0.77 s (8,203 bytes into the stream) was consumed as pixels; the firmware replied only when the stream ended at **+14.4 s**; the next `SYST:ESTOP` was honoured at once | matches the prediction (13.3 s of stream at 115200 + USB); analyzer value optional |
| Encoder task stack high-water mark (words free) | **168** | read over serial with the new `DIAG:TASKS?` (same table as the LCD Task Monitor), after the motion scenarios and index crossings; item 4 of 03_open_issues can close: the estimate held |
| Other tasks' high-water marks (words free) | Comms 1403, Motor 797, Display 351, IDLE 114; free heap pool 512 bytes | `DIAG:TASKS?`, debug build |
| STBY/RESET shield resistor (designator, value, rail) | **none; C6 = 100 nF to ground** on the RESET_IC net (UM1910 figure 3, section 2 above). Bench: with PA9 switched to a floating input (`STBY_SENSE` debug command) the node falls LOW after 38.1 / 37.8 / 37.9 / 37.2 ms, which is 100 nF discharging through the chip's internal 450 kΩ pull-down to the input threshold (an earlier version of the command sampled after a few ms, saw the capacitor still high and reported a pull-up: wrong) | consequence for Phase 4: a dead MCU lets the chip fall into standby by itself after ~40 ms (fail-safe, slow); the pin path must drive PA9 low actively, which discharges C6 in microseconds. No hardware change needed |
| Step-mode readback (4, 7) | 4 → `step_mode 4, 1/16, 3200 µsteps/rev`; 7 → `step_mode 7, 1/128, 25600`; restored 5 (1/32) | each `DRV:CFG:APPLY` writes flash; the reply arrived within the 2.5 s wait |
| Run / ESTOP / refused / clear / run | `MOT:RUN 300 1` → position advancing at speed 299, bridges on; `SYST:ESTOP` → state ESTOP, Hi-Z; `MOT:RUN` → `ERROR Motor fault active`; `SYST:FAULT:CLEAR` → IDLE; `MOT:RUN` ok | `SYST:FAULT:CLEAR` while not latched answers `ERROR Invalid state for operation` |
| Debug pause / LCD disable observed | `DBG:UNLOCK` ok; `SPI_STOP` pauses the Motor task (telemetry tick advances, motor telemetry frozen); `SPI_START` resumes; `LCD_DISABLE` ok; `DBG:LOCK` ok; `SPI_STOP` refused when locked (`DBG_LOCKED`) | `scripts/bench/bringup_step0.py COM5 --no-motor`, 2026-10-17: all item-6 checks PASS |
| Index pulse: pulses / revolutions / period | `index_seen` true, revolutions 3, index period 2,404,685 µs, count 13,531 after the runs | the index was crossed during the 300 steps/s runs |
| Motor supply / faults at rest | UVLO false, OCD false, status 0xE203 (Hi-Z, not busy) | on the v0.3.0 firmware found on the board, OCD/STALL events fired at rest with OCD disabled in `ALARM_EN` (noise); not seen since reflashing |
| Firmware found on the board before Step 0 | v0.3.0, built Mar 6 2026 | replaced by c9a35e5 |

Scripts fixed during the session: `estop_baseline.py` case (c) paused 200 ms after the
bitmap header, longer than the firmware's 100 ms per-byte timeout, so the first run never
entered bitmap mode; `bringup_step0.py` item 3 now checks speed and Hi-Z rather than BUSY,
which the powerSTEP01 asserts only while a command executes.

## 6. After Phase 4 step 1 (out-of-band ESTOP, 2026-10-17)

Byte 0x18 is recognised in the USART2 interrupt, which drives STBY/RESET low and latches;
the Motor task finishes with the SPI HardHiZ and the wake-up on clear (PROTOCOL.md section 5).
Measured with the self-timing in the `SYST:ESTOP` reply (`oob_pin_us`, `oob_hiz_us`) on the
same board, same debug build:

| Case | 0x18 arrival -> STBY/RESET low | 0x18 arrival -> SPI HardHiZ | before (Step 0, line path) |
|---|---|---|---|
| (a) idle | 0-1 µs (5 of 5) | 1.3 / 1.6 / 3.4 ms (min / median / max) | 697 µs median |
| (a') ESTOP right after `MOT:EN` | 0-1 µs (5 of 5) | 14.6 / 14.6 / 15.2 ms (the Motor task still holds SPI1 for the enable) | 14.2 ms median |
| (b) during `MOT:RUN 300` | 1 µs (5 of 5) | 1.3 / 1.6 / 1.8 ms | 692 µs median |

The bridges are now off within a microsecond of the byte in every case; the SPI command that
follows only tidies up (the chip is already in standby and ignores it), so the 14 ms SPI1
hold no longer matters for the stop itself. It still delays the status snapshot, which the
Motor task now substitutes with "Hi-Z, stopped" while the chip is in standby. Recovery:
`SYST:FAULT:CLEAR` releases the pin, the Motor task waits 2 ms, re-initialises the driver
and re-applies the saved config; a 1-revolution move afterwards lands on 6400 microsteps
at the saved 1/32. The client's fast path (purge, 0x18, `SYST:ESTOP`, readback) confirms
`state=ESTOP hiZ=True` in 94-109 ms, which is the host-side readback cadence, not the stop.

Not covered by this step: a raw binary upload (`UI:DISP:BITMAP`, `:RLE`, `UI:FLASH:UPLOAD`,
`:RLE`) pauses the 0x18 recognition for its payload, since pixel bytes may be 0x18; the
client's 5-row RLE bands keep that window near 0.2 s, and the base64 variants keep the
byte active. Per-chunk SPI1 locking (03_open_issues item 1) and the single ESTOP state
holder (item 6) stay open.

## 7. After Phase 4 step 2 (interrupt-driven UART transmit, 2026-10-17)

`UartHardware::write()` queues into a 4 KB ring that the USART2 TXE interrupt drains; the
Comms task returns in microseconds instead of busy-waiting a line out at 115200 (a status
push cost it 78 ms, a `SYST:HELP?` 280 ms). When the ring is full the writer sleeps 1 ms at
a time. `flush()` waits for the ring and the shift register (baud change, binary transfers).

| Measurement | before (step 1) | after step 2 |
|---|---|---|
| `SYST:ESTOP` line parsed while a `SYST:HELP?` reply is still going out (`latency_us`) | 224 ms (the handler was busy-waiting the reply out) | 0.70 / 0.72 / 0.75 ms |
| `DIAG:PING` on-chip rx->tx while a `SYST:HELP?` reply is going out | not measured (the line waited behind the reply) | 1-2 µs |
| 0x18 -> STBY low during the same | 1 µs | 1 µs |
| host-side arrival of those replies | ~230-280 ms | 245-280 ms: the reply bytes still queue behind the HELP bytes on the wire; that is link time, not firmware time |
| `telemetry_load` at the 139 ms floor, `motion_scale` | PASS | PASS (encoder advanced in 10 of 10 samples; 4001 / 3986 counts per rev) |
| Step 0 case (c), ESTOP text inside a raw bitmap stream | parsed after the stream (+14.4 s) | unchanged by design: a raw payload pauses the 0x18 byte and the text is pixels; the client's RLE bands keep the window ~0.2 s and the base64 variants keep the byte active |

The telemetry floor (`telemetryPeriodFloorMs`) stays: it now protects the link budget rather
than the Encoder task. RAM: +4 KB bss for the ring.

## 8. After Phase 4 step 3 (SPI1 locked per chunk during bitmaps, 2026-10-17)

`LCD::streamBitmapStart` now releases SPI1 after the window set-up and every
`streamBitmapData` call is its own locked transaction, continued with the ST7789 RAMWRC
command (0x3C). Measured by running a software-timed 3 s identification step (the Motor task
advances the test's clock 50 ms per pass, so a blocked task simply stretches the run) while
a 240 x 60 raw bitmap (28.8 KB, 2.7 s at 115200) streams in remote display mode. Watchdog
widened to 5 s for the test, because a single 2.7 s receive starves the 500 ms heartbeat
(the GUI's 5-row bands exist for that reason).

| Measurement | before (bus held for the whole bitmap) | after (per chunk) |
|---|---|---|
| wall time of the 3.2 s software run | 6.34 s | 3.58 s (3.2 s + settle + poll granularity) |
| encoder travel during it (3 s at 400 steps/s = 6 rev = 24000 counts) | 45558 counts: the chip kept running at the last commanded speed, unsupervised, while the Motor task waited for the bus | 24303 / 24290 counts |
| Motor task passes during the 2.7 s stream | none | every 50 ms |

So during a remote bitmap the status poll, the following supervisor, the speed trim and
system identification keep running, and an ESTOP's SPI HardHiZ no longer queues behind the
stream (the pin path already made the stop itself independent of it).

Open, noted while measuring: twice the board answered nothing on a serial port opened
after it had booted (both the previous and this build), while a port held open through the
reset always saw a normal boot and normal replies; reprogramming with the port open
recovers it. Not reproduced since; tracked in `03_open_issues.md`.

## 9. After Phase 4 steps 4 and 5 (single ESTOP holder, Motor-task jobs, 2026-10-17)

`CommandQueue::getState()` now derives ESTOP from the MotorService latch instead of keeping
its own copy, and the three calls the Comms task used to make on the controller directly
(`reinit`, `applyConfig`, `getDebugInfo`) are messages the Motor task executes between its
own driver sequences, answered through a result slot (250 ms bounded wait).

| Measurement | value |
|---|---|
| 0x18 alone (no `SYST:ESTOP` line), then `DIAG:STAT?` | `state ESTOP`, `hi_z true`, `speed 0`; `MOT:RUN` refused with `FAULT`; `SYST:FAULT:CLEAR` -> IDLE, chip re-initialised at 1/32 (before step 4 the state read IDLE after a bare 0x18) |
| `DRV:REINIT` end to end (job round trip included) | 25.6 ms |
| `DRV:CFG:APPLY` end to end (apply job + flash journal append + readback job) | 46.5 ms |
| `MOTOR_DEBUG` (GetDebugInfo job, 15 registers) | 19.4 ms |
| scenarios after | motion_scale, step_mode_chip, estop_latency PASS (confirm 78 ms) |

Phase 4 as planned in `03_open_issues.md` items 1, 3, 6 and the Step 0 proposal is complete.
