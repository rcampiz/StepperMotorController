# Pin Assignments

Every pin the firmware drives or reads on the NUCLEO-F401RE with its two expansion boards
and the encoder. The source of truth is `server/layers/L5_board/board_pins.hpp`; this page
is that header in table form plus the board reworks it relies on. Updated 2026-10-18 to
match the header (the previous version described an earlier routing).

## Hardware Configuration

- **MCU:** STM32F401RET6 (64-pin LQFP) on NUCLEO-F401RE
- **Expansion 1:** X-NUCLEO-IHM03A1 (powerSTEP01 stepper driver), SPI1
- **Expansion 2:** X-NUCLEO-GFX01M2 (ST7789 LCD on SPI1, NOR flash on SPI2, 5-way joystick)
- **External:** StepperOnline 23HS22-2804-ME1K: 1000-line quadrature encoder with index
  (4000 counts per revolution after x4 decoding), AM26LV32 differential receiver board
- **Supply:** motor supply on the IHM03A1 screw terminals (12-24 V on the bench)

## Pin Summary Table

| Pin | Function | Peripheral | Header | Notes |
|-----|----------|------------|--------|-------|
| PA0 | LCD TE | GPIO input | A0 | Tearing-effect output of the ST7789 |
| PA1 | LCD NRESET | GPIO output | A1 | Active low |
| PA2 | UART TX | USART2_TX, AF7 | VCP | To ST-LINK via SB13 |
| PA3 | UART RX | USART2_RX, AF7 | VCP | From ST-LINK via SB14 |
| PA5 | SPI1 SCK | SPI1_SCK, AF5 | D13, CN10-11 | Shared bus; also the user LED LD2 |
| PA6 | SPI1 MISO | SPI1_MISO, AF5 | D12, CN10-13 | Shared bus |
| PA7 | SPI1 MOSI | SPI1_MOSI, AF5 | D11, CN10-15 | Shared bus |
| PA8 | Flash CS | GPIO output | D7, CN10-23 | GFX01M2 SPIB_NCS |
| PA9 | Motor STBY/RESET | GPIO output | D8 | Active low; pulled low by the 0x18 out-of-band stop |
| PA10 | Motor FLAG | GPIO input | D2 | Active low, interrupt capable |
| PA12 | Joystick CENTER | GPIO input, pull-up | CN10-12 | Active low |
| PB0 | Joystick RIGHT | GPIO input, pull-up | A3 | Active low |
| PB4 | Joystick DOWN | GPIO input, pull-up | D5 | Active low |
| PB5 | Motor BUSY/SYNC | GPIO input | D4 | Active low; the chip can emit step pulses here instead (SYNC_SEL) |
| PB6 | Encoder A | TIM4_CH1, AF2 | D10, CN10-17 | Quadrature |
| PB7 | Encoder B | TIM4_CH2, AF2 | CN7-21 | Quadrature |
| PB10 | LCD D/C | GPIO output | D6, CN10-25 | Data/command select (SPIA_DCX) |
| PB13 | SPI2 SCK | SPI2_SCK, AF5 | CN10-30 | Flash bus |
| PC0 | Joystick UP | GPIO input, pull-up | A5 | Active low |
| PC2 | SPI2 MISO | SPI2_MISO, AF5 | CN7-35 | Flash bus (GFX01M2 SPIB_MISO) |
| PC3 | SPI2 MOSI | SPI2_MOSI, AF5 | CN7-37 | Flash bus, rerouted (see reworks) |
| PC5 | Joystick LEFT | GPIO input, pull-up | CN10-6 | Active low |
| PC6 | LCD CS | GPIO output | CN10-4 | GFX01M2 SPIA_NCS |
| PC8 | Motor CS | GPIO output | CN10-2 | Rework: originally PB6 |
| PC9 | Encoder Z | GPIO input, EXTI9 | CN10-1 | Index pulse (EXTI9_5 interrupt) |
| PC13 | User button B1 | GPIO input | on board | Active low |

