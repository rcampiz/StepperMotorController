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
#include "L3_services/boundary/itrace_dispatcher.hpp"
#include "L3_services/boundary/isafety_dispatcher.hpp"
#include "L3_services/boundary/idisplay_dispatcher.hpp"
#include "L3_services/boundary/iflash_image_dispatcher.hpp"
#include "L3_services/boundary/imotion_dispatcher.hpp"
#include "L3_services/boundary/iqueue_dispatcher.hpp"
#include "L3_services/boundary/iencoder_dispatcher.hpp"
#include "L3_services/boundary/icontrol_mode_dispatcher.hpp"
#include "L3_services/boundary/isupervisor_trim_dispatcher.hpp"
#include "L3_services/boundary/imotor_driver_dispatcher.hpp"
#include "L3_services/boundary/imotor_config_dispatcher.hpp"
#include "L3_services/boundary/isystem_dispatcher.hpp"

namespace Services {

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

} // namespace Services
