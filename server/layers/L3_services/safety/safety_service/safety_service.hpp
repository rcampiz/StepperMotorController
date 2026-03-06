/**
 * @file safety_service.hpp
 * @brief Safety service — unifies ESTOP, heartbeat watchdog, and fault clear
 *
 * Namespace free functions (no class, no vtable).
 * CLEAR_FAULT checks powerSTEP01 STATUS register before allowing clear.
 */

#pragma once
#include <stdint.h>

namespace Services::Safety {

enum class Result : uint8_t { OK, FAULT_ACTIVE, INVALID_STATE };

struct HeartbeatStatus {
    bool enabled;
    uint32_t timeoutMs;
    uint32_t lastSeq;
    uint32_t remainingMs;
    bool timedOut;
};

Result emergencyStop();

/**
 * @brief Clear fault latch after verifying hardware faults are gone
 * @param activeFaults If non-null, receives string of active fault names on failure
 * @param activeFaultsSize Size of activeFaults buffer
 * @return OK on success, FAULT_ACTIVE if hardware faults remain, INVALID_STATE if not in fault
 */
Result clearFault(char *activeFaults = nullptr, uint32_t activeFaultsSize = 0);

/**
 * @brief Force clear fault latch without checking hardware faults
 * @return OK on success, INVALID_STATE if not in fault
 */
Result forceClearFault();

uint32_t setHeartbeatTimeout(uint32_t ms);
void heartbeatReceived(uint32_t seq);
void getHeartbeatStatus(HeartbeatStatus &status);

} // namespace Services::Safety
