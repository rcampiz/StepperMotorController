/**
 * @file encoder_driver_init.hpp
 * @brief L4 init for encoder driver — calls harness board init, constructs Encoder
 *
 * Returns harness interface pointers. Callers never see concrete L4 types.
 */

#pragma once

namespace Harness { class IEncoderHardware; class ITimerCounter; class IEncoderSampler; }

namespace Drivers {

/**
 * @brief Encoder driver init result — harness interfaces only
 */
struct EncoderDriverHandle {
    Harness::IEncoderHardware* encoder = nullptr;
    Harness::ITimerCounter* timer = nullptr;
    Harness::IEncoderSampler* sampler = nullptr;
};

/**
 * @brief Initialize encoder driver via cascading init (L4 → L5)
 *
 * Calls Harness::initEncoderBoard() for GPIO/EXTI/timer, then constructs
 * and initializes Encoder driver. Returns opaque handle.
 *
 * @param handle Output: opaque handle (populated on success)
 * @return true on success
 */
bool initEncoderDrivers(EncoderDriverHandle*& handle);

} // namespace Drivers
