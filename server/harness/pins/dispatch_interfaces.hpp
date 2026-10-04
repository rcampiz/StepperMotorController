/**
 * @file dispatch_interfaces.hpp
 * @brief Harness contract — 12 sub-dispatcher interface pointers
 *
 * Output of L3 dispatch init, input of L2 protocol init.
 * Allows the composition root to connect L3 dispatch implementations
 * to L2 ServiceDispatcher without either layer seeing the other's types.
 */

#pragma once

namespace Services {
class IMotionDispatcher;
class IMotorConfigDispatcher;
class ISafetyDispatcher;
class IQueueDispatcher;
class IControlModeDispatcher;
class ISupervisorTrimDispatcher;
class IEncoderDispatcher;
class IMotorDriverDispatcher;
class IDisplayDispatcher;
class IFlashImageDispatcher;
class ISystemDispatcher;
class ITraceDispatcher;
} // namespace Services

namespace Harness {

struct DispatchInterfaces {
    Services::IMotionDispatcher*         motion = nullptr;
    Services::IMotorConfigDispatcher*    config = nullptr;
    Services::ISafetyDispatcher*         safety = nullptr;
    Services::IQueueDispatcher*          queue = nullptr;
    Services::IControlModeDispatcher*    controlMode = nullptr;
    Services::ISupervisorTrimDispatcher* supervisorTrim = nullptr;
    Services::IEncoderDispatcher*        encoder = nullptr;
    Services::IMotorDriverDispatcher*    motorDriver = nullptr;
    Services::IDisplayDispatcher*        display = nullptr;
    Services::IFlashImageDispatcher*     flash = nullptr;
    Services::ISystemDispatcher*         system = nullptr;
    Services::ITraceDispatcher*          trace = nullptr;
};

} // namespace Harness
