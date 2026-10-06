/**
 * @file comms_service_init.cpp
 * @brief Implements Services::initCommsSubsystem() — L3 drives the comms init cascade
 *
 * Builds the thirteen L3 dispatchers, cascades to L2 (which cascades to L1), connects
 * H_L2_L3 and initialises DebugService + CommsService. No task creation (rule 1).
 */

#include "L3_services/comms/comms_service_init.hpp"
#include "harness/defs/harness_l2_l3.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "harness/pins/comms_protocol_init.hpp"
#include "L3_services/dispatch/dispatch_init.hpp"
#include "L3_services/debug/debug_service.hpp"
#include "L3_services/comms/comms_service/comms_service.hpp"
#include "L3_services/motion/motion.hpp"
#include "L3_services/dispatch/command_queue/command_queue.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include "L3_services/ui/ui_mode/ui_mode.hpp"

namespace Services {

bool initCommsSubsystem(const CommsServiceDeps& deps,
                        Platform::IClock& clock,
                        CommsSubsystemResult& result)
{
    // Debug service: raw access through H_L3_L4 (may be absent); task control by message/flag
    const Harness::L3L4Harness& l3l4 = Harness::connectL3L4(clock);
    g_debugService.init(l3l4.debug, deps.motorCtrl, deps.displayControl);

    // Thirteen dispatchers (L3 side of H_L2_L3)
    DispatchDeps dispDeps;
    dispDeps.encoder       = deps.encoder;
    dispDeps.encFilter     = deps.encFilter;
    dispDeps.motorCtrl     = deps.motorCtrl;
    dispDeps.remoteDisplay = deps.remoteDisplay;
    dispDeps.clock         = &clock;
    const Harness::L3Dispatchers* dispatchers = initDispatches(dispDeps);
    if (dispatchers == nullptr) {
        return false;
    }

    // L2 (which cascades to L1)
    Protocol::CommsProtocol protocol;
    if (!Protocol::initCommsProtocol(protocol, *dispatchers, clock)) {
        return false;
    }

    // H_L2_L3
    const Harness::L2L3Harness& l2l3 = Harness::connectL2L3(*dispatchers, *protocol.parser);

    // Comms service (task body, safety actions, dispatch stats)
    g_commsService.init(l2l3.processor, clock, motion.stepper.event, g_commandQueue,
                        g_telemetryStore, &UI::g_uiMode, deps.motorCtrl);
    setDispatchEventSeqFn(&CommsService::lastEventSeqStatic);
    setDispatchTelemetryFns(&CommsService::setTelemetryPeriodStatic,
                            &CommsService::getTelemetryPeriodStatic);

    result.commsService = &g_commsService;
    return true;
}

} // namespace Services
