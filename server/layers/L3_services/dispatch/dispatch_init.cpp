/**
 * @file dispatch_init.cpp
 * @brief Implements Services::initDispatches() — the L3 side of H_L2_L3
 */

#include "L3_services/dispatch/dispatch_init.hpp"
#include "harness/defs/harness_l2_l3.hpp"
#include "L3_services/comms/system_dispatch/system_dispatch.hpp"
#include "L3_services/comms/trace_dispatch/trace_dispatch.hpp"
#include "L3_services/dispatch/queue_dispatch/queue_dispatch.hpp"
#include "L3_services/display/display_dispatch/display_dispatch.hpp"
#include "L3_services/infra/flash_dispatch/flash_dispatch.hpp"
#include "L3_services/motion/config_dispatch/config_dispatch.hpp"
#include "L3_services/motion/control_mode_dispatch/control_mode_dispatch.hpp"
#include "L3_services/motion/encoder_dispatch/encoder_dispatch.hpp"
#include "L3_services/motion/motion_dispatch/motion_dispatch.hpp"
#include "L3_services/motion/motor_driver_dispatch/motor_driver_dispatch.hpp"
#include "L3_services/motion/supervisor_trim_dispatch/supervisor_trim_dispatch.hpp"
#include "L3_services/safety/safety_dispatch/safety_dispatch.hpp"
#include "L3_services/debug/debug_service.hpp"
#include "L3_services/comms/comms.hpp"
#include "L3_services/display/display.hpp"

static Services::DisplayDispatch s_displayDisp;
static Services::TraceDispatch s_traceDisp;
static Services::SystemDispatch s_systemDisp;
static Services::MotionDispatch s_motionDisp;
static Services::ConfigDispatch s_configDisp;
static Services::SafetyDispatch s_safetyDisp;
static Services::QueueDispatch s_queueDisp;
static Services::ControlModeDispatch s_ctrlModeDisp;
static Services::SupervisorTrimDispatch s_supTrimDisp;
static Services::FlashDispatch s_flashDisp;
static Services::EncoderDispatch s_encoderDisp;
static Services::MotorDriverDispatch s_motorDrvDisp;

namespace Services {
DisplayGroup display{s_displayDisp};
CommsGroup comms{s_traceDisp, s_systemDisp};
} // namespace Services

namespace Services {

const Harness::L3Dispatchers* initDispatches(const DispatchDeps& deps)
{
    s_encoderDisp.setEncoder(deps.encoder);
    s_encoderDisp.setEncFilter(deps.encFilter);
    s_motorDrvDisp.setMotorCtrl(deps.motorCtrl);
    s_displayDisp.setRemoteDisplay(deps.remoteDisplay);
    s_systemDisp.setClock(deps.clock);

    static const Harness::L3Dispatchers s_bundle{
        s_motionDisp, s_configDisp, s_safetyDisp, s_queueDisp, s_ctrlModeDisp,
        s_supTrimDisp, s_encoderDisp, s_motorDrvDisp, s_displayDisp, s_flashDisp,
        s_systemDisp, s_traceDisp, g_debugService
    };
    return &s_bundle;
}

void setDispatchEventSeqFn(uint32_t (*fn)())
{
    s_systemDisp.setEventSeqFn(fn);
}

} // namespace Services
