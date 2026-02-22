# Command Reference — STM32 Stepper Motor Controller

Protocol: line-based ASCII over UART (115200 8N1)
Format: `<CMD> [ARG1] [ARG2]...\r\n`
Response: `OK [data]\n` or `ERROR <code> [msg]\n` (ASCII mode)
         `{"status":"ok","command":"CMD","data":{...}}\n` (JSON mode)

SCPI-style namespaced commands (e.g. `MOT:RUN`) and legacy flat commands
(e.g. `RUN`) are both supported. They route to the same handler.

---

## Motion

| SCPI Name | Legacy Alias | Args | Description |
|---|---|---|---|
| `MOT:MOVE` | `MOVE` | `<steps> <dir>` | Relative move. steps: 0-2097151, dir: 0=rev 1=fwd |
| `MOT:GOTO` | `GOTO` | `<position>` | Absolute move. position: -2097152 to 2097151 |
| `MOT:RUN` | `RUN` | `<speed> <dir>` | Continuous run. speed: 0-15625 steps/s, dir: 0/1 |
| `MOT:STOP` | `STOP` | `[hard]` | Soft stop (decelerate) or hard stop (immediate) |
| `MOT:EN` | `ENABLE` | | Enable motor outputs (release SoftHiZ) |
| `MOT:DIS` | `DISABLE` | | Disable motor outputs (HardHiZ) |
| `MOT:HOME` | `HOME` | | Return to home position (GoHome) |
| `MOT:ZERO` | `ZERO` | | Reset motor ABS_POS counter to zero (motor only) |
| `CTRL:ENC:ZERO` | `ENC_ZERO` | | Reset encoder count to zero (encoder only) |
| `SYST:ZERO` | `ZERO_ALL` | | Reset both motor ABS_POS and encoder count to zero |

## Motion Configuration

| SCPI Name | Legacy Alias | Args | Units | Description |
|---|---|---|---|---|
| `MOT:CFG:ACCEL` | — | `<value>` | steps/s² | Set acceleration (1-59590) |
| `MOT:CFG:DECEL` | — | `<value>` | steps/s² | Set deceleration (1-59590) |
| `MOT:CFG:MAXSPD` | — | `<value>` | steps/s | Set max speed (1-15609) |
| — | `ACCEL` | `<value>` | raw 12-bit | Set acceleration register (1-4095) |
| — | `DECEL` | `<value>` | raw 12-bit | Set deceleration register (1-4095) |
| — | `MAXSPD` | `<value>` | raw 10-bit | Set max speed register (1-1023) |

**Note:** SCPI commands use physical units with integer conversion.
Legacy commands use raw powerSTEP01 register values. Both coexist.

## Safety

| SCPI Name | Legacy Alias | Args | Description |
|---|---|---|---|
| `SYST:ESTOP` | `ESTOP` | | Emergency stop — outputs disabled, latch set |
| `SYST:FAULT:CLEAR` | `CLEAR_FAULT` | | Clear fault latch (rejects if hardware faults still active) |
| `SYST:FAULT:STAT?` | — | | Query fault status (currently uses GET_STATUS) |

**Safety hardening:** `CLEAR_FAULT` / `SYST:FAULT:CLEAR` reads the powerSTEP01
STATUS register before clearing. If OCD, TH_SD, or UVLO flags are still active,
the command is rejected with a descriptive error listing active faults.

## System / Heartbeat / Timing

| SCPI Name | Legacy Alias | Args | Description |
|---|---|---|---|
| `SYST:TICK?` | `GET_TICK` | | Query FreeRTOS tick counter |
| `SYST:HB` | `HEARTBEAT` | `<seq>` | Reset comms watchdog. Returns ACK with timing data |
| `SYST:HB:TIMEOUT` | `SET_HEARTBEAT` | `<ms>` | Set watchdog timeout. 0=disabled, clamped [100, 5000] |
| `SYST:HB:STAT?` | `GET_HEARTBEAT_STATUS` | | Query watchdog state |
| `SYST:VER?` | — | | Firmware version (alias for `*IDN?`) |

## Diagnostics

| SCPI Name | Legacy Alias | Args | Description |
|---|---|---|---|
| `DIAG:PING` | `PING` | `<seq>` | Latency probe. Returns `PONG <seq> <rx_tick> <tx_tick> <state>` |
| `DIAG:STAT?` | `GET_STATUS` | | Full telemetry snapshot (motor + encoder + system) |

