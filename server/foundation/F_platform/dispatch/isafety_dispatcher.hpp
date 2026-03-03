/**
 * @file isafety_dispatcher.hpp
 * @brief Interface for safety and heartbeat operations (SYST namespace)
 */

#pragma once

#include "F_platform/types/dispatch_result.hpp"
#include <stdint.h>

namespace Comms {

class ISafetyDispatcher {
public:
    virtual ~ISafetyDispatcher() = default;

    struct HeartbeatStatus {
        bool enabled;
        uint32_t timeoutMs;
        uint32_t lastSeq;
        uint32_t remainingMs;
        bool timedOut;
    };

    virtual void safetyEstop() = 0;
    virtual ServiceStatus safetyClearFault(char* activeFaults, uint32_t bufSize) = 0;
    virtual ServiceStatus safetyForceClearFault() = 0;
    virtual uint32_t safetySetHeartbeatTimeout(uint32_t ms) = 0;
    virtual void safetyHeartbeatReceived(uint32_t seq) = 0;
    virtual HeartbeatStatus safetyGetHeartbeatStatus() = 0;
};

} // namespace Comms
