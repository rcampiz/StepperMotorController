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

#include "harness/pins/dispatch_result.hpp"
#include "harness/pins/itrace_dispatcher.hpp"
#include "harness/pins/isafety_dispatcher.hpp"
#include "harness/pins/idisplay_dispatcher.hpp"
#include "harness/pins/iflash_image_dispatcher.hpp"
#include "harness/pins/imotion_dispatcher.hpp"
#include "harness/pins/iqueue_dispatcher.hpp"
#include "harness/pins/iencoder_dispatcher.hpp"
#include "harness/pins/icontrol_mode_dispatcher.hpp"
#include "harness/pins/isupervisor_trim_dispatcher.hpp"
#include "harness/pins/imotor_driver_dispatcher.hpp"
#include "harness/pins/imotor_config_dispatcher.hpp"
#include "harness/pins/isystem_dispatcher.hpp"

namespace Harness {

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

} // namespace Harness
