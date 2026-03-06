/**
 * @file spi_board_config.hpp
 * @brief Board-specific SPI device enumeration and pin mappings
 *
 * Defines which SPI devices exist on this board, which bus each
 * uses, and which GPIO pins serve as chip-selects.
 *
 * CS GPIO objects are created as static locals — callers get GPIO&
 * without needing CMSIS types.
 */

#ifndef SPI_BOARD_CONFIG_HPP
#define SPI_BOARD_CONFIG_HPP

#include <stdint.h>
#include "harness/pins/ispi_bus.hpp"
#include "L5_board/gpio.hpp"
#include "L5_board/board_pins.hpp"

namespace Board {

/**
 * @brief SPI device identifier (determines bus and CS)
 */
enum class SPIDevice : uint8_t {
    Motor,      // SPI1, Mode 3, CS=PC8
    LCD,        // SPI1, Mode 0, CS=PC6
    NORFlash    // SPI2, Mode 0, CS=PA8
};

inline uint8_t getDeviceBus(SPIDevice device) {
    switch (device) {
        case SPIDevice::Motor:    return 1;
        case SPIDevice::LCD:      return 1;
        case SPIDevice::NORFlash: return 2;
        default:                  return 1;
    }
}

inline Harness::SPIMode getDeviceMode(SPIDevice device) {
    switch (device) {
        case SPIDevice::Motor:    return Harness::SPIMode::Mode3;
        case SPIDevice::LCD:      return Harness::SPIMode::Mode0;
        case SPIDevice::NORFlash: return Harness::SPIMode::Mode0;
        default:                  return Harness::SPIMode::Mode0;
    }
}

/**
 * @brief Holder for the three CS GPIO objects (single set of statics)
 */
struct CSPins {
    GPIO motor;
    GPIO lcd;
    GPIO flash;
    bool configured;

    CSPins()
        : motor(Pins::IHM03A1::CS_PORT, Pins::IHM03A1::CS_PIN),
          lcd(Pins::GFX_LCD::CS_PORT, Pins::GFX_LCD::CS_PIN),
          flash(Pins::GFX_Flash::CS_PORT, Pins::GFX_Flash::CS_PIN),
          configured(false) {}

    void configure() {
        if (configured) return;
        GPIO* all[] = { &motor, &lcd, &flash };
        for (GPIO* g : all) {
            g->setMode(GPIOMode::Output);
            g->setOutputType(GPIOOutputType::PushPull);
            g->setSpeed(GPIOSpeed::VeryHigh);
            g->write(true);  // Deasserted (high)
        }
        configured = true;
    }
};

inline CSPins& csPins() {
    static CSPins pins;
    return pins;
}

/**
 * @brief Initialize all SPI CS pins (call once during system init)
 */
inline void initAllCSPins() {
    csPins().configure();
}

/**
 * @brief Get CS GPIO for a device
 */
inline GPIO& getDeviceCS(SPIDevice device) {
    auto& p = csPins();
    switch (device) {
        case SPIDevice::Motor:    return p.motor;
        case SPIDevice::LCD:      return p.lcd;
        case SPIDevice::NORFlash: return p.flash;
        default:                  return p.motor;
    }
}

} // namespace Board

#endif // SPI_BOARD_CONFIG_HPP