Timers and peripherals in use: TIM4 (encoder, 16-bit), TIM5 (microsecond clock), SPI1,
SPI2, USART2, EXTI9 (index), DMA (encoder sampler).

## Detailed Pin Configurations

### SPI1 Bus (Shared: Motor + LCD)

```
Namespace: Pins::SPI1_Bus
Port: GPIOA, AF5

PA5 ------> SCK  (CN10-11)
PA6 <------ MISO (CN10-13)
PA7 ------> MOSI (CN10-15)
```

Both the IHM03A1 (powerSTEP01, 5 MHz max) and the GFX01M2 LCD share SPI1 at 2.625 MHz
(84 MHz / 32). The SPI manager serialises access with a mutex; bitmap streams lock the bus
per chunk so the Motor task keeps its 50 ms passes.

### SPI2 Bus (Flash only)

```
Namespace: Pins::SPI2_Bus
AF5

PB13 ------> SCK  (CN10-30)
PC2  <------ MISO (CN7-35, GFX01M2 SPIB_MISO)
PC3  ------> MOSI (CN7-37, rerouted)
```

Dedicated to the NOR flash on the GFX01M2 (APB1, 42 MHz clock domain). The GFX01M2's
default SPIB_MOSI pin (PB5) is cut and MOSI runs to PC3, which is the SPI2_MOSI alternate
(see reworks).

### X-NUCLEO-IHM03A1 (powerSTEP01)

```
Namespace: Pins::IHM03A1

PC8  ------> CS         (CN10-2; rework, originally PB6)
PA9  ------> STBY/RESET (D8, active low)
PA10 <------ FLAG       (D2, active low, interrupt capable)
PB5  <------ BUSY/SYNC  (D4, active low)
```

STBY/RESET is driven low directly from the USART2 interrupt on the out-of-band ESTOP byte
(0x18), which turns the bridges off within microseconds; the shield has no pull on the
line, only C6 (100 nF), so the firmware drives it actively (UM1910 figure 3).

BUSY/SYNC: the powerSTEP01 puts either its busy flag or a step-synchronous pulse train on
this pin (STEP_MODE register, SYNC_SEL bits). The firmware reads the busy state from the
status register over SPI and does not depend on the pin level, so the pin is free to be
switched to SYNC and fed to TIM3_CH2 (AF2) as a hardware step counter for a kilohertz
capture of the commanded position. No wiring change is needed for that.

### X-NUCLEO-GFX01M2 (LCD)

```
Namespace: Pins::GFX_LCD

PC6  ------> CS      (CN10-4, SPIA_NCS)
PB10 ------> D/C     (D6, CN10-25, SPIA_DCX)
PA1  ------> NRESET  (A1, active low)
PA0  <------ TE      (A0, tearing effect)
```

Uses SPI1. TE drives the frame-synchronised writes (`ITearingSync`).

### X-NUCLEO-GFX01M2 (NOR Flash)

```
Namespace: Pins::GFX_Flash

PA8 ------> CS (D7, CN10-23, SPIB_NCS)
```

Uses SPI2 (PB13 / PC2 / PC3). Holds the image slots and, in sectors 0x000000-0x001FFF,
the device identity (DEV:ID / DEV:ROLE).

### X-NUCLEO-GFX01M2 (Joystick)

```
Namespace: Pins::Joystick
All pins: active low, internal pull-ups

PC5  <------ LEFT   (CN10-6)
PA12 <------ CENTER (CN10-12)
PB4  <------ DOWN   (D5)
PB0  <------ RIGHT  (A3)
PC0  <------ UP     (A5)
```

### Encoder Interface

```
Namespace: Pins::Encoder

PB6 <------ EA (quadrature A, TIM4_CH1, AF2, CN10-17)
PB7 <------ EB (quadrature B, TIM4_CH2, AF2, CN7-21)
PC9 <------ EZ (index pulse, EXTI9, CN10-1)
```

