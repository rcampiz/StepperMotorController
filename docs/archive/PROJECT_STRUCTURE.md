# Project Structure

This document describes the reorganized project structure for the STM32F401RE Stepper Motor Controller.

## Directory Layout

```
StepperMotorController/
+-- Core/
|   +-- Inc/
|   |   +-- board/                    # Board-specific pin definitions
|   |   |   +-- board_pins.hpp        # Canonical pin mappings for all hardware
|   |   +-- comms/                    # Communication layer
|   |   |   +-- transport_interface.hpp   # Abstract transport base class
|   |   |   +-- uart_transport.hpp        # USART2 VCP implementation
|   |   |   +-- rtt_transport.hpp         # SEGGER RTT implementation
|   |   |   +-- command_parser.hpp        # ASCII protocol parser
|   |   |   +-- telemetry.hpp             # Shared telemetry structures
|   |   +-- drivers/                  # Hardware drivers
|   |   |   +-- powerstep01.hpp       # powerSTEP01 stepper driver (IHM03A1)
|   |   |   +-- lcd_st7789.hpp        # ST7789 LCD driver (GFX01M2)
|   |   |   +-- flash_nor.hpp         # SPI NOR flash driver (GFX01M2)
|   |   |   +-- joystick.hpp          # 5-way joystick driver (GFX01M2)
|   |   |   +-- encoder.hpp           # Quadrature encoder driver
|   |   |   +-- spi_bus.hpp           # RTOS-safe SPI bus driver
|   |   +-- tasks/                    # FreeRTOS task headers
|   |   |   +-- motor_task.hpp        # Motor control task
|   |   |   +-- encoder_task.hpp      # Encoder reading task
|   |   |   +-- display_task.hpp      # LCD display task
|   |   |   +-- comms_task.hpp        # Command/telemetry task
|   |   |   +-- bringup_task.hpp      # Hardware bringup task
|   |   +-- services/                 # System services
|   |   |   +-- tick_timer.hpp        # Microsecond tick counter (TIM5)
|   |   |   +-- command_queue.hpp     # Thread-safe command queue
|   |   |   +-- device_config.hpp     # Persistent device configuration
|   |   |   +-- control_mode.hpp      # Control mode management
|   |   +-- ui/                       # UI framework
|   |   |   +-- ui_mode.hpp           # LOCAL/REMOTE mode manager
|   |   |   +-- screen.hpp            # IScreen interface
|   |   |   +-- menu_screen.hpp       # Menu navigation screen
|   |   |   +-- terminal_screen.hpp   # Scrolling text console
|   |   +-- FreeRTOSConfig.h          # FreeRTOS configuration
|   |   +-- [other peripheral headers]
|   +-- Src/
|       +-- comms/                    # Communication implementations
|       |   +-- uart_transport.cpp
|       |   +-- rtt_transport.cpp
|       |   +-- command_parser.cpp
|       |   +-- telemetry.cpp
|       +-- tasks/                    # Task implementations
|       |   +-- motor_task.cpp
|       |   +-- encoder_task.cpp
|       |   +-- display_task.cpp
|       |   +-- comms_task.cpp
|       |   +-- bringup_task.cpp
|       +-- services/                 # Service implementations
|       |   +-- tick_timer.cpp
|       |   +-- command_queue.cpp
|       |   +-- device_config.cpp
|       |   +-- control_mode.cpp
|       +-- ui/                       # UI implementations
|       |   +-- ui_mode.cpp
|       |   +-- menu_screen.cpp
|       |   +-- terminal_screen.cpp
|       +-- main.cpp                  # Application entry point
|       +-- [other source files]
+-- Drivers/
|   +-- CMSIS/                        # ARM CMSIS headers
|   +-- STM32F4xx_HAL_Driver/         # ST HAL library
+-- Middlewares/
|   +-- Third_Party/
|   |   +-- FreeRTOS-Kernel/          # FreeRTOS kernel
|   +-- SEGGER/                       # SEGGER tools (user must download)
|       +-- RTT/                      # Real-Time Transfer
|       +-- SystemView/               # SystemView trace
+-- docs/                             # Documentation
+-- CMakeLists.txt                    # CMake build configuration
+-- compile_commands.json             # clangd configuration
+-- STM32F401RETx_FLASH.ld            # Linker script
```

