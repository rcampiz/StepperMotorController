# Virtual Nucleo

The firmware's real protocol layer (L2), services layer (L3) and Motor, Comms and Encoder
task bodies, compiled for a desktop and reached over TCP instead of the ST-LINK serial
port. One process is one board. No hardware, no FreeRTOS.

| Board part | On the virtual Nucleo |
|---|---|
| USART2 + 1 KB RX ring | TCP server, same 1 KB ring, overflow counted (F1) |
| powerSTEP01 over SPI | `FakePowerStep`: datasheet registers, trapezoidal RUN/MOVE/GOTO, MAX_SPEED clamp, Hi-Z |
| Quadrature encoder + DMA ring | `FakeEncoderHw`: follows the fake motor (1000 PPR, x4) |
| Internal-flash config, NOR flash | RAM; config and identity persisted with `--state DIR` |
| Remote LCD | accepts and counts draw commands |
| FreeRTOS tasks | `std::thread` running `vMotorTask`, `vCommsTask`, `vEncoderTask` |

Known differences: the identity store has its own fake flash here (the firmware keeps it in
the reserved first sectors of the NOR image flash); no LCD, joystick or display task; the boot id is random per process and the serial comes from `--serial`.
`harness/pins/interrupt_guard.hpp` is shadowed by a host mutex (`vnucleo/shadow/`).

## Build (Windows with WinLibs via `scripts\build\env.ps1`, or any Linux / Raspberry Pi with g++)

```
cd sim
cmake --preset vnucleo
cmake --build --preset vnucleo          # -> build-vnucleo/vnucleo(.exe)
```

## Run

```
build-vnucleo/vnucleo --port 5761                      # one board
python sim/vnucleo_fleet.py --count 4                  # four boards, ports 5761..5764
python sim/vnucleo_fleet.py --count 4 --state sim_state   # keep config/identity between runs
```

Then point any client at `tcp://127.0.0.1:5761`:

```
cd client
python -m smc_cli info --port tcp://127.0.0.1:5761
python -m smc_cli test run --port tcp://127.0.0.1:5761 bringup estop_latency sysid
python -m smc_gui                                      # pick "tcp://127.0.0.1:5761 - virtual Nucleo"
```

Kill one board's process to rehearse "one wheel lost"; with the watchdog armed, a board
whose host disappears stops its motor exactly as the real one does.

The client tests (`client/tests/test_vnucleo.py`, and the identical-output test) use the
virtual Nucleo automatically when `build-vnucleo/` exists, and skip it otherwise.
