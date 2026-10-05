# Display Architecture

This document describes the display subsystem for the ST7789-based LCD on the X-NUCLEO-GFX01M2 expansion board.

## Hardware

- **Display**: ST7789 240x240 RGB LCD
- **Interface**: SPI1 (shared with powerSTEP01 motor driver)
- **SPI Mode**: Mode 0 (CPOL=0, CPHA=0) - LCD switches mode dynamically
- **Joystick**: 5-way (Up, Down, Left, Right, Center) for navigation

## Dual-Mode UI Architecture

The display operates in one of two primary modes:

| Mode | Description |
|------|-------------|
| `LOCAL` | MCU owns UI state machine; joystick navigates local screens |
| `REMOTE` | Host controls display via commands; joystick events forwarded upstream |

### Mode Switching

Mode transitions are triggered by:
1. Host command (`UI_MODE LOCAL` or `UI_MODE REMOTE`)
2. Default mode at startup is `LOCAL`

When switching modes:
- Screen content is not cleared automatically
- In `REMOTE` mode, local page rendering is suspended
- Joystick events are forwarded upstream instead of local navigation

## LOCAL Mode

In LOCAL mode, the MCU manages the display through a screen abstraction layer.

### Screen Types

| Type | Description |
|------|-------------|
| `STATUS` | Telemetry display (position, speed, encoder) |
| `MENU` | List-based navigation with selection |
| `TERMINAL` | Scrolling text console |
| `MOTOR_DETAIL` | Detailed motor information |
| `ENCODER_DETAIL` | Detailed encoder information |
| `SYSTEM` | System info (uptime, heap, CPU load) |
| `DEBUG` | Debug/log output |

### Joystick Navigation (LOCAL Mode)

| Input | Action |
|-------|--------|
| Left | Previous page / back in menu |
| Right | Next page / enter submenu |
| Up | Previous menu item / scroll up |
| Down | Next menu item / scroll down |
| Center | Select / refresh |

### Display Pages

The default LOCAL mode cycles through these pages:

```
Status -> MotorDetail -> EncoderDetail -> System -> Debug
  ^                                                   |
  +---------------------------------------------------+
```

### Status Page Layout

```
+------------------------+
| STATUS                 |
+------------------------+
| Position:        12345 |
| Speed:            1200 |
| State:          Moving |
+------------------------+
| Encoder:         12340 |
| Velocity:         1198 |
| Index:             Yes |
+------------------------+
```

## REMOTE Mode

In REMOTE mode, the host has full control over display content via commands.

### Remote Rendering Commands

| Command | Format | Description |
|---------|--------|-------------|
| `DISP_CLEAR` | `DISP_CLEAR [color]` | Clear display (RGB565 hex) |
| `DISP_TEXT` | `DISP_TEXT <x> <y> <fg> <bg> <text>` | Draw text |
| `DISP_RECT` | `DISP_RECT <x> <y> <w> <h> <color> [fill]` | Draw rectangle |
| `DISP_LINE` | `DISP_LINE <x1> <y1> <x2> <y2> <color>` | Draw line |
| `DISP_BITMAP_B64` | `DISP_BITMAP_B64 <x> <y> <w> <h> <base64>` | Draw bitmap |

All commands require REMOTE mode to be active. Colors are RGB565 format in hexadecimal.

### Joystick Event Forwarding

In REMOTE mode, joystick events are sent upstream:

```
EVENT JOY <direction> <pressed|released>
```

Examples:
```
EVENT JOY LEFT pressed
EVENT JOY LEFT released
EVENT JOY CENTER pressed
```

Directions: `NONE`, `LEFT`, `RIGHT`, `UP`, `DOWN`, `CENTER`

### Remote Rendering Example (Python)

```python
import serial

ser = serial.Serial('COM3', 115200)

# Switch to REMOTE mode
ser.write(b'UI_MODE REMOTE\n')
print(ser.readline())  # OK REMOTE

# Clear screen to blue
ser.write(b'DISP_CLEAR 001F\n')
print(ser.readline())  # OK

# Draw white text
ser.write(b'DISP_TEXT 10 10 FFFF 001F Hello\n')
print(ser.readline())  # OK

# Draw red rectangle
ser.write(b'DISP_RECT 50 50 100 100 F800 fill\n')
print(ser.readline())  # OK
```

## Screen Abstraction Layer

### IScreen Interface

All screens implement the `IScreen` interface:

```cpp
class IScreen {
public:
    virtual ScreenType getType() const = 0;
    virtual void render(LCD& lcd) = 0;
    virtual InputResult handleInput(JoyDirection dir, bool pressed) = 0;
    virtual void onActivate() {}
    virtual void onDeactivate() {}
    virtual bool needsFullRedraw() const { return false; }
    virtual void clearRedrawFlag() {}
};
```

### InputResult Values

| Result | Meaning |
|--------|---------|
| `HANDLED` | Input consumed by screen |
| `UNHANDLED` | Input should be handled by parent |
| `EXIT_SCREEN` | Screen requests to close |
| `SWITCH_SCREEN` | Screen requests switching to another |

