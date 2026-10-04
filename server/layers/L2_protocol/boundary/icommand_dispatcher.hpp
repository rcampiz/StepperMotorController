/**
 * @file boundary/icommanddispatcher.hpp
 * @brief Consumer port — Protocol side of the Services::ICommandDispatcher harness connection
 *
 * Structurally identical to Services::ICommandDispatcher.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

#include "harness/pins/dispatch_result.hpp"
#include "L2_protocol/boundary/itrace_dispatcher.hpp"
#include "L2_protocol/boundary/isafety_dispatcher.hpp"
#include "L2_protocol/boundary/idisplay_dispatcher.hpp"
#include "L2_protocol/boundary/iflash_image_dispatcher.hpp"
#include "L2_protocol/boundary/imotion_dispatcher.hpp"
#include "L2_protocol/boundary/iqueue_dispatcher.hpp"
#include "L2_protocol/boundary/iencoder_dispatcher.hpp"
#include "L2_protocol/boundary/icontrol_mode_dispatcher.hpp"
#include "L2_protocol/boundary/isupervisor_trim_dispatcher.hpp"
#include "L2_protocol/boundary/imotor_driver_dispatcher.hpp"
#include "L2_protocol/boundary/imotor_config_dispatcher.hpp"
#include "L2_protocol/boundary/isystem_dispatcher.hpp"

namespace Protocol {

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

} // namespace Protocol
