/**
 * @file board_pins.hpp
 * @brief Consolidated pin definitions for NUCLEO-F401RE with expansion boards
 *
 * Hardware configuration:
 *   - X-NUCLEO-IHM03A1 (powerSTEP01 stepper driver) on SPI1
 *   - X-NUCLEO-GFX01M2 (LCD on SPI1, NOR flash on SPI2, joystick GPIOs)
 *
 * Pin assignments based on ST-Morpho connector mapping (UM1724 Table 29)
 * with user-modified routing as documented below.
 */

#ifndef BOARD_PINS_HPP
#define BOARD_PINS_HPP

/**
 * @note clangd errors expected until compile_commands.json is generated.
 *       Actual build uses ARM GCC toolchain which resolves all includes.
 */
#include "stm32f401xe.h"

namespace Pins {

// ============================================================================
// SPI1 - Shared bus for IHM03A1 (powerSTEP01) and GFX01M2 LCD
// ============================================================================
namespace SPI1_Bus {
    static GPIO_TypeDef* const PORT = GPIOA;
    static constexpr uint8_t SCK_PIN  = 5;   // PA5 - CN10-11
    static constexpr uint8_t MISO_PIN = 6;   // PA6 - CN10-13
    static constexpr uint8_t MOSI_PIN = 7;   // PA7 - CN10-15
    static constexpr uint8_t AF = 5;         // AF5 for SPI1
}

// ============================================================================
// SPI2 - GFX01M2 NOR Flash (SPIB)
// ============================================================================
namespace SPI2_Bus {
    static GPIO_TypeDef* const PORT = GPIOB;
    static constexpr uint8_t SCK_PIN  = 13;  // PB13 - CN10-30
    static constexpr uint8_t MISO_PIN = 14;  // PB14 - CN10-28
    static constexpr uint8_t MOSI_PIN = 15;  // PB15 - CN10-26
    static constexpr uint8_t AF = 5;         // AF5 for SPI2
}

// ============================================================================
// X-NUCLEO-IHM03A1 (powerSTEP01 Stepper Driver)
// ============================================================================
namespace IHM03A1 {
    // Chip select - PC8 (CN10-P2, rework from default PB6 to free encoder/joystick)
    static GPIO_TypeDef* const CS_PORT = GPIOC;
    static constexpr uint8_t CS_PIN = 8;

    // Directly uses SPI1_Bus for SCK/MISO/MOSI

    // Status/control pins (directly from powerSTEP01)
    // STBY/RESET active low - directly from board
    static GPIO_TypeDef* const STBY_RST_PORT = GPIOA;
    static constexpr uint8_t STBY_RST_PIN = 9;   // D8 - directly from board

    // FLAG output (active low, directly from board interrupt capable)
    static GPIO_TypeDef* const FLAG_PORT = GPIOA;
    static constexpr uint8_t FLAG_PIN = 10;      // D2

    // BUSY output (directly from powerSTEP01, active low)
    static GPIO_TypeDef* const BUSY_PORT = GPIOB;
    static constexpr uint8_t BUSY_PIN = 4;       // D5
}

// ============================================================================
// X-NUCLEO-GFX01M2 - LCD (SPI Display)
// ============================================================================
namespace GFX_LCD {
    // Chip select - SPIA_NCS on PC6 (CN10-4)
    static GPIO_TypeDef* const CS_PORT = GPIOC;
    static constexpr uint8_t CS_PIN = 6;

    // Display Tearing Effect output - PC15 (CN7-28)
    static GPIO_TypeDef* const TE_PORT = GPIOC;
    static constexpr uint8_t TE_PIN = 15;

    // Display Reset (active low) - PH1 (CN7-30)
    static GPIO_TypeDef* const NRESET_PORT = GPIOH;
    static constexpr uint8_t NRESET_PIN = 1;

    // Data/Command select - SPIA_DCX on PB10 (CN10-25)
    static GPIO_TypeDef* const DC_PORT = GPIOB;
    static constexpr uint8_t DC_PIN = 10;

    // Directly uses SPI1_Bus for SCK/MISO/MOSI
}

// ============================================================================
// X-NUCLEO-GFX01M2 - NOR Flash (SPIB)
// ============================================================================
namespace GFX_Flash {
    // Chip select for flash - SPIB_NCS on PA8 (CN10-23)
    static GPIO_TypeDef* const CS_PORT = GPIOA;
    static constexpr uint8_t CS_PIN = 8;

