/**
 * @file dispatch_interfaces.hpp
 * @brief Harness contract — 12 sub-dispatcher interface pointers
 *
 * Output of L3 dispatch init, input of L2 protocol init.
 * Allows the composition root to connect L3 dispatch implementations
 * to L2 ServiceDispatcher without either layer seeing the other's types.
 */

#pragma once

namespace Harness {

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

struct DispatchInterfaces {
    IMotionDispatcher*         motion = nullptr;
    IMotorConfigDispatcher*    config = nullptr;
    ISafetyDispatcher*         safety = nullptr;
    IQueueDispatcher*          queue = nullptr;
    IControlModeDispatcher*    controlMode = nullptr;
    ISupervisorTrimDispatcher* supervisorTrim = nullptr;
    IEncoderDispatcher*        encoder = nullptr;
    IMotorDriverDispatcher*    motorDriver = nullptr;
    IDisplayDispatcher*        display = nullptr;
    IFlashImageDispatcher*     flash = nullptr;
    ISystemDispatcher*         system = nullptr;
    ITraceDispatcher*          trace = nullptr;
};

} // namespace Harness
