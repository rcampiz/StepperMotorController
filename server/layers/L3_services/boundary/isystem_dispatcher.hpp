/**
 * @file isystem_dispatcher.hpp
 * @brief Interface for system-level operations (timing, device, events)
 */

#pragma once

#include "harness/pins/dispatch_result.hpp"
#include <stdint.h>
#include "harness/pins/telemetry_types.hpp"

namespace Services {

class ISystemDispatcher {
public:
    virtual ~ISystemDispatcher() = default;

    // Timing
    virtual uint32_t getTickUs() = 0;
    virtual uint32_t getTickMs() = 0;
    /// Copy of the shared telemetry snapshot (serves the L2 status/encoder/driver queries)
    virtual void getTelemetry(Protocol::TelemetrySnapshot& out) = 0;
    virtual void setTransportDelay(uint32_t ms) = 0;
    virtual uint32_t getTransportDelay() = 0;

    // Device config
    virtual void getDeviceInfo(uint16_t& deviceId, uint8_t& role) = 0;
    virtual bool setDeviceId(uint16_t id) = 0;
    virtual Harness::ServiceStatus setRole(uint8_t role) = 0;

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

    // Telemetry push (F9): period in ms, 0 = off. Returns the accepted (clamped) period.
    virtual uint32_t setTelemetryPeriod(uint32_t ms) = 0;
    virtual uint32_t getTelemetryPeriod() = 0;
};

} // namespace Services