### MenuScreen

List-based navigation screen with:
- Up to 16 menu items
- Scrolling when items exceed visible area
- Selection highlight
- Item enable/disable
- Callback on selection

```cpp
MenuScreen menu("Settings");
menu.addItem("Motor Settings", onMotorSettings);
menu.addItem("Encoder Settings", onEncoderSettings);
menu.addItem("System Info", onSystemInfo);
```

### TerminalScreen

Scrolling text console with:
- 20-line circular buffer
- 40 characters per line
- Auto-scroll to new content
- Manual scroll with Up/Down
- Jump to bottom with Center

```cpp
TerminalScreen terminal("Debug Log");
terminal.println("System started");
terminal.printf("Heap: %lu bytes", freeHeap);
```

## LCD Driver

### Drawing Primitives

| Method | Description |
|--------|-------------|
| `fillScreen(color)` | Fill entire screen |
| `fillRect(x, y, w, h, color)` | Filled rectangle |
| `drawRect(x, y, w, h, color)` | Rectangle outline |
| `drawHLine(x, y, w, color)` | Horizontal line |
| `drawVLine(x, y, h, color)` | Vertical line |
| `drawLine(x0, y0, x1, y1, color)` | Arbitrary line (Bresenham) |
| `drawString(x, y, text, fg, bg)` | Text with colors |
| `drawInt(x, y, value, width, fg, bg)` | Signed integer |
| `drawUInt(x, y, value, width, fg, bg)` | Unsigned integer |
| `drawBitmap(x, y, w, h, data)` | RGB565 bitmap (16-bit array) |
| `drawBitmapRaw(x, y, w, h, data, len)` | RGB565 bitmap (byte array) |

### Bitmap Streaming

For large images, use streaming to avoid buffering:

```cpp
lcd.streamBitmapStart(x, y, w, h);
while (hasData) {
    lcd.streamBitmapData(chunk, chunkLen);
}
lcd.streamBitmapEnd();
```

### Color Constants

| Name | RGB565 Value | Color |
|------|--------------|-------|
| `BLACK` | 0x0000 | Black |
| `WHITE` | 0xFFFF | White |
| `RED` | 0xF800 | Red |
| `GREEN` | 0x07E0 | Green |
| `BLUE` | 0x001F | Blue |
| `CYAN` | 0x07FF | Cyan |
| `YELLOW` | 0xFFE0 | Yellow |
| `GRAY` | 0x8410 | Gray |

## SPI Bus Sharing

The display shares SPI1 with the powerSTEP01 motor driver. The `SPIBus` class handles:
- Mutex-based locking for thread safety
- Dynamic mode switching (Mode 0 for LCD, Mode 3 for motor)

```cpp
// LCD driver handles mode switching automatically
lcd.fillScreen(LCD::BLACK);  // Acquires SPI, sets Mode 0, draws, releases
```

## Memory Considerations

| Resource | Allocation |
|----------|------------|
| Menu items | 16 items x 32 bytes = 512 bytes |
| Terminal buffer | 20 lines x 40 chars = 800 bytes |
| Remote bitmap decode | 512 bytes max (base64 input) |
| No frame buffer | Streaming directly to LCD |

## Implementation Status

| Component | Status |
|-----------|--------|
| ST7789 initialization | **Complete** |
| Basic drawing primitives | **Complete** |
| Text rendering (8x8 font) | **Complete** |
| Line drawing (Bresenham) | **Complete** |
| Bitmap rendering | **Complete** |
| Bitmap streaming | **Complete** |
| UI mode manager | **Complete** |
| LOCAL mode pages | **Complete** (Status, Motor, Encoder, System, Debug) |
| REMOTE mode rendering | **Complete** |
| Joystick event forwarding | **Complete** |
| IScreen interface | **Complete** |
| MenuScreen | **Complete** |
| TerminalScreen | **Complete** |
| Joystick input handling | **Complete** |

## Files

| File | Purpose |
|------|---------|
| `Core/Inc/drivers/lcd_st7789.hpp` | LCD driver with all drawing primitives |
| `Core/Inc/ui/ui_mode.hpp` | UI mode enum and manager |
| `Core/Inc/ui/screen.hpp` | IScreen interface |
| `Core/Inc/ui/menu_screen.hpp` | MenuScreen class |
| `Core/Inc/ui/terminal_screen.hpp` | TerminalScreen class |
| `Core/Src/ui/ui_mode.cpp` | UI mode manager implementation |
| `Core/Src/ui/menu_screen.cpp` | MenuScreen implementation |
| `Core/Src/ui/terminal_screen.cpp` | TerminalScreen implementation |
| `Core/Inc/tasks/display_task.hpp` | Display task interface |
| `Core/Src/tasks/display_task.cpp` | Display task implementation |

## References

- [ST7789 Datasheet](https://www.waveshare.com/w/upload/a/ae/ST7789_Datasheet.pdf)
- [X-NUCLEO-GFX01M2 User Manual](https://www.st.com/resource/en/user_manual/um2739-xnucleogfx01m2-graphics-expansion-board-stmicroelectronics.pdf)
