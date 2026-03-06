/**
 * @file encoder_driver_init.cpp
 * @brief L4 init for encoder driver
 *
 */

#include "L4_drivers/init/encoder_driver_init.hpp"
#include "harness/pins/encoder_board.hpp"
#include "L4_drivers/devices/encoder.hpp"

namespace Drivers {

static EncoderDriverHandle s_encoderHandle;

bool initEncoderDrivers(EncoderDriverHandle*& handle)
{
    // Board init through harness — GPIO, EXTI, timer, sampler
    Harness::EncoderPins pins;
    if (!Harness::initEncoderBoard(pins)) {
        return false;
    }

    // Construct and initialize encoder driver
    static Encoder encoder(*pins.timer);
    if (!encoder.init()) {
        return false;
    }

    s_encoderHandle.encoder = &encoder;
    s_encoderHandle.timer = pins.timer;
    s_encoderHandle.sampler = pins.sampler;
    handle = &s_encoderHandle;
    return true;
}

} // namespace Drivers
