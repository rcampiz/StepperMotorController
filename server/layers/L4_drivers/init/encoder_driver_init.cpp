/**
 * @file encoder_driver_init.cpp
 * @brief L4 init for the quadrature encoder — pins from the L4<->L5 harness
 */

#include "harness/pins/encoder_driver_init.hpp"
#include "harness/defs/harness_l4_l5.hpp"
#include "L4_drivers/devices/encoder.hpp"

namespace Drivers {

bool initEncoderDrivers(Platform::IClock& clock,
                        Platform::IMicrosecondClock& usClock,
                        uint16_t sampleRateHz,
                        Drivers::IEncoderHardware*& encoder)
{
    const Harness::L4L5Harness& h = Harness::connectL4L5();
    if (h.encoder == nullptr || h.encoder->timer == nullptr || h.encoder->sampler == nullptr) {
        return false;
    }

    static Encoder s_encoder(*h.encoder, clock, usClock, h.registerExti);
    if (!s_encoder.init(sampleRateHz)) {
        return false;
    }
    encoder = &s_encoder;
    return true;
}

} // namespace Drivers
