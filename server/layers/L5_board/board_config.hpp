/**
 * @file board_config.hpp
 * @brief Pin definitions for NUCLEO-F401RE board
 *
 * Centralizes all GPIO pin assignments for the project.
 * Arduino connector pin names are used for clarity.
 */

#ifndef BOARD_CONFIG_HPP
#define BOARD_CONFIG_HPP

#include "stm32f401xe.h"

namespace BoardConfig {

// ============================================================================
// LED Pins
// ============================================================================
namespace LEDPins {
    // Onboard user LED (LD2)
    static GPIO_TypeDef* const USER_PORT = GPIOA;
    static constexpr uint8_t USER_PIN = 5;

    // External LED on D12 (for button feedback)
    static GPIO_TypeDef* const BUTTON_FEEDBACK_PORT = GPIOA;
    static constexpr uint8_t BUTTON_FEEDBACK_PIN = 6;
}

// ============================================================================
// Button/Input Pins
// ============================================================================
namespace ButtonPins {
    // Button on D8
    static GPIO_TypeDef* const D8_PORT = GPIOA;
    static constexpr uint8_t D8_PIN = 9;
}

// ============================================================================
// Shift Register (SN74HC595) Pins
// ============================================================================
namespace ShiftRegister {
    // SER (Serial Data) - D2
    static GPIO_TypeDef* const SER_PORT = GPIOA;
    static constexpr uint8_t SER_PIN = 10;

    // nOE (Output Enable, active low) - D3
    static GPIO_TypeDef* const NOE_PORT = GPIOB;
    static constexpr uint8_t NOE_PIN = 3;

    // RCLK (Latch Clock) - D4
    static GPIO_TypeDef* const RCLK_PORT = GPIOB;
    static constexpr uint8_t RCLK_PIN = 5;

    // SRCLK (Shift Clock) - D5
    static GPIO_TypeDef* const SRCLK_PORT = GPIOB;
    static constexpr uint8_t SRCLK_PIN = 4;

    // nSRCLR (Shift Register Clear, active low) - D6
    static GPIO_TypeDef* const NSRCLR_PORT = GPIOB;
    static constexpr uint8_t NSRCLR_PIN = 10;
}

// ============================================================================
// UART/USB Pins
// ============================================================================
namespace UARTPins {
    // USART2 (connected to ST-LINK VCP)
    static GPIO_TypeDef* const TX_PORT = GPIOA;
    static constexpr uint8_t TX_PIN = 2;

    static GPIO_TypeDef* const RX_PORT = GPIOA;
    static constexpr uint8_t RX_PIN = 3;

    static constexpr uint8_t ALTERNATE_FUNCTION = 7;  // AF7 for USART1/2/3
}

// ============================================================================
// ADC Pins
// ============================================================================
namespace ADCPins {
    // ADC Channel 0 - A0 connector
    static GPIO_TypeDef* const A0_PORT = GPIOA;
    static constexpr uint8_t A0_PIN = 0;
    static constexpr uint8_t A0_CHANNEL = 0;
}

// ============================================================================
// Stepper Motor Pins (example for future use)
// ============================================================================
namespace Stepper {
    // Stepper motor driver (A4988/DRV8825/TMC2208)
    // Uncomment and configure when needed

    // constexpr GPIO_TypeDef* STEP_PORT = GPIOB;
    // constexpr uint8_t STEP_PIN = 0;

    // constexpr GPIO_TypeDef* DIR_PORT = GPIOB;
    // constexpr uint8_t DIR_PIN = 1;

    // constexpr GPIO_TypeDef* ENABLE_PORT = GPIOB;
    // constexpr uint8_t ENABLE_PIN = 2;
}

// ============================================================================
// Arduino Connector Pin Mapping Reference
// ============================================================================
/*
 * CN9 Connector (Digital pins):
 *   D0  -> PA3  (USART2_RX)
 *   D1  -> PA2  (USART2_TX)
 *   D2  -> PA10
 *   D3  -> PB3
 *   D4  -> PB5
 *   D5  -> PB4
 *   D6  -> PB10
 *   D7  -> PA8
 *   D8  -> PA9
 *   D9  -> PC7
 *   D10 -> PB6
 *   D11 -> PA7
 *   D12 -> PA6
 *   D13 -> PA5  (LED_USER, SPI_SCK)
 *   D14 -> PB9  (I2C_SDA)
 *   D15 -> PB8  (I2C_SCL)
 *
 * CN5 Connector (Analog pins):
 *   A0  -> PA0  (ADC_IN0)
 *   A1  -> PA1  (ADC_IN1)
 *   A2  -> PA4  (ADC_IN4)
 *   A3  -> PB0  (ADC_IN8)
 *   A4  -> PC1  (ADC_IN11)
 *   A5  -> PC0  (ADC_IN10)
 */

} // namespace BoardConfig

#endif // BOARD_CONFIG_HPP