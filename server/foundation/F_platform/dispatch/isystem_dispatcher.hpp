/**
 * @file isystem_dispatcher.hpp
 * @brief Interface for system-level operations (timing, device, events)
 */

#pragma once

#include "F_platform/types/dispatch_result.hpp"
#include <stdint.h>

namespace Comms {

class ISystemDispatcher {
public:
    virtual ~ISystemDispatcher() = default;

    // Timing
    virtual uint32_t getTickUs() = 0;
    virtual uint32_t getTickMs() = 0;
    virtual void setTransportDelay(uint32_t ms) = 0;
    virtual uint32_t getTransportDelay() = 0;

    // Device config
    virtual void getDeviceInfo(uint16_t& deviceId, uint8_t& role) = 0;
    virtual bool setDeviceId(uint16_t id) = 0;
    virtual ServiceStatus setRole(uint8_t role) = 0;

    // Events
    struct EventStats {
        uint32_t sent;
        uint32_t lostCritical;
        uint32_t lostInfo;
        uint8_t mask;
        uint8_t queueDepth;
    };

    virtual void enableEvents(uint8_t mask, uint16_t currentStatus) = 0;
    virtual void disableEvents() = 0;
    virtual EventStats getEventStats() = 0;

    // Comms diagnostics
    virtual uint32_t getLastEventSeq() = 0;
};

} // namespace Comms
