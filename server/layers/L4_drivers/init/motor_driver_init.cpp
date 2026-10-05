/**
 * @file motor_driver_init.cpp
 * @brief L4 init for the motor driver — takes its pins from the L4<->L5 harness
 */

#include "harness/pins/motor_driver_init.hpp"
#include "harness/defs/harness_l4_l5.hpp"
#include "L4_drivers/devices/powerstep01.hpp"

namespace Drivers {

bool initMotorDriver(Drivers::IMotorDriver*& driver)
{
    const Harness::L4L5Harness& h = Harness::connectL4L5();
    if (h.motor == nullptr) {
        return false;
    }
    const Harness::MotorPins& p = *h.motor;

    // Construct and initialize PowerSTEP01 directly on the Board:: pins
    static PowerSTEP01 motor(*p.spi, *p.cs, *p.stbyRst, *p.busy, *p.flag);
    motor.init();

    // Raw driver only. The L3<->L4 harness (adaptMotorDriver) applies the trace tap once.
    driver = &motor;
    return true;
}

} // namespace Drivers
