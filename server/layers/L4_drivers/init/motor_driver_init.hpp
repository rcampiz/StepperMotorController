/**
 * @file motor_driver_init.hpp
 * @brief Harness init contract — motor driver (L4 → L5)
 *
 * Declared in harness so L3 can call L4 init without including L4 headers.
 * Implemented in L4_drivers/init/motor_driver_init.cpp.
 */

#pragma once

namespace Harness { class IMotorDriver; }

namespace Drivers {

/**
 * @brief Initialize motor driver via cascading init (L4 → L5)
 *
 * Calls Harness::initMotorBoard() to get GPIO/SPI, then constructs
 * and initializes PowerSTEP01.
 *
 * @param driver Output pointer to initialized motor driver
 * @return true on success
 */
bool initMotorDriver(Harness::IMotorDriver*& driver);

} // namespace Drivers
