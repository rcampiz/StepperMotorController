/**
 * @file motor_driver_init.cpp
 * @brief L4 init for motor driver
 */

#include "harness/pins/motor_driver_init.hpp"
#include "harness/pins/motor_board.hpp"
#include "harness/adapters/spi_adapter.hpp"
#include "harness/adapters/gpio_adapter.hpp"
#include "L4_drivers/devices/powerstep01.hpp"

// Tracing (conditional)
#include "harness/trace/interface_trace.hpp"
#ifdef ENABLE_INTERFACE_TRACE
#include "harness/taps/traced_motor_driver.hpp"
#endif

namespace Drivers {

bool initMotorDriver(Drivers::IMotorDriver*& driver)
{
    // Board init through harness — GPIO + SPI
    Harness::MotorPins pins;
    if (!Harness::initMotorBoard(pins)) {
        return false;
    }

    // Adapt Board:: provider types to Drivers:: consumer types
    static Harness::SPIAdapter spiAdapter(*pins.spi);
    static Harness::GPIOAdapter csAdapter(*pins.cs);
    static Harness::GPIOAdapter stbyRstAdapter(*pins.stbyRst);
    static Harness::GPIOAdapter busyAdapter(*pins.busy);
    static Harness::GPIOAdapter flagAdapter(*pins.flag);

    // Construct and initialize PowerSTEP01
    static PowerSTEP01 motor(spiAdapter, csAdapter, stbyRstAdapter, busyAdapter, flagAdapter);
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