## Synchronized Multi-Controller

| SCPI Name | Legacy Alias | Args | Description |
|---|---|---|---|
| `SYNC:QUEUE` | `QUEUE` | `<cmd> [args]` | Queue a motion command |
| `SYNC:ARM` | `ARM` | | Arm queued commands for synchronized execution |
| `SYNC:START` | `START` | | Begin executing armed commands immediately |
| `SYNC:START:AT` | `START_AT` | `<tick>` | Begin execution at specified FreeRTOS tick |
| `SYNC:CLEAR` | `CLEAR_QUEUE` | | Discard all queued commands |

### QUEUE sub-commands

| Sub-command | Args | Description |
|---|---|---|
| `QUEUE MOVE` | `<steps> <dir>` | Queue relative move |
| `QUEUE GOTO` | `<position>` | Queue absolute move |
| `QUEUE RUN` | `<speed> <dir>` | Queue continuous run |

## Identity / Version

| SCPI Name | Legacy Alias | Args | Description |
|---|---|---|---|
| `*IDN?` | `VER`, `VERSION` | | Firmware version, build date, device ID, role |
| `DEV:ID?` | `GET_DEVICE_ID` | | Query numeric device ID |
| `DEV:ID` | `SET_DEVICE_ID` | `<id>` | Assign numeric device ID |
| `DEV:ROLE?` | — | | Query device role (returns ID + role) |
| `DEV:ROLE` | `SET_ROLE` | `<role>` | Set wheel role: `FL` `FR` `RL` `RR` `NONE` |

## Control Mode / Encoder

| SCPI Name | Legacy Alias | Args | Description |
|---|---|---|---|
| `CTRL:MODE?` | `GET_MODE` | | Query control mode |
| `CTRL:MODE` | `SET_MODE` | `<mode>` | Set mode: `OPEN_LOOP` or `CLOSED_LOOP` |
| `CTRL:ENC:STAT?` | `GET_ENCODER_STATUS` | | Query encoder availability/status |
| `CTRL:ENC?` | `ENCODER`, `ENC` | | Read encoder count, velocity, index state |
| `CTRL:ENC:DBG?` | `ENC_DEBUG` | | Raw encoder hardware register dump |
| `CTRL:ENC:FILT` | `ENC_FILTER` | `[type] [param]` | Set/query velocity filter. Types: `NONE`, `EMA <alpha>` (0-255), `SMA <window>` (2-32). Bare number = legacy EMA. |

## Response Format

| SCPI Name | Legacy Alias | Args | Description |
|---|---|---|---|
| `FMT` | `SET_FORMAT` | `<fmt>` | Switch format: `ASCII` or `JSON` |
| `FMT?` | `GET_FORMAT` | | Query current response format |

## UI / Display (Remote Rendering)

| SCPI Name | Legacy Alias | Args | Description |
|---|---|---|---|
| `UI:MODE` | `UI_MODE` | `[LOCAL\|REMOTE]` | Get or set UI mode |
| `UI:MODE?` | `UI_GET_MODE` | | Query current UI mode |
| `UI:DISP:CLEAR` | `DISP_CLEAR` | `[color]` | Clear display. color: RGB565 (default 0x0000) |
| `UI:DISP:TEXT` | `DISP_TEXT` | `<x> <y> <fg> <bg> <text>` | Draw text at (x,y) with fg/bg RGB565 |
| `UI:DISP:RECT` | `DISP_RECT` | `<x> <y> <w> <h> <color> [fill]` | Draw rectangle. fill: 0/1 |
| `UI:DISP:LINE` | `DISP_LINE` | `<x1> <y1> <x2> <y2> <color>` | Draw line (Bresenham) |
| `UI:DISP:BITMAP` | `DISP_BITMAP` | `<x> <y> <w> <h>` | Binary RGB565 stream (raw bytes follow) |
| `UI:DISP:BITMAP:B64` | `DISP_BITMAP_B64` | `<x> <y> <w> <h> <b64data>` | Base64-encoded RGB565 bitmap |

## Motor Configuration (powerSTEP01 Registers)

