/**
 * @file boundary/isafety_actions.hpp
 * @brief Safety actions invoked by the comms path: emergency stop, motor disable (L3-internal interface)
 */

#pragma once

#include <stdint.h>

namespace Services {

class ISafetyActions {
public:
    virtual ~ISafetyActions() = default;

    /**
     * @brief The motor driver's active faults, decoded by the driver
     * @return false when the driver could not be read (faults then 0)
     */
    virtual bool getMotorFaults(uint8_t& faults) = 0;   ///< Harness::DriverFault bits
    /// True while the out-of-band ESTOP holds the driver in standby (status reads are garbage).
    virtual bool isMotorInStandby() = 0;
    /// Out-of-band ESTOP timing (see IMotorTaskControl::estopTiming).
    virtual bool estopTiming(uint32_t& pinUs, uint32_t& hizUs) = 0;

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

} // namespace Services