    // Directly uses SPI2_Bus for SCK/MISO/MOSI
}

// ============================================================================
// X-NUCLEO-GFX01M2 - Joystick (5-way)
// Active low inputs with internal pull-ups
// ============================================================================
namespace Joystick {
    // KEY_LEFT - PC7 (CN10 area, D9)
    static GPIO_TypeDef* const LEFT_PORT = GPIOC;
    static constexpr uint8_t LEFT_PIN = 7;

    // KEY_CENTER - PB6 (CN10 area, D10)
    static GPIO_TypeDef* const CENTER_PORT = GPIOB;
    static constexpr uint8_t CENTER_PIN = 6;

    // KEY_DOWN - PC9 (CN10-1)
    static GPIO_TypeDef* const DOWN_PORT = GPIOC;
    static constexpr uint8_t DOWN_PIN = 9;

    // KEY_RIGHT - PC10 (CN7-1)
    static GPIO_TypeDef* const RIGHT_PORT = GPIOC;
    static constexpr uint8_t RIGHT_PIN = 10;

    // KEY_UP - PC12 (CN7-3)
    static GPIO_TypeDef* const UP_PORT = GPIOC;
    static constexpr uint8_t UP_PIN = 12;
}

// ============================================================================
// Encoder Interface (TIM4 hardware encoder mode)
// Uses TIM4_CH1/CH2 for quadrature counting, EXTI for index pulse
// Note: TIM4 is 16-bit (ARR max 0xFFFF). Wired to CN10-P17 / CN7-P21.
// ============================================================================
namespace Encoder {
    // Timer instance for hardware encoder mode
    static TIM_TypeDef* const TIMER = TIM4;

    // Quadrature A - PB6 (TIM4_CH1) - CN10 P-17
    static GPIO_TypeDef* const EA_PORT = GPIOB;
    static constexpr uint8_t EA_PIN = 6;
    static constexpr uint8_t EA_AF = 2;          // AF2 for TIM4

    // Quadrature B - PB7 (TIM4_CH2) - CN7 P-21
    static GPIO_TypeDef* const EB_PORT = GPIOB;
    static constexpr uint8_t EB_PIN = 7;
    static constexpr uint8_t EB_AF = 2;          // AF2 for TIM4

    // Index pulse - PC9 (GPIO input, EXTI9 for interrupt)
    static GPIO_TypeDef* const EZ_PORT = GPIOC;
    static constexpr uint8_t EZ_PIN = 9;
    static constexpr uint8_t EZ_EXTI_LINE = 9;
    static constexpr IRQn_Type EZ_EXTI_IRQn = EXTI9_5_IRQn;

    // Configuration pins (directly from board directly from board directly from board from encoder module directly from board)
    static GPIO_TypeDef* const NG_PORT = GPIOC;  // nG (active low config)
    static constexpr uint8_t NG_PIN = 2;

    static GPIO_TypeDef* const G_PORT = GPIOC;   // G (config)
    static constexpr uint8_t G_PIN = 3;
}

// ============================================================================
// Onboard Resources
// ============================================================================
namespace Onboard {
    // User LED (LD2) - NOTE: shares PA5 with SPI1 SCK!
    static GPIO_TypeDef* const LED_PORT = GPIOA;
    static constexpr uint8_t LED_PIN = 5;

    // User Button (B1)
    static GPIO_TypeDef* const BTN_PORT = GPIOC;
    static constexpr uint8_t BTN_PIN = 13;
}

// ============================================================================
// VCP UART (ST-LINK / J-Link OB Virtual COM Port)
// Per UM1724: USART2 connected via SB13/SB14 solder bridges
// ============================================================================
namespace VCP_UART {
    static USART_TypeDef* const INSTANCE = USART2;
    static GPIO_TypeDef* const PORT = GPIOA;
    static constexpr uint8_t TX_PIN = 2;         // PA2 -> ST-LINK RX
    static constexpr uint8_t RX_PIN = 3;         // PA3 <- ST-LINK TX
    static constexpr uint8_t AF = 7;             // AF7 for USART2

    // Default configuration
    static constexpr uint32_t DEFAULT_BAUD = 115200;
    static constexpr uint32_t APB1_CLOCK_HZ = 42000000;  // For baud calculation
}

} // namespace Pins

#endif // BOARD_PINS_HPP
