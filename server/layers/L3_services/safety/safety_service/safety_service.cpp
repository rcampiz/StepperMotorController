/**
 * @file safety_service.cpp
 * @brief Safety service implementation — ESTOP, heartbeat, fault clear
 */

#include "L3_services/safety/safety_service/safety_service.hpp"
#include "L3_services/dispatch/command_queue/command_queue.hpp"
#include "L3_services/infra/trace/trace.hpp"
#include "harness/pins/isafety_actions.hpp"
#include <stdio.h>

namespace Services::Safety {

// Harness pin types used in this module
using Harness::g_safetyActions;

Result emergencyStop() {
    Trace::ServiceScope svc(Trace::SVC_SAFETY);
    TRACE_ENTRY("SAFE:ESTOP");
    Services::g_commandQueue.emergencyStop();
    TRACE_EXIT("SAFE:ESTOP");
    return Result::OK;
}

Result clearFault(char *activeFaults, uint32_t activeFaultsSize) {
    Trace::ServiceScope svc(Trace::SVC_SAFETY);
    TRACE_ENTRY("SAFE:CLR");
    // Safety check: read powerSTEP01 STATUS register before allowing clear
    uint16_t statusReg = g_safetyActions->getMotorStatusReg();
    if (statusReg != 0) {
        bool ocd   = !(statusReg & (1 << 13));  // Overcurrent (bit 13, active-low)
        uint8_t thStatus = (statusReg >> 11) & 0x3;  // Bits 11-12: TH_STATUS
        bool th_sd = (thStatus >= 2);                   // Bridge or device shutdown
        bool uvlo  = !(statusReg & (1 << 9));         // Undervoltage (active-low)
        if (ocd || th_sd || uvlo) {
            if (activeFaults != nullptr && activeFaultsSize > 0) {
                snprintf(activeFaults, activeFaultsSize,
                         "Hardware fault active:%s%s%s",
                         ocd ? " OCD" : "", th_sd ? " TH_SD" : "",
                         uvlo ? " UVLO" : "");
            }
            TRACE_EXIT("SAFE:CLR", static_cast<uint32_t>(Result::FAULT_ACTIVE));
            return Result::FAULT_ACTIVE;
        }
    }

    Services::QueueResult result = Services::g_commandQueue.clearFault();
    if (result == Services::QueueResult::OK) {
        g_safetyActions->clearCommsTimeout();
        TRACE_EXIT("SAFE:CLR", static_cast<uint32_t>(Result::OK));
        return Result::OK;
    }
    TRACE_EXIT("SAFE:CLR", static_cast<uint32_t>(Result::INVALID_STATE));
    return Result::INVALID_STATE;
}

Result forceClearFault() {
    Trace::ServiceScope svc(Trace::SVC_SAFETY);
    TRACE_ENTRY("SAFE:FCLR");
    // Skip hardware fault check — force clear regardless
    Services::QueueResult result = Services::g_commandQueue.clearFault();
    if (result == Services::QueueResult::OK) {
        g_safetyActions->clearCommsTimeout();
        TRACE_EXIT("SAFE:FCLR", static_cast<uint32_t>(Result::OK));
        return Result::OK;
    }
    TRACE_EXIT("SAFE:FCLR", static_cast<uint32_t>(Result::INVALID_STATE));
    return Result::INVALID_STATE;
}

uint32_t setHeartbeatTimeout(uint32_t ms) {
    Trace::ServiceScope svc(Trace::SVC_SAFETY);
    TRACE_ENTRY("SAFE:HB:TO", ms);
    uint32_t accepted = g_safetyActions->setHeartbeatTimeout(ms);
    TRACE_EXIT("SAFE:HB:TO", accepted);
    return accepted;
}

void heartbeatReceived(uint32_t seq) {
    Trace::ServiceScope svc(Trace::SVC_SAFETY);
    TRACE_ENTRY("SAFE:HB", seq);
    g_safetyActions->heartbeatReceived(seq);
    TRACE_EXIT("SAFE:HB", seq);
}

void getHeartbeatStatus(HeartbeatStatus &status) {
    g_safetyActions->getHeartbeatStatus(
        status.enabled, status.timeoutMs, status.lastSeq,
        status.remainingMs, status.timedOut);
}

} // namespace Services::Safety
