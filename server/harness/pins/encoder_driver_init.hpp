/**
 * @file encoder_driver_init.hpp
 * @brief Init contract: L4 provides the quadrature encoder driver
 *
 * Implemented in L4_drivers/init/encoder_driver_init.cpp over H_L4_L5. Returns false
 * when no encoder board is present (the system then runs OPEN_LOOP).
 */
#pragma once
#include <stdint.h>

namespace Platform { class IClock; class IMicrosecondClock; }
namespace Drivers { class IEncoderHardware; }

namespace Drivers {

bool initEncoderDrivers(Platform::IClock& clock,
                        Platform::IMicrosecondClock& usClock,
                        uint16_t sampleRateHz,
                        Drivers::IEncoderHardware*& encoder);

} // namespace Drivers
