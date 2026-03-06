/**
 * @file comms_service_init.hpp
 * @brief Harness contract for L3 comms subsystem init
 *
 * L3 orchestrates: dispatches → L2 protocol (cascades to L1) → task.
 * All Platform deps injected.
 */

#pragma once

namespace Harness {
class IClock;
class IEncoder;
class IEncoderFilterControl;
class IMotorTaskControl;
class IRemoteDisplay;
class IScheduler;
} // namespace Harness

namespace Services {

/**
 * @brief Dependencies for comms subsystem init (from earlier subsystem inits)
 */
struct CommsServiceDeps {
    Harness::IEncoder* encoder = nullptr;
    Harness::IEncoderFilterControl* encFilter = nullptr;
    Harness::IMotorTaskControl* motorCtrl = nullptr;
    Harness::IRemoteDisplay* remoteDisplay = nullptr;
};

/**
 * @brief Initialize comms subsystem — L3 drives the cascade
 *
 * @param deps      Dependencies from earlier subsystem inits
 * @param clock     System clock (injected from Platform)
 * @param scheduler Scheduler for RTOS task creation (injected from Platform)
 * @return true on success
 */
bool initCommsSubsystem(const CommsServiceDeps& deps,
                        Harness::IClock& clock,
                        Harness::IScheduler& scheduler);

} // namespace Services