**Timer configuration:**
- TIM4 in encoder mode 3 (count on both edges of A and B): 4000 counts per revolution
  with the 1000-line encoder
- 16-bit counter; the Encoder service extends it to 32 bits in software
- Sampled by DMA at 1 kHz by default (`DRV:ENC:...` config, 100 Hz to 10 kHz)

**Index pulse:**
- PC9 on EXTI line 9 (EXTI9_5 interrupt, priority 5, deliberately not 6; see open issue 14)
- Falling edge; the ISR records the tick and sets `indexSeen`

**Receiver enables:** the AM26LV32 board's nG and G inputs were routed to PA2/PA3, which
are the UART pins. Those traces are cut on the encoder board and the on-board pulls keep
the receiver enabled; the STM32 drives nothing for them.

### VCP UART (protocol and early debug)

```
Namespace: Pins::VCP_UART

PA2 ------> TX (to ST-LINK RX via SB13)
PA3 <------ RX (from ST-LINK TX via SB14)
```

USART2, AF7, 115200 8N1 by default (`SYST:BAUD` switches, reverting if no valid command
follows). The early-debug printer uses the same peripheral bare-metal until the scheduler
starts, after which it is retired (`Platform::retireDebugOutput`). The RX interrupt
recognises the 0x18 out-of-band stop byte before the byte reaches the ring.

### Onboard Resources

```
Namespace: Pins::Onboard

PA5  ------> LD2 (user LED, shares PA5 with SPI1 SCK)
PC13 <------ B1  (user button, active low)
```

The LED flickers with SPI1 clock activity; accepted.

## Board Reworks

The routing above depends on these physical modifications. Do not undo them without
changing `board_pins.hpp`.

| Where | Change | Why |
|-------|--------|-----|
| IHM03A1 | Chip select moved from PB6 (D10) to PC8 (CN10-2) | Frees PB6 for encoder A (TIM4_CH1) |
| GFX01M2 | SPIB_MOSI trace to PB5 cut; SPI2 MOSI rerouted to PC3 (CN7-37) | Frees PB5 (D4) for the powerSTEP01 BUSY/SYNC line |
| GFX01M2 | SPIB_MISO on PC2 (CN7-35), SPIB_NCS on PA8 (D7), SPIA_NCS on PC6 (CN10-4) | SPI2 and chip selects clear of the motor and encoder pins |
| Encoder board | nG and G traces to PA2/PA3 cut | PA2/PA3 are the ST-LINK VCP UART; the receiver stays enabled by its pulls |
| Nucleo | SB13/SB14 left closed | USART2 on the ST-LINK virtual COM port |

## Available Pins

Not used by the current configuration (check the Morpho connector and the expansion
boards' own use before claiming one):

- PA4, PA11, PA13, PA14, PA15
- PB1, PB2, PB3, PB8, PB9, PB11, PB12, PB14, PB15
- PC1, PC4, PC7, PC10, PC11, PC12, PC14, PC15
- PH0, PH1

PB14 and PB15 are the SPI2 default MISO/MOSI pins and are free only because of the GFX01M2
reroute; PA13/PA14 are SWD and stay reserved for the debugger.

## Code Reference

All pin definitions are in `server/layers/L5_board/board_pins.hpp`:

```cpp
namespace Board::Pins {
    namespace SPI1_Bus { ... }
    namespace SPI2_Bus { ... }
    namespace IHM03A1  { ... }
    namespace GFX_LCD  { ... }
    namespace GFX_Flash { ... }
    namespace Joystick { ... }
    namespace Encoder  { ... }
    namespace Onboard  { ... }
    namespace VCP_UART { ... }
}
```

Only L5 (`server/layers/L5_board/`) includes this header; the layers above reach the
hardware through the L4/L5 boundary interfaces (`IGPIO`, `IEncoderSampler`, `ITransport`
and so on), never through pin numbers. The `DBG:BOARD` dump prints the live GPIO input
registers with these pin names for bench checks.
