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

#include "F_platform/types/dispatch_result.hpp"
#include "F_platform/dispatch/itrace_dispatcher.hpp"
#include "F_platform/dispatch/isafety_dispatcher.hpp"
#include "F_platform/dispatch/idisplay_dispatcher.hpp"
#include "F_platform/dispatch/iflash_image_dispatcher.hpp"
#include "F_platform/dispatch/imotion_dispatcher.hpp"
#include "F_platform/dispatch/iqueue_dispatcher.hpp"
#include "F_platform/dispatch/iencoder_dispatcher.hpp"
#include "F_platform/dispatch/icontrol_mode_dispatcher.hpp"
#include "F_platform/dispatch/isupervisor_trim_dispatcher.hpp"
#include "F_platform/dispatch/imotor_driver_dispatcher.hpp"
#include "F_platform/dispatch/imotor_config_dispatcher.hpp"
#include "F_platform/dispatch/isystem_dispatcher.hpp"

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
