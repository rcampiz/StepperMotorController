/**
 * @file dispatch_init.hpp
 * @brief L3 builds the thirteen dispatcher objects and bundles them for H_L2_L3
 */
#pragma once

#include <stdint.h>

namespace Harness { struct L3Dispatchers; }

namespace Platform { class IClock; }

namespace Services {

class IEncoder;
class IEncoderFilterControl;
class IMotorTaskControl;
class IRemoteDisplay;

struct DispatchDeps {
    Services::IEncoder*              encoder = nullptr;
    Services::IEncoderFilterControl* encFilter = nullptr;
    Services::IMotorTaskControl*     motorCtrl = nullptr;
    Services::IRemoteDisplay*        remoteDisplay = nullptr;
    Platform::IClock*                clock = nullptr;
};

/// Build (once) and return the dispatcher bundle, or nullptr on failure.
const Harness::L3Dispatchers* initDispatches(const DispatchDeps& deps);

void setDispatchEventSeqFn(uint32_t (*fn)());
void setDispatchTelemetryFns(uint32_t (*set)(uint32_t), uint32_t (*get)());

} // namespace Services
