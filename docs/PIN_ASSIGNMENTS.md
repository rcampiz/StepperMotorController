# Pin Assignments

This document details all pin assignments for the NUCLEO-F401RE with expansion boards.

## Hardware Configuration

- **MCU:** STM32F401RET6 (64-pin LQFP)
- **Board:** NUCLEO-F401RE
- **Expansion 1:** X-NUCLEO-IHM03A1 (powerSTEP01 stepper driver)
- **Expansion 2:** X-NUCLEO-GFX01M2 (LCD, flash, joystick)
- **External:** Quadrature encoder with index

## Pin Summary Table

| Pin | Function | Peripheral | Notes |
|-----|----------|------------|-------|
| PA0 | Encoder A | TIM2_CH1 | AF1, quadrature input |
| PA1 | Encoder B | TIM2_CH2 | AF1, quadrature input |
| PA2 | UART TX | USART2_TX | AF7, VCP via ST-LINK |
| PA3 | UART RX | USART2_RX | AF7, VCP via ST-LINK |
| PA5 | SPI1 SCK | SPI1_SCK | AF5, shared bus |
| PA6 | SPI1 MISO | SPI1_MISO | AF5, shared bus |
| PA7 | SPI1 MOSI | SPI1_MOSI | AF5, shared bus |
| PA8 | LCD D/C | GPIO | Data/Command select |
| PA9 | Motor STBY/RST | GPIO | Active low |
| PA10 | Motor FLAG | GPIO | Active low, interrupt capable |
| PB4 | Motor BUSY | GPIO | Active low |
| PB6 | Joystick CENTER | GPIO | Active low, pull-up |
| PB8 | LCD CS | GPIO | **MODIFIED** from default |
| PB13 | SPI2 SCK | SPI2_SCK | AF5, flash bus |
| PB14 | SPI2 MISO | SPI2_MISO | AF5, flash bus |
| PB15 | SPI2 MOSI | SPI2_MOSI | AF5, flash bus |
| PC2 | Encoder nG | GPIO | Config pin |
| PC3 | Encoder G | GPIO | Config pin |
| PC4 | Encoder Z | GPIO/EXTI4 | Index pulse |
| PC6 | Flash CS | GPIO | NOR flash select |
| PC7 | Joystick LEFT | GPIO | Active low, pull-up |
| PC8 | Motor CS | GPIO | **MODIFIED** from default |
| PC9 | Joystick DOWN | GPIO | Active low, pull-up |
| PC10 | Joystick RIGHT | GPIO | Active low, pull-up |
| PC12 | Joystick UP | GPIO | Active low, pull-up |
| PC13 | User Button | GPIO | Blue button (directly active low) |
| PC15 | LCD TE | GPIO | Tearing effect output |
| PH0 | LCD NRESET | GPIO | Active low reset |

## Detailed Pin Configurations

### SPI1 Bus (Shared: Motor + LCD)

```
Namespace: Pins::SPI1_Bus
Port: GPIOA
Alternate Function: AF5

PA5 ------> SCK  (CN10-11)
PA6 <------ MISO (CN10-13)
PA7 ------> MOSI (CN10-15)
```

Both IHM03A1 (motor) and GFX01M2 (LCD) share SPI1.
Mutex protection required for RTOS-safe access.

### SPI2 Bus (Flash Only)

```
Namespace: Pins::SPI2_Bus
Port: GPIOB
Alternate Function: AF5

PB13 ------> SCK  (CN10-30)
PB14 <------ MISO (CN10-28)
PB15 ------> MOSI (CN10-26)
```

Dedicated to NOR flash on GFX01M2.

### X-NUCLEO-IHM03A1 (powerSTEP01)

```
Namespace: Pins::IHM03A1

PC8  ------> CS        **MODIFIED** (CN10-2, originally PB6)
PA9  ------> STBY/RST  (D8)
PA10 <------ FLAG      (D2, active low, interrupt capable)
PB4  <------ BUSY      (D5, active low)
```

**CS Pin Modification:**
The default IHM03A1 CS pin conflicts with joystick CENTER.
Reassigned to PC8 (CN10-2) via board modification.

### X-NUCLEO-GFX01M2 (LCD)

