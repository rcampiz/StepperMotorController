/**
 * @file isafety_actions.hpp
 * @brief Interface for safety-related hardware actions
 *
 * Lets SafetyService query motor status and control heartbeat
 * without directly calling Foundation task APIs.
 */

#pragma once

#include <stdint.h>

namespace Harness {

class ISafetyActions {
public:
    virtual ~ISafetyActions() = default;

    /**
     * @brief Read the motor driver STATUS register
     * @return 16-bit STATUS value (0 on failure)
     */
    virtual uint16_t getMotorStatusReg() = 0;

    /**
     * @brief Clear the comms timeout latch
     */
    virtual void clearCommsTimeout() = 0;

    /**
     * @brief Set heartbeat watchdog timeout
     * @param ms Timeout in milliseconds (0 = disable)
     * @return Accepted timeout value
     */
    virtual uint32_t setHeartbeatTimeout(uint32_t ms) = 0;

    /**
     * @brief Notify that a heartbeat was received
     * @param seq Sequence number from client
     */
    virtual void heartbeatReceived(uint32_t seq) = 0;

    /**
     * @brief Query heartbeat watchdog status
     */
    virtual void getHeartbeatStatus(bool& enabled, uint32_t& timeoutMs,
                                     uint32_t& lastSeq, uint32_t& remainingMs,
                                     bool& timedOut) = 0;
};

// Global instance — set by main.cpp composition root
extern ISafetyActions* g_safetyActions;

} // namespace Harness
