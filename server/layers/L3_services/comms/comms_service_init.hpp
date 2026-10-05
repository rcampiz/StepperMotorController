/**
 * @file comms_service_init.hpp
 * @brief Harness contract for L3 comms subsystem init
 *
 * L3 orchestrates: dispatches → L2 protocol (cascades to L1) → task.
 * All Platform deps injected.
 */

#pragma once

namespace Platform { class IClock; class IScheduler; class ITaskNotifier; }

namespace Services {

class IEncoder;
class IEncoderFilterControl;
class IMotorTaskControl;
class IRemoteDisplay;

/**
 * @brief Dependencies for comms subsystem init (from earlier subsystem inits)
 */
struct CommsServiceDeps {
    Services::IEncoder* encoder = nullptr;
    Services::IEncoderFilterControl* encFilter = nullptr;
    Services::IMotorTaskControl* motorCtrl = nullptr;
    Services::IRemoteDisplay* remoteDisplay = nullptr;
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
                        Platform::IClock& clock,
                        Platform::IScheduler& scheduler,
                        Platform::ITaskNotifier* rxWake);

} // namespace Services
