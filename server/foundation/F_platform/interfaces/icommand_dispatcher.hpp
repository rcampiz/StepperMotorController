/**
 * @file icommand_dispatcher.hpp
 * @brief Composite interface aggregating all domain dispatchers
 *
 * ICommandDispatcher inherits from 12 focused domain interfaces.
 * Consuming code (CommandParser) holds a single ICommandDispatcher&
 * reference, accessing domain methods via inheritance.
 *
 * Each sub-interface is independently defined and can be used
 * standalone for testing, mocking, or future decomposition.
 */

#pragma once

#include "F_platform/interfaces/dispatch_result.hpp"
#include "F_platform/interfaces/itrace_dispatcher.hpp"
#include "F_platform/interfaces/isafety_dispatcher.hpp"
#include "F_platform/interfaces/idisplay_dispatcher.hpp"
#include "F_platform/interfaces/iflash_image_dispatcher.hpp"
#include "F_platform/interfaces/imotion_dispatcher.hpp"
#include "F_platform/interfaces/iqueue_dispatcher.hpp"
#include "F_platform/interfaces/iencoder_dispatcher.hpp"
#include "F_platform/interfaces/icontrol_mode_dispatcher.hpp"
#include "F_platform/interfaces/isupervisor_trim_dispatcher.hpp"
#include "F_platform/interfaces/imotor_driver_dispatcher.hpp"
#include "F_platform/interfaces/imotor_config_dispatcher.hpp"
#include "F_platform/interfaces/isystem_dispatcher.hpp"

namespace Comms {

class ICommandDispatcher
    : public ITraceDispatcher
    , public ISafetyDispatcher
    , public IDisplayDispatcher
    , public IFlashImageDispatcher
    , public IMotionDispatcher
    , public IQueueDispatcher
    , public IEncoderDispatcher
    , public IControlModeDispatcher
    , public ISupervisorTrimDispatcher
    , public IMotorDriverDispatcher
    , public IMotorConfigDispatcher
    , public ISystemDispatcher
{
public:
    virtual ~ICommandDispatcher() = default;
};

} // namespace Comms
