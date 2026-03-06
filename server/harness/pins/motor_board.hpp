/**
 * @file motor_board.hpp
 * @brief Harness contract for motor board initialization
 *
 * Declares the pin struct and init function for the motor subsystem.
 * L5 provides the implementation; L4 calls through this harness interface.
 */

#pragma once

namespace Harness {

class IGPIO;
class ISPIBus;

struct MotorPins {
    IGPIO* cs = nullptr;
    IGPIO* stbyRst = nullptr;
    IGPIO* busy = nullptr;
    IGPIO* flag = nullptr;
    ISPIBus* spi = nullptr;
};

/**
 * @brief Initialize motor board GPIO and SPI
 *
 * Constructs GPIO objects for powerSTEP01 pins and gets SPI1.
 * Implemented by L5 board layer.
 *
 * @param pins Output struct populated with harness interface pointers
 * @return true on success
 */
bool initMotorBoard(MotorPins& pins);

} // namespace Harness
