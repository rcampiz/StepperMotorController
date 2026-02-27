/**
 * @file safety_service.cpp
 * @brief Safety service implementation — ESTOP, heartbeat, fault clear
 */

#include "L3_services/safety/safety_service.hpp"
#include "L3_services/dispatch/command_queue.hpp"
#include "L3_services/infra/trace.hpp"
#include "F_platform/tasks/comms_task.hpp"
#include "F_platform/tasks/motor_task.hpp"
#include <stdio.h>

namespace Services::Safety {

Result emergencyStop() {
    TRACE_ENTRY("SAFE:ESTOP");
    Services::g_commandQueue.emergencyStop();
    TRACE_EXIT("SAFE:ESTOP");
    return Result::OK;
}

Result clearFault(char *activeFaults, uint32_t activeFaultsSize) {
    TRACE_ENTRY("SAFE:CLR");
    // Safety check: read powerSTEP01 STATUS register before allowing clear
    Tasks::MotorDebugInfo info;
    if (Tasks::MotorTask_GetDebugInfo(info)) {
        bool ocd   = !(info.status & (1 << 13));  // Overcurrent (bit 13, active-low)
        uint8_t thStatus = (info.status >> 11) & 0x3;  // Bits 11-12: TH_STATUS
        bool th_sd = (thStatus >= 2);                   // Bridge or device shutdown
        bool uvlo  = !(info.status & (1 << 9));         // Undervoltage (active-low)
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
        Tasks::CommsTask_ClearCommsTimeout();
        TRACE_EXIT("SAFE:CLR", static_cast<uint32_t>(Result::OK));
        return Result::OK;
    }
    TRACE_EXIT("SAFE:CLR", static_cast<uint32_t>(Result::INVALID_STATE));
    return Result::INVALID_STATE;
}

Result forceClearFault() {
    TRACE_ENTRY("SAFE:FCLR");
    // Skip hardware fault check — force clear regardless
    Services::QueueResult result = Services::g_commandQueue.clearFault();
    if (result == Services::QueueResult::OK) {
        Tasks::CommsTask_ClearCommsTimeout();
        TRACE_EXIT("SAFE:FCLR", static_cast<uint32_t>(Result::OK));
        return Result::OK;
    }
    TRACE_EXIT("SAFE:FCLR", static_cast<uint32_t>(Result::INVALID_STATE));
    return Result::INVALID_STATE;
}

uint32_t setHeartbeatTimeout(uint32_t ms) {
    TRACE_ENTRY("SAFE:HB:TO", ms);
    uint32_t accepted = Tasks::CommsTask_SetHeartbeatTimeout(ms);
    TRACE_EXIT("SAFE:HB:TO", accepted);
    return accepted;
}

void heartbeatReceived(uint32_t seq) {
    TRACE_ENTRY("SAFE:HB", seq);
    Tasks::CommsTask_HeartbeatReceived(seq);
    TRACE_EXIT("SAFE:HB", seq);
}

void getHeartbeatStatus(HeartbeatStatus &status) {
    Tasks::CommsTask_GetHeartbeatStatus(
        status.enabled, status.timeoutMs, status.lastSeq,
        status.remainingMs, status.timedOut);
}

} // namespace Services::Safety
