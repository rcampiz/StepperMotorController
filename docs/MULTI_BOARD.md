# Four boards on a mecanum base

A Raspberry Pi drives four NUCLEO-F401RE + IHM03A1 boards over USB, one per wheel (FL, FR,
RL, RR). The Pi runs the coordinator: kinematics, synchronised motion, telemetry and
safety. This document is the design. `client/smc_core/fleet.py` and
`client/smc_core/kinematics.py` implement the host side, and
`client/tests/test_fleet_vnucleo.py` exercises it against four virtual Nucleos
(`sim/README.md`).

Numbers are labelled. **Measured (sim)** means measured on four virtual Nucleos on one PC.
**Estimate** means derived from the protocol, the firmware source and the link rates;
it needs a bench measurement on real boards (section 8).

## 1. Board identity

Each board has three identities:

| Identity | Where it comes from | Use |
|---|---|---|
| ST-LINK USB serial | the ST-LINK/V2-1 on the NUCLEO, visible to the OS | stable device name on the Pi (udev) |
| MCU serial | `*IDN?` field 3, the STM32 96-bit UID folded to 8 hex characters | proof that the board on a port is the expected one |
| Stored role | `DEV:ROLE FL\|FR\|RL\|RR` in the board's flash | convenience only |

**Linux enumeration.** `/dev/ttyACM0..3` follow plug-in and enumeration order, so they
can change between boots. `docs/multi_board/99-smc-wheels.rules` creates
`/dev/smc-FL` … `/dev/smc-RR` from each ST-LINK's USB serial, independent of USB socket
and order. Set up once:

```
udevadm info -a -n /dev/ttyACM0 | grep '{serial}' | head -1    # once per board
sudo cp docs/multi_board/99-smc-wheels.rules /etc/udev/rules.d/  # fill in the 4 serials
sudo udevadm control --reload && sudo udevadm trigger
```

**Swapped ports.** The fleet config (`docs/multi_board/fleet.example.json`) maps each role
to an address and to the expected `*IDN?` serial. `Fleet.open()` refuses to start if
two roles reach the same board or a board answers on the wrong corner:
`FleetError: board on the wrong corner`. This catches swapped cables, swapped NUCLEOs
and a wrong udev entry; the sim tests swap two boards to prove it.

**Do not rely on the stored role today.** `DEV:ID` and `DEV:ROLE` crash current firmware
(`g_deviceConfig.init()` is never called; `docs/review/LINK_BUDGET.md`, finding 1).
The fleet binds roles to serials on the Pi, so it does not need them. Once fixed, the
stored role can be a cross-check: refuse if the stored role disagrees with the config.

## 2. Synchronised motion

**What the firmware offers.** `SYNC:QUEUE <cmd>` stages motions without running them.
`SYNC:ARM` arms the queue, and `SYNC:START` flushes it to the Motor task, which wakes at
once. `SYNC:START:AT <tick>` does the same at a given value of the board's microsecond
counter (TIM5). The Pi learns each board's offset from `DIAG:PING`, which returns the
board's receive and transmit ticks (`Fleet.sync_clocks()`, NTP-style, best of 8).

**`SYNC:START:AT` runs on the Motor task** (since 2026-10-17). The Comms task validates
the tick (past: `TIMEOUT_EXPIRED`; more than 2 s ahead: `INVALID_PARAM`), hands a `StartAt`
command to the Motor task and answers `OK SCHEDULED` at once. The Motor task sleeps in
1 ms steps until 1.5 ms before the tick, spins the rest, then flushes the armed queue
itself; it abandons the wait if the ESTOP latch is set or the queue is cleared. The Comms
task stays free, so an ESTOP or the watchdog is handled during the wait. (It used to
sleep and spin on the Comms task, which is why the fleet kept the delay at 150 ms.)

**Start skew if the Pi sends `SYNC:START` to four ports in turn (estimate).** The Pi's
four writes take microseconds. Each board then sees:
- USB: data waits for the next 1 ms full-speed frame on its own ST-LINK. Four
  independent phases give 0 to 1 ms each, plus unknown ST-LINK buffering.
- UART: `SYNC:START\r\n` is 12 bytes, 1.04 ms at 115200. This is the same on every
  board, so it adds latency, not skew.
- Firmware: the USART2 receive wake runs the Comms task, the line is parsed at `\n`,
  and the flush wakes the Motor task. Tens of microseconds.

So the expected skew is about 1 ms, perhaps 2 to 3 ms with ST-LINK buffering. At 1000
full steps/s, 2 ms is 2 full steps (1 % of a revolution), about 3 mm at the rim of a
97 mm wheel. All four wheels follow the same acceleration ramp, so the base hardly
notices.