| SCPI Name | Legacy Alias | Args | Description |
|---|---|---|---|
| `DRV:CFG?` | `MCONFIG` | | Show current motor config (multi-line) |
| `DRV:CFG:SAVE` | `MCONFIG_SAVE` | | Persist current config to flash |
| `DRV:CFG:LOAD` | `MCONFIG_LOAD` | | Load config from flash |
| `DRV:CFG:RESET` | `MCONFIG_RESET` | | Reset config to factory defaults |
| `DRV:CFG:KVAL` | `MCONFIG_KVAL` | `<args>` | Set KVAL registers (hold/run/acc/dec, 0x00-0xFF) |
| `DRV:CFG:OCD` | `MCONFIG_OCD` | `<args>` | Set overcurrent detection threshold |
| `DRV:CFG:STALL` | `MCONFIG_STALL` | `<args>` | Set stall detection threshold |
| `DRV:CFG:FAULT` | `MCONFIG_FAULT` | `<args>` | Configure fault handling behavior |
| `DRV:CFG:MOTION` | `MCONFIG_MOTION` | `<args>` | Set motion parameters (acc/dec/maxspd/etc.) |
| `DRV:CFG:STEPMODE` | `MCONFIG_STEPMODE` | `<mode>` | Set microstep mode (0-7: full/half/.../1/128, or 8/16/32/64/128) |
| `DRV:CFG:APPLY` | `MCONFIG_APPLY` | | Write current config to powerSTEP01 registers |

## Debug / Trace

| SCPI Name | Legacy Alias | Description |
|---|---|---|
| `DBG:MOTOR` | `MOTOR_DEBUG` | powerSTEP01 register dump + GPIO diagnostics |
| `DBG:TRACE:DUMP` | — | Dump trace ring buffer (128 entries, oldest first) |
| `DBG:TRACE:RESET` | — | Clear trace ring buffer |

Trace output format: `[idx] T=<tick> >/<  <tag> <arg>`
- `>` = ENTRY, `<` = EXIT
- Tags: `CMD:RX`, `MOT:RUN`, `MOT:MOVE`, `MOT:GOTO`, `MOT:STOP`, `MOT:EN`,
  `MOT:DIS`, `MOT:HOME`, `MOT:ZERO`, `SAFE:ESTOP`, `SAFE:CLR`, `SAFE:HB:TO`, `SAFE:HB`

## Utility

| Command | Args | Description |
|---|---|---|
| `HELP` / `?` | | Print command summary |

## Debug / Low-Level (Bringup Diagnostics)

These are legacy flat commands only — no SCPI aliases. They exist for
hardware bringup and SPI debugging. Not listed in `HELP`.

| Command | Description |
|---|---|
| `SPI_DEBUG` | Show SPI1 CR1, SR, GPIO MODER/AFR/ODR |
| `SPI_MODE_TEST` | Permute SPI modes and read STATUS for each |
| `CS_LOW` / `CS_HIGH` | Manual motor chip select control |
| `MISO_PULLDOWN` / `MISO_NOPULL` | Configure MISO pull resistor |
| `STBY_RELEASE` / `STBY_HOLD` | powerSTEP01 standby pin control |
| `GPIO_STATE` | Read relevant GPIO pin states |
| `PA6_HIZ` / `PA6_RESTORE` | Float/restore PA6 (MISO) |
| `SPI_STOP` / `SPI_START` | Disable/enable SPI1 peripheral |
| `GPIO_DUMP` | Full GPIO port register dump |
| `SPI_FLOAT_TEST` | Diagnose floating SPI lines |
| `SPI_BITBANG` | Bit-bang SPI transfer (Mode 0) |
| `SPI_BITBANG_M3` | Bit-bang SPI transfer (Mode 3) |
| `SPI_ECHO_TEST` | SPI echo/loopback diagnostic |
| `SPI_CS_TOGGLE` | CS toggle timing diagnostic |
| `PS01_DIAG` | powerSTEP01 deep diagnostic read |
| `PS01_FIX_CLK` | Clock recovery sequence for powerSTEP01 |
| `PS01_SAFE` | Safe power-on initialization sequence |
| `PS01_RESET` | Full powerSTEP01 reset + re-init |
| `PS01_RUN` | Direct powerSTEP01 RUN (bypasses MotorTask queue) |
| `PS01_STOP` | Direct powerSTEP01 stop (bypasses MotorTask queue) |
| `LCD_DISABLE` | Disable LCD SPI (free bus for motor) |
| `MOSI_TEST` | MOSI pin toggle diagnostic |
| `SPI_LOOPBACK` | SPI loopback test |

---

**Totals:** ~50 production commands (SCPI + legacy aliases) + ~20 debug/bringup commands
