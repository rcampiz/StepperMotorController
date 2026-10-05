/**
 * @file comms_service_init.hpp
 * @brief L3 init contract: dispatchers, L2/L1 cascade, DebugService, CommsService
 *
 * Creates no task. The composition root hands commsService to the task sheet.
 */
#pragma once

namespace Platform { class IClock; }

namespace Services {

class ICommsService;
class IEncoder;
class IEncoderFilterControl;
class IMotorTaskControl;
class IRemoteDisplay;
class IDisplayControl;

struct CommsServiceDeps {
    Services::IEncoder* encoder = nullptr;
    Services::IEncoderFilterControl* encFilter = nullptr;
    Services::IMotorTaskControl* motorCtrl = nullptr;
    Services::IRemoteDisplay* remoteDisplay = nullptr;
    Services::IDisplayControl* displayControl = nullptr;
};

struct CommsSubsystemResult {
    Services::ICommsService* commsService = nullptr;   ///< Comms task entry (H_TASKS_L3)
};

bool initCommsSubsystem(const CommsServiceDeps& deps,
                        Platform::IClock& clock,
                        CommsSubsystemResult& result);

} // namespace Services
