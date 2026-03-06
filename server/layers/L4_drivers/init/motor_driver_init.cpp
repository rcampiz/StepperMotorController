/**
 * @file motor_driver_init.cpp
 * @brief L4 init for motor driver
 */

#include "L4_drivers/init/motor_driver_init.hpp"
#include "harness/pins/motor_board.hpp"
#include "L4_drivers/devices/powerstep01.hpp"

// Tracing (conditional)
#include "harness/trace/interface_trace.hpp"
#ifdef ENABLE_INTERFACE_TRACE
#include "harness/taps/traced_motor_driver.hpp"
#endif

namespace Drivers {

bool initMotorDriver(Harness::IMotorDriver*& driver)
{
    // Board init through harness — GPIO + SPI
    Harness::MotorPins pins;
    if (!Harness::initMotorBoard(pins)) {
        return false;
    }

    // Construct and initialize PowerSTEP01
    static PowerSTEP01 motor(*pins.spi, *pins.cs, *pins.stbyRst, *pins.busy, *pins.flag);
    motor.init();

#ifdef ENABLE_INTERFACE_TRACE
    static Harness::TracedMotorDriver tracedMotor(motor);
    driver = &tracedMotor;
#else
    driver = &motor;
#endif

    return true;
}

} // namespace Drivers