**`SYNC:START:AT` with clock sync (estimate).** The offset error is about half the
round-trip asymmetry of `DIAG:PING`, roughly ±1 ms over the VCP. The boards' clocks also
drift apart: the NUCLEO clock comes from the ST-LINK's 8 MHz MCO, so ±50 ppm means up to
0.1 ms per second between two boards, and a re-sync per start is needed. Expected skew is
therefore about 1 ms as well: no better than sending in turn, and it blocks the Comms
task.

**Recommendation.** For driving, fan out `MOT:RUN` (what `Fleet.drive()` does) or use
`SYNC:START` in turn. Use `SYNC:START:AT` only once the firmware makes it non-blocking.
If a sub-millisecond start is ever needed, use a shared hardware line: a GPIO wired to
an EXTI input on all four boards that triggers the armed start, which gives microseconds.

**Measured (sim):** both paths start all four wheels (`test_synchronised_start_starts_every_wheel`).
The sim cannot measure the skew on Windows: every board process wakes on the same
15.6 ms OS timer tick, which lines the boards up artificially. Measure it on the bench
(section 8).

## 3. ESTOP for the whole base

**Software fan-out (implemented, `Fleet.estop()`).** The fleet writes the stop on every
board before waiting for any confirmation. Each write is: purge unsent output, then
`0x18` and `SYST:ESTOP`. Then it reads back each board's latch and Hi-Z state.

- **Measured (sim):** all four writes go out within 0.01 ms of each other. Each board
  confirms latched and Hi-Z in 78 to 94 ms; that time is the Motor task's 50 ms status
  refresh, not the stop.
- **Estimate, hardware, after Phase 4.** The `0x18` byte is honoured in the USART2
  interrupt: up to 1 ms of USB frame wait, 87 µs on the UART, then the driver's STBY pin
  goes low. Each wheel stops within about 1 to 2 ms, and the wheels are within about
  1 ms of each other.
- **Today, before Phase 4.** The text command waits for the Comms task. That is
  milliseconds normally, but seconds while a bitmap or flash upload handler runs on that
  board (`docs/diagram/03_open_issues.md` item 1).

**Shared hardware line (recommended in addition).** Wire a normally-closed E-stop loop
to all four powerSTEP01 STBY/RESET inputs, diode-ORed with each board's own PA9 drive,
so opening the loop pulls every STBY low. This takes microseconds and works with the Pi,
USB or firmware dead. Two consequences:

- STBY resets the driver's registers (DS9836), so recovery needs the Phase 4 CLEAR
  sequence, which re-initialises the driver.
- The firmware should see an external STBY low and latch its own ESTOP state. That needs
  a spare input pin wired to the loop (section 7).

Recommended layering:

| Layer | Stops | Covers |
|---|---|---|
| Hardware E-stop loop on STBY | all four, µs | the physical button; everything else dead |
| `Fleet.estop()` fan-out | all four, ~1–2 ms after Phase 4 | software stop, one-board policy |
| Each board's heartbeat watchdog | its own wheel, ≤ 500 ms | Pi, USB or coordinator dead |

**One board loses USB: stop the other three (implemented).** You were right: a base with
one dead wheel must not keep driving. `Fleet` stops the remaining boards when any board
loses its link, misses three heartbeats, reports a FAULT or STALL event, or reboots.
- **Measured (sim):** killing one board's process stops the other three within the
  test's polling (under 3 s; the work itself is immediate).
- **The lost board** stops through its own watchdog within 500 ms.
- **Recovery** is an explicit operator action: `Fleet.rearm()` clears the latches.

## 4. Heartbeat

- **Per board.** Each `Board` sends `SYST:HB` every 100 ms; each firmware watchdog stops
  its wheel 500 ms after the last one (`docs/review/LINK_BUDGET.md`).
- **One liveness token for the fleet.** The coordinator's control loop touches it every
  cycle. If that loop hangs, all four boards stop being fed and all four wheels stop.

How each failure is detected and what stops the wheels:

| Failure | How it is seen | What stops the wheels |
|---|---|---|
| Coordinator loop hangs | liveness stale | all four watchdogs, ≤ 0.5 s |
| Pi process crashes or Pi kernel hangs | no heartbeats | all four watchdogs, ≤ 0.5 s |
| One USB cable pulled | that link errors on the Pi; that board gets no heartbeats | fleet stops the other three at once; that board's watchdog ≤ 0.5 s |
| One board hangs | no heartbeat acks (3 misses, ~0.3 s) | fleet stops the other three |
| One board resets | `!RESET` line or a new boot id | fleet stops the other three; the reset board is already stopped |

**How a board detects that the Pi has died.** Heartbeats stop arriving, and its watchdog
fires. No extra mechanism is needed.

## 5. Kinematics

`client/smc_core/kinematics.py` is plain functions over small dataclasses, with no numpy,
so it ports to Rust line for line. Body frame: x forward, y left, ωz counter-clockwise.
Rollers form an X seen from above. With r the wheel radius and k = lx + ly:

