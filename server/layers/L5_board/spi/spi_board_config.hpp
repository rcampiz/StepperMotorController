/**
 * @file spi_board_config.hpp
 * @brief Board-specific SPI device enumeration and pin mappings
 *
 * Defines which SPI devices exist on this board, which bus each
 * uses, and which GPIO pins serve as chip-selects.
 */

#ifndef SPI_BOARD_CONFIG_HPP
#define SPI_BOARD_CONFIG_HPP

#include <stdint.h>
#include "X_vendor/CMSIS/stm32f401xe.h"
#include "F_platform/interfaces/ispi_bus.hpp"
#include "L5_board/board_pins.hpp"

/**
 * @brief SPI device identifier (determines bus and CS)
 */
enum class SPIDevice : uint8_t {
    Motor,      // SPI1, Mode 3, CS=PC8
    LCD,        // SPI1, Mode 0, CS=PC6
    NORFlash    // SPI2, Mode 0, CS=PA8
};

/**
 * @brief Get the SPI bus for a device
 */
inline uint8_t getDeviceBus(SPIDevice device) {
    switch (device) {
        case SPIDevice::Motor:    return 1;
        case SPIDevice::LCD:      return 1;
        case SPIDevice::NORFlash: return 2;
        default:                  return 1;
    }
}

/**
 * @brief Get the default SPI mode for a device
 */
inline SPIMode getDeviceMode(SPIDevice device) {
    switch (device) {
        case SPIDevice::Motor:    return SPIMode::Mode3;
        case SPIDevice::LCD:      return SPIMode::Mode0;
        case SPIDevice::NORFlash: return SPIMode::Mode0;
        default:                  return SPIMode::Mode0;
    }
}

/**
 * @brief CS pin descriptor
 */
struct CSPin {
    GPIO_TypeDef* port;
    uint8_t pin;
};

/**
 * @brief Get CS pin for a device
 */
inline CSPin getDeviceCS(SPIDevice device) {
    switch (device) {
        case SPIDevice::Motor:    return {Pins::IHM03A1::CS_PORT, Pins::IHM03A1::CS_PIN};
        case SPIDevice::LCD:      return {Pins::GFX_LCD::CS_PORT, Pins::GFX_LCD::CS_PIN};
        case SPIDevice::NORFlash: return {Pins::GFX_Flash::CS_PORT, Pins::GFX_Flash::CS_PIN};
        default:                  return {Pins::IHM03A1::CS_PORT, Pins::IHM03A1::CS_PIN};
    }
}

#endif // SPI_BOARD_CONFIG_HPP
