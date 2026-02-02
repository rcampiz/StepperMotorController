# Display Architecture

This document describes the display subsystem for the ST7789-based LCD on the X-NUCLEO-GFX01M2 expansion board.

## Hardware

- **Display**: ST7789 240x240 RGB LCD
- **Interface**: SPI2 (shared with NOR flash)
- **SPI Mode**: Mode 0 (CPOL=0, CPHA=0)

## Display Modes

The display operates in one of the following modes:

| Mode | Description |
|------|-------------|
| `Menu` | Interactive menu for parameter adjustment and mode selection |
| `Status` | Real-time telemetry display (position, speed, encoder) |
| `Debug` | Runtime information, task stats, heap usage |
| `Image` | Full-frame image display via J-Link transfer |

### Mode Switching

Mode transitions are triggered by:
1. Joystick input (center button cycles modes)
2. Host command (`DISPLAY:MODE:<mode>`)
3. Automatic timeout (Image mode returns to Status after idle)

## Menu System

### Navigation

| Input | Action |
|-------|--------|
| Up | Previous menu item / increment value |
| Down | Next menu item / decrement value |
| Left | Back / cancel |
| Right | Enter submenu / confirm |
| Center | Select / toggle |

### Menu Structure

```
Main Menu
├── Control Mode
│   ├── Open Loop
│   └── Closed Loop
├── Motor Settings
│   ├── Max Speed
│   ├── Acceleration
│   └── Deceleration
├── Encoder
│   ├── Zero Position
│   └── Show Index Status
├── Display
│   ├── Brightness
│   └── Refresh Rate
└── System
    ├── Device ID
    ├── Reset
    └── About
```

## Status Display

Default telemetry view showing:

```
┌────────────────────┐
│ STEPPER CONTROLLER │
├────────────────────┤
│ Position:   12345  │
│ Target:     15000  │
│ Speed:       1200  │
│ Status:     MOVING │
├────────────────────┤
│ Encoder:    12340  │
│ Velocity:    1198  │
│ Index:        YES  │
├────────────────────┤
│ Mode: CLOSED_LOOP  │
│ ID: 0x01           │
└────────────────────┘
```

## Debug Display

Runtime diagnostics:

```
┌────────────────────┐
│ DEBUG INFO         │
├────────────────────┤
│ Heap Free:  45.2KB │
│ Heap Min:   42.1KB │
├────────────────────┤
│ Task        Stack  │
│ Motor        156   │
│ Encoder       84   │
│ Display      128   │
│ Comms        312   │
├────────────────────┤
│ Uptime: 01:23:45   │
│ CPU: 12%           │
└────────────────────┘
```

## J-Link Image Transfer

Full-frame images can be pushed to the display over RTT for splash screens, logos, or diagnostics.

### Protocol

1. Host sends `IMAGE:START` command
2. Controller enters Image mode, clears display
3. Host streams raw RGB565 pixel data over RTT channel 2
4. Controller writes pixels to LCD via SPI
5. Host sends `IMAGE:END` command
6. Controller returns to previous mode (or stays in Image mode)

### Data Format

- **Pixel format**: RGB565 (16-bit, big-endian)
- **Resolution**: 240x240 = 57,600 pixels = 115,200 bytes
- **RTT channel**: 2 (dedicated for image data)
- **Flow control**: RTT handles buffering; host should throttle to ~100KB/s

### Host-Side Example (Python)

```python
import pylink

jlink = pylink.JLink()
jlink.open()
jlink.connect('STM32F401RE')

# Send start command on channel 0
jlink.rtt_write(0, b'IMAGE:START\n')

# Stream image data on channel 2
with open('splash.rgb565', 'rb') as f:
    while chunk := f.read(1024):
        jlink.rtt_write(2, chunk)
        time.sleep(0.01)  # Throttle

# Send end command
jlink.rtt_write(0, b'IMAGE:END\n')
```

## Implementation Status

| Component | Status |
|-----------|--------|
| ST7789 initialization | *Scaffolded* |
| Basic drawing primitives | *Scaffolded* |
| Text rendering | *Scaffolded* |
| Menu system | *Not started* |
| Status display | *Not started* |
| Debug display | *Not started* |
| Image transfer | *Not started* |
| Joystick input | *Scaffolded* |

## SPI Bus Sharing

The display shares SPI2 with the NOR flash. Both use Mode 0, so no mode switching is required. The `SPIBus` class provides mutex-based locking to prevent conflicts.

**Important**: Always acquire the SPI mutex before display operations:

```cpp
if (s_spi->lock(pdMS_TO_TICKS(100))) {
    lcd.drawText(0, 0, "Hello");
    s_spi->unlock();
}
```

## References

- [ST7789 Datasheet](https://www.waveshare.com/w/upload/a/ae/ST7789_Datasheet.pdf)
- [X-NUCLEO-GFX01M2 User Manual](https://www.st.com/resource/en/user_manual/um2739-xnucleogfx01m2-graphics-expansion-board-stmicroelectronics.pdf)
