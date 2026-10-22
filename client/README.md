# Host client

One shared core and two thin frontends (CLIENT_REVIEW section 9). Python 3.11+.

```
client/
  smc_core/            the core: pure Python + pyserial, no Qt
    transport/         ITransport; serial (VCP), tcp (virtual Nucleo), rtt (J-Link), memory;
                       open_transport("COM6" | "/dev/ttyACM0" | "tcp://HOST:PORT")
    framing.py         classify(line) -> Frame (response/event/telemetry/reset/banner/...)
    codec.py           Response, Event, ResetInfo, Identity, HeartbeatAck, parse_*()
    protocol_spec.py   GENERATED from the firmware parser (scripts/arch/gen_protocol.py)
    commands.py        one typed builder per PROTOCOL.md command -> Request
    views.py           typed views of text replies (DBG:MOTOR, DRV:CFG?, CTRL:ENC?, SYSID)
    units.py           motion units and powerSTEP01 register scaling (protocol 2)
    driver.py          the motor driver in physical units on protocol 3 or 2 (DRV:CAPS?, settings)
    board.py           Board: the link's threads, session, heartbeat, ESTOP, reset handling
    scenarios/         named tests: basic.py (bringup, estop_latency, heartbeat_loss, step_mode_readback,
                       sysid) and motion.py (motion_scale, step_mode_chip, telemetry_load)
    runner.py          run / submit scenarios; JSON-lines results
    kinematics.py      mecanum inverse/forward, motor speed scaling (pure functions)
    fleet.py           Fleet: four boards as one base (identity, base ESTOP, one-lost policy,
                       clock sync, synchronised start, merged telemetry)
    sim.py             FakeBoard: a small protocol 3 (or 2) responder for tests and --sim
  smc_gui/             PySide6 frontend; bridge.py is the only Qt <-> core seam
  smc_cli/             `smc` command line (Raspberry Pi, bench)
  tests/               pytest: core unit tests, board/scenario tests on the fake board,
                       the GUI on the fake board, GUI-vs-CLI identical output, frontend rules
```

## Run

From `client/` (Windows: `.venv\Scripts\python.exe`; or `pip install -e .[gui,dev]` for the
`smc` and `smc-gui` commands):

```
python -m smc_gui                 # GUI, pick the port in the window
python -m smc_gui --sim           # GUI against the in-process fake board
python -m smc_cli info --port tcp://127.0.0.1:5761   # a virtual Nucleo (sim/README.md)
python -m smc_cli ports
python -m smc_cli info --port COM6
python -m smc_cli send --port COM6 MOT:EN
python -m smc_cli telemetry --port COM6 -n 10
python -m smc_cli estop --port COM6
python -m smc_cli test list
python -m smc_cli test run --port COM6 bringup --out results.jsonl
python -m smc_cli test run --sim sysid -p type='"step"' -p speed=800
python -m smc_cli fleet check --config fleet.json      # docs/MULTI_BOARD.md
python -m smc_cli fleet drive --config fleet.json --vx 0.2 --seconds 2
python -m smc_cli fleet estop --config fleet.json
python -m pytest -q tests        # on every core (pytest-xdist, the dev extra): ~20 s; -n 0 for one process
```

## Starting the GUI on a new machine (a PC or the Raspberry Pi)

```
python3 start_gui.py          # Windows: py start_gui.py
python3 start_gui.py --check  # what it would set up, changing nothing
```

`start_gui.py` needs nothing but Python. It makes `client/.venv` (once), lists the packages the
GUI is missing with their download size and installs them from pypi.org when you say yes, and on
Linux offers the system pieces (Qt's X11 library libxcb-cursor0, the `input` group for a gamepad,
python3-venv when needed, an applications-menu entry), each asked first. Then it starts the GUI.
It asks in the terminal, or in a small window when started from the menu. A "no" to an optional
step is remembered.

The Raspberry Pi needs Raspberry Pi OS based on Debian Trixie (glibc 2.41): Qt for Python has no
build for Bookworm (glibc 2.36). The command line (`python3 -m smc_cli`) runs on both.

## Gamepads

The Drive tab and the Fleet page drive from a pad (`smc_core/gamepad.py`): an Xbox pad through
XInput (Windows), or a PS4 DualShock 4 / PS5 DualSense / any pad SDL2 maps, over USB or
Bluetooth, through pygame-ce (Windows and Linux, the Raspberry Pi 5 included; `pip install
pygame-ce`, part of the `gui` extra). The GUI takes whichever answers, XInput first, shows its
name and battery and names the buttons the pad's way (a PS4: ✕ ○ □ △, L1/R1, L2/R2,
OPTIONS); the light bar turns hot pink when the GUI has the pad. A pad that drops (out of
range, asleep) reads as no controller, which stops the base, and is picked up again within a
second of coming back.

Pairing a PS4 pad over Bluetooth (once; afterwards the PS button wakes it and it reconnects). The
GUI has the steps too: the controller group's "Pair a controller..." button, which opens Windows'
Bluetooth settings, or on Linux finds, pairs, trusts and connects the pad for you (bluetoothctl):

- Hold SHARE and PS together until the light bar double-flashes (pairing mode).
- Windows: Settings > Bluetooth & devices > Add device > Bluetooth, pick "Wireless Controller".
- Raspberry Pi 5 (Raspberry Pi OS Bookworm; the kernel's hid-playstation driver):

  ```
  bluetoothctl
  power on
  agent on
  default-agent
  scan on            # wait for "Wireless Controller" and its address
  pair XX:XX:XX:XX:XX:XX
  trust XX:XX:XX:XX:XX:XX
  connect XX:XX:XX:XX:XX:XX
  scan off
  quit
  ```

  The user running the GUI must be in the `input` group (the default user is) to read the
  pad. The light bar needs the pad's hidraw node too; without it the pad still drives.

## Rules (enforced by tests/test_frontend_rules.py and test_identical_output.py)

1. A frontend never formats a command line or parses a reply line. It builds a Request
   with `smc_core.commands` and renders `codec` / `views` data. New wire behaviour goes
   into the core (and PROTOCOL.md) first.
2. The core talks to frontends through plain data and callbacks; `smc_gui/bridge.py`
   turns callbacks into Qt signals. Nothing in `smc_core` imports Qt.
3. Threads live in `board.py` only (a reader and a worker per board).
4. Scenarios are data in, data out: the GUI Tests tab and `smc test run` call the same
   functions and get the same `ScenarioResult`.
5. GUI and CLI produce identical scenario results and command streams (heartbeats and
   session traffic filtered, polling runs collapsed).

Safety behaviour lives in the Board, not in a frontend: no heartbeat without fresh
liveness from the frontend's own loop; ESTOP purges unsent output and writes
`0x18` + `SYST:ESTOP` ahead of everything, drops the queue, cancels a running upload
within one chunk and reads the latch back; telemetry is pushed by the firmware; a board
reset (the `!RESET` line or a new boot id in a heartbeat ack) re-runs the session setup;
event sequence gaps are reported and trigger a status resync.

## The core's public surface

What a second implementation of the core (e.g. Rust via PyO3) must provide with the
same signatures and plain-data types: `transport.ITransport`; `framing.classify`;
`codec.*` dataclasses and parsers; `commands.*` builders and `Request`; `views.*`;
`units.*`; `board.Board`, `BoardConfig`, `Liveness`, `EstopResult`, `EventGap`,
`Ticket`, the subscription event names in `Board.EVENTS`, `Board.tx_log` /
`flight_recorder()`; `runner.run/submit/list_scenarios/write_jsonl/comparable`. The
identical-output test and the scenario suite are the acceptance tests.