```
w_FL = (vx - vy - k*wz)/r     w_FR = (vx + vy + k*wz)/r
w_RL = (vx + vy - k*wz)/r     w_RR = (vx - vy + k*wz)/r
vx = r/4 (FL+FR+RL+RR)   vy = r/4 (-FL+FR+RL-RR)   wz = r/(4k) (-FL+FR-RL+RR)
```

Wheel rad/s converts to motor full steps/s through the gear ratio and steps per
revolution. A per-wheel `inverted` flag accounts for the mirrored right-hand motors.
`motor_commands()` scales every wheel by one factor when any wheel would exceed
`max_motor_sps`, so the base keeps its direction of travel and its turn ratio. Keep
`max_motor_sps` under the configured MAX_SPEED: the powerSTEP01 clamps RUN there.

**Tests:**
- Unit tests check the inverse/forward round trips, the canonical motions, the motor
  conversion and the scaling.
- **Measured (sim):** `Fleet.drive(vx=0.20, vy=0.10, wz=0.30)` on four virtual boards
  reads back (0.199, 0.100, 0.300) from the motors' reported speeds through
  `Fleet.measured_twist()`.

## 6. Merging four telemetry streams

Each board pushes its status every 200 ms on its own schedule. The Pi stamps every line
on arrival with one monotonic clock. `Fleet.frame()` returns the newest sample of each
board with its age, plus `spread_s`, the time between the oldest and newest of the four.
**Measured (sim):** a 16 ms spread.

Ages are not sample times. A status line takes about 78 ms on the wire at 115200, and
the Motor task refreshes the snapshot every 50 ms, so a sample is 0 to about 130 ms old
when it arrives.

Policy:
- **Display and supervision:** use the frame as is, and treat a board older than 3
  periods as stale.
- **Odometry and control:** use the board's own sample time. Every status carries `tick`,
  the board's microsecond counter, which maps to Pi time through the `sync_clocks()`
  offsets. Re-sync every few seconds because of crystal drift.

A per-board link budget is unaffected: every wheel has its own USB link.

## 7. Firmware changes the four-board base needs (priority order)

1. **Initialise the identity store** so `DEV:ID`/`DEV:ROLE` stop crashing (finding 1).
   Small change, needed before the stored role can be used as a cross-check. **Done
   2026-10-17.**
2. **Phase 4 fast ESTOP** (0x18 in the USART2 ISR, STBY low): makes the fan-out a 1–2 ms
   stop on every wheel, even during uploads.
3. **Never stall the MCU while driving.** `DRV:CFG:APPLY` and every save erase a 128 KB
   flash sector, about 1–2 s with the CPU likely stalled (finding 2). On a base, that is
   one wheel frozen while three keep driving. **Done 2026-10-17**: saves append to a flash
   journal; the erase runs at boot or only with the motor stopped (`STORE_FULL` otherwise).
4. **Make `SYNC:START:AT` non-blocking** (section 2), so a timed start never hides an
   ESTOP or the watchdog. **Done 2026-10-17**: the Motor task waits and flushes.
5. **External STBY sense:** an input pin on the E-stop loop, so a hardware stop also
   latches the firmware state and is reported as an event.
6. **Fix the encoder velocity units** (finding 4) before any closed-loop mode is used on
   the base. Otherwise trim and supervision act on velocities 1000 times too small.
   **Done 2026-10-17**: counts per second.
7. **Compact telemetry line** (about 80 bytes) for odometry at 20–50 Hz per wheel without
   loading the link.

## 8. Bench measurements to make with the real boards

| What | How | Expected |
|---|---|---|
| Start skew, `SYNC:START` in turn | logic analyser on the four STEP or BUSY pins, `smc fleet` start | ≈ 1 ms (2–3 ms worst) |
| Start skew, `SYNC:START:AT` | same | ≈ 1 ms |
| Base ESTOP, wheel to wheel | analyser on the four STBY pins, `smc fleet estop` | ≤ 1 ms after Phase 4 |
| ESTOP with one board mid-upload | same, upload running on one board | today: seconds on that board |
| Telemetry arrival jitter | Pi timestamps per board | 0–130 ms sample age |

## 9. Running it on four virtual boards

```
cd sim && cmake --preset vnucleo && cmake --build --preset vnucleo && cd ..
python sim/vnucleo_fleet.py --count 4                 # tcp://127.0.0.1:5761..5764
# a fleet config pointing at those addresses with serials VN000001..VN000004, then:
cd client
python -m smc_cli fleet check --config fleet_sim.json
python -m smc_cli fleet drive --config fleet_sim.json --vx 0.2 --vy 0.1 --wz 0.3 --seconds 2
python -m smc_cli fleet estop --config fleet_sim.json
python -m pytest -q tests/test_fleet_vnucleo.py -s
```

Kill one `vnucleo` process to rehearse a lost wheel.
