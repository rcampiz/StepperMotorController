/**
 * @file encoder_service_init.hpp
 * @brief Harness contract for L3 encoder subsystem init
 *
 * L3 orchestrates: L4 driver init (through harness) → L3 filter config →
 * task init (through harness). All Platform deps injected.
 */

#pragma once

namespace Harness {
class IClock;
class IEncoder;
class IEncoderFilterControl;
class IScheduler;
} // namespace Harness

namespace Services {

/**
 * @brief Result of encoder subsystem initialization
 */
struct EncoderSubsystemResult {
    bool encoderAvailable = false;              ///< true if encoder hardware present
    Harness::IEncoder* encoder = nullptr;       ///< Encoder data access
    Harness::IEncoderFilterControl* filterControl = nullptr; ///< Filter control
};

/**
 * @brief Initialize encoder subsystem — L3 drives the cascade
 *
 * @param clock     System clock (injected from Platform)
 * @param scheduler Scheduler for RTOS task creation (injected from Platform)
 * @param result    Output: availability flag + encoder interfaces
 * @return true on success (encoder may still be unavailable — check result)
 */
bool initEncoderSubsystem(Harness::IClock& clock,
                          Harness::IScheduler& scheduler,
                          EncoderSubsystemResult& result);

} // namespace Services
