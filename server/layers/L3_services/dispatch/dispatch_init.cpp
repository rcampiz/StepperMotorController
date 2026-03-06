/**
 * @file dispatch_init.cpp
 * @brief Creates all 12 L3 dispatch implementations
 *
 * Layer-clean: only includes L3 + harness headers.
 * No L1, L2, L4, L5, or F_platform includes.
 */

#include "L3_services/dispatch/dispatch_init.hpp"

// L3 dispatch implementations
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

// Service group headers (extern declarations)
#include "L3_services/comms/comms.hpp"
#include "L3_services/display/display.hpp"

// Tracing (conditional)
#include "harness/trace/interface_trace.hpp"
#ifdef ENABLE_INTERFACE_TRACE
#include "harness/taps/traced_encoder.hpp"
#endif

// ── File-scope L3 dispatch instances (group members) ────────────────

static Services::DisplayDispatch s_displayDisp;
static Services::TraceDispatch s_traceDisp;
static Services::SystemDispatch s_systemDisp;

// ── Service group definitions (satisfy extern declarations) ─────────

namespace Services {
DisplayGroup display{s_displayDisp};
CommsGroup comms{s_traceDisp, s_systemDisp};
} // namespace Services

namespace Services {

bool initDispatches(Harness::DispatchInterfaces& out, const DispatchDeps& deps)
{
    // L3 dispatch implementations (pure L3, no injected deps)
    static MotionDispatch motionDisp;
    static ConfigDispatch configDisp;
    static SafetyDispatch safetyDisp;
    static QueueDispatch queueDisp;
    static ControlModeDispatch ctrlModeDisp;
    static SupervisorTrimDispatch supTrimDisp;
    static FlashDispatch flashDisp;

    // L3 dispatch implementations (with injected harness pins)
    static EncoderDispatch encoderDisp;
    static MotorDriverDispatch motorDrvDisp;

#ifdef ENABLE_INTERFACE_TRACE
    static Harness::TracedEncoder tracedEncoder(*deps.encoder);
    encoderDisp.setEncoder(&tracedEncoder);
#else
    encoderDisp.setEncoder(deps.encoder);
#endif
    encoderDisp.setEncFilter(deps.encFilter);
    motorDrvDisp.setMotorCtrl(deps.motorCtrl);

    // Inject deps into file-scope group member dispatch instances
    s_displayDisp.setRemoteDisplay(deps.remoteDisplay);
    s_systemDisp.setClock(deps.clock);

    // Populate output — 12 sub-dispatcher interface pointers
    out.motion         = &motionDisp;
    out.config         = &configDisp;
    out.safety         = &safetyDisp;
    out.queue          = &queueDisp;
    out.controlMode    = &ctrlModeDisp;
    out.supervisorTrim = &supTrimDisp;
    out.encoder        = &encoderDisp;
    out.motorDriver    = &motorDrvDisp;
    out.display        = &s_displayDisp;
    out.flash          = &flashDisp;
    out.system         = &s_systemDisp;
    out.trace          = &s_traceDisp;

    return true;
}

void setDispatchEventSeqFn(uint32_t (*fn)())
{
    s_systemDisp.setEventSeqFn(fn);
}

} // namespace Services
