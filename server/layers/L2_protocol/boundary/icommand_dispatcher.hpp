/**
 * @file icommand_dispatcher.hpp
 * @brief L2-internal composite of the thirteen L3 dispatcher interfaces (H_L2_L3)
 *
 * The parser and its handlers talk to one object. The single interface definitions are
 * owned by L3 and arrive through harness_l2_l3.hpp; this composite only aggregates them.
 * Protocol::ServiceDispatcher implements it by forwarding to Harness::L3Dispatchers.
 */
#pragma once

#include "harness/defs/harness_l2_l3.hpp"

namespace Protocol {

class ICommandDispatcher
    : public Services::ITraceDispatcher
    , public Services::ISafetyDispatcher
    , public Services::IDisplayDispatcher
    , public Services::IFlashImageDispatcher
    , public Services::IMotionDispatcher
    , public Services::IQueueDispatcher
    , public Services::IEncoderDispatcher
    , public Services::IControlModeDispatcher
    , public Services::ISupervisorTrimDispatcher
    , public Services::IMotorDriverDispatcher
    , public Services::IMotorConfigDispatcher
    , public Services::ISystemDispatcher
    , public Services::IDebugDispatcher
{
public:
    virtual ~ICommandDispatcher() = default;
};

} // namespace Protocol