## Design Rationale

### Why Reorganize?

The original structure had drivers scattered across multiple locations:
- `Drivers/encoder/`
- `Drivers/gfx01m2/`
- `Drivers/ihm03a1/`
- `Drivers/spi_bus.hpp`
- `tasks/` (root level)

The new structure consolidates everything under `Core/` for several reasons:

1. **Consistency with STM32CubeMX projects** - CubeMX generates code in `Core/Inc` and `Core/Src`
2. **Cleaner include paths** - All application code uses `Core/Inc` as the base
3. **Separation of concerns** - Clear distinction between board config, drivers, tasks, and comms
4. **Easier navigation** - Related files are grouped together

### Include Path Strategy

All includes are relative to `Core/Inc`:
```cpp
#include "board/board_pins.hpp"      // Pin definitions
#include "drivers/powerstep01.hpp"   // Hardware drivers
#include "tasks/motor_task.hpp"      // Task interfaces
#include "comms/telemetry.hpp"       // Communication layer
```

This is configured in `CMakeLists.txt`:
```cmake
include_directories(
    Core/Inc
    Core/Inc/board
    Core/Inc/comms
    Core/Inc/drivers
    Core/Inc/tasks
    ...
)
```

## File Descriptions

### Board Configuration

| File | Purpose |
|------|---------|
| `board/board_pins.hpp` | All pin definitions in namespaced constants |

### Drivers

| File | Purpose |
|------|---------|
| `drivers/powerstep01.hpp` | powerSTEP01 stepper motor driver via SPI |
| `drivers/lcd_st7789.hpp` | ST7789 240x240 LCD driver |
| `drivers/flash_nor.hpp` | SPI NOR flash for storage |
| `drivers/joystick.hpp` | 5-way joystick input |
| `drivers/encoder.hpp` | Quadrature encoder with optional index pulse |
| `drivers/spi_bus.hpp` | RTOS-safe SPI bus with mutex |

### Tasks

| File | Purpose | Priority |
|------|---------|----------|
| `tasks/motor_task.hpp` | Motor command processing | 4 (highest) |
| `tasks/comms_task.hpp` | Raspberry Pi communication | 3 |
| `tasks/encoder_task.hpp` | Encoder sampling at 100Hz | 2 |
| `tasks/display_task.hpp` | LCD refresh at 10Hz | 1 (lowest) |

### Communication

| File | Purpose |
|------|---------|
| `comms/transport_interface.hpp` | Abstract `ITransport` base class |
| `comms/uart_transport.hpp` | USART2 VCP transport |
| `comms/rtt_transport.hpp` | SEGGER RTT transport |
| `comms/command_parser.hpp` | ASCII command protocol |
| `comms/telemetry.hpp` | Thread-safe telemetry manager |

### Services

| File | Purpose |
|------|---------|
| `services/tick_timer.hpp` | High-resolution microsecond tick counter using TIM5 |
| `services/command_queue.hpp` | Thread-safe command queue for inter-task communication |
| `services/device_config.hpp` | Persistent device configuration in SPI NOR flash |
| `services/control_mode.hpp` | Control mode (OPEN_LOOP/CLOSED_LOOP) and encoder status management |

### UI

| File | Purpose |
|------|---------|
| `ui/ui_mode.hpp` | UI mode enum (LOCAL/REMOTE) and mode manager |
| `ui/screen.hpp` | IScreen interface for screen abstractions |
| `ui/menu_screen.hpp` | List-based menu navigation screen |
| `ui/terminal_screen.hpp` | Scrolling text console screen |

## Migration Notes

If you had code that included the old paths, update as follows:

| Old Include | New Include |
|-------------|-------------|
| `#include "board_pins.hpp"` | `#include "board/board_pins.hpp"` |
| `#include "../spi_bus.hpp"` | `#include "drivers/spi_bus.hpp"` |
| `#include "powerstep01.hpp"` | `#include "drivers/powerstep01.hpp"` |