```
Namespace: Pins::GFX_LCD

PB8  ------> CS        **MODIFIED** (CN10-3, originally PA15)
PA8  ------> D/C       (D7, Data/Command)
PH0  ------> NRESET    (CN7-29, active low)
PC15 <------ TE        (CN7-27, tearing effect)
```

**CS Pin Modification:**
The default GFX01M2 SPIA_NCS conflicts with other signals.
Reassigned to PB8 (CN10-3) via board modification.

### X-NUCLEO-GFX01M2 (NOR Flash)

```
Namespace: Pins::GFX_Flash

PC6 ------> CS (CN10-4)
```

Uses SPI2 bus (PB13/PB14/PB15).

### X-NUCLEO-GFX01M2 (Joystick)

```
Namespace: Pins::Joystick
All pins: Active low with internal pull-ups

PC7  <------ LEFT   (D9)
PB6  <------ CENTER (D10)
PC9  <------ DOWN   (CN10-1)
PC10 <------ RIGHT  (CN7-1)
PC12 <------ UP     (CN7-3)
```

### Encoder Interface

```
Namespace: Pins::Encoder

PA0 <------ EA (Quadrature A, TIM2_CH1, AF1)
PA1 <------ EB (Quadrature B, TIM2_CH2, AF1)
PC4 <------ EZ (Index pulse, EXTI4)
PC2 ------> nG (Config, directly active low)
PC3 ------> G  (Config)
```

**Timer Configuration:**
- TIM2 in encoder mode 3 (count on both edges)
- 32-bit counter (full range)
- AF1 for PA0/PA1

**Index Pulse:**
- PC4 configured as EXTI4
- Falling edge trigger (active low)
- ISR sets `indexSeen` flag

### VCP UART (Debug/Control)

```
Namespace: Pins::VCP_UART

PA2 ------> TX (to ST-LINK RX via SB13)
PA3 <------ RX (from ST-LINK TX via SB14)
```

**Configuration:**
- USART2, AF7
- 115200 baud default
- 8N1

**ST-LINK Connection (per UM1724):**
The on-board ST-LINK/V2-1 provides VCP via solder bridges:
- SB13 connects PA2 to ST-LINK USART RX
- SB14 connects PA3 to ST-LINK USART TX

### Onboard Resources

```
Namespace: Pins::Onboard

PA5  ------> LD2 (User LED, shares with SPI1 SCK!)
PC13 <------ B1  (User button, directly active low)
```

**Warning:** PA5 is shared between the user LED and SPI1 SCK.
When SPI1 is active, the LED will flicker with clock activity.
Consider using a different GPIO for LED indication.

## Conflict Resolution

### Resolved Conflicts

| Original Pin | Conflict | Resolution |
|--------------|----------|------------|
| PB6 | IHM03A1 CS vs Joystick CENTER | Motor CS -> PC8 |
| PA15 | GFX01M2 SPIA_NCS vs JTDI | LCD CS -> PB8 |
| PA5 | SPI1 SCK vs User LED | Accepted (LED flickers during SPI) |

### Available Pins

Pins not used by current configuration:
- PA4, PA11, PA12, PA15
- PB0, PB1, PB2, PB3, PB5, PB7, PB9, PB10, PB11, PB12
- PC0, PC1, PC5, PC11, PC14
- PH1

## Code Reference

All pin definitions are in `Core/Inc/board/board_pins.hpp`:

```cpp
namespace Pins {
    namespace SPI1_Bus { ... }
    namespace SPI2_Bus { ... }
    namespace IHM03A1 { ... }
    namespace GFX_LCD { ... }
    namespace GFX_Flash { ... }
    namespace Joystick { ... }
    namespace Encoder { ... }
    namespace VCP_UART { ... }
    namespace Onboard { ... }
}
```

Usage example:
```cpp
#include "board/board_pins.hpp"

// Configure motor CS as output
Pins::IHM03A1::CS_PORT->MODER |= (1 << (Pins::IHM03A1::CS_PIN * 2));

// Read encoder count
int32_t count = Pins::Encoder::TIMER->CNT;

// Check joystick
bool pressed = !(Pins::Joystick::CENTER_PORT->IDR & (1 << Pins::Joystick::CENTER_PIN));
```
