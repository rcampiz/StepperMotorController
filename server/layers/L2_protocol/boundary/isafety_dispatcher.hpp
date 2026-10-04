/**
 * @file boundary/isafetydispatcher.hpp
 * @brief Consumer port — Protocol side of the Services::ISafetyDispatcher harness connection
 *
 * Structurally identical to Services::ISafetyDispatcher.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

#include "harness/pins/dispatch_result.hpp"
#include <stdint.h>

namespace Protocol {

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
    virtual Harness::ServiceStatus safetyClearFault(char* activeFaults, uint32_t bufSize) = 0;
    virtual Harness::ServiceStatus safetyForceClearFault() = 0;
    virtual uint32_t safetySetHeartbeatTimeout(uint32_t ms) = 0;
    virtual void safetyHeartbeatReceived(uint32_t seq) = 0;
    virtual HeartbeatStatus safetyGetHeartbeatStatus() = 0;
};

} // namespace Protocol
