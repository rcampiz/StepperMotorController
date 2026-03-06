/**
 * @file system_dispatch.hpp
 * @brief L3 implementation of ISystemDispatcher
 *
 * Timing uses IClock harness pin + L3 TickTimer/TimingService.
 * Device config uses L3 DeviceConfigManager.
 * Events use motion.stepper.event.
 * Event sequence uses injected function pointer.
 */

#pragma once

#include "harness/pins/isystem_dispatcher.hpp"
#include "harness/pins/iclock.hpp"
#include <stdint.h>

namespace Services {

class SystemDispatch : public Harness::ISystemDispatcher {
public:
    using EventSeqFn = uint32_t(*)();

    SystemDispatch() = default;
    SystemDispatch(Harness::IClock* clock, EventSeqFn eventSeqFn)
        : m_clock(clock), m_eventSeqFn(eventSeqFn) {}

    void setClock(Harness::IClock* c) { m_clock = c; }
    void setEventSeqFn(EventSeqFn fn) { m_eventSeqFn = fn; }

    uint32_t getTickUs() override;
    uint32_t getTickMs() override;
    void setTransportDelay(uint32_t ms) override;
    uint32_t getTransportDelay() override;
    void getDeviceInfo(uint16_t& deviceId, uint8_t& role) override;
    bool setDeviceId(uint16_t id) override;
    Harness::ServiceStatus setRole(uint8_t role) override;
    void enableEvents(uint8_t mask, uint16_t currentStatus) override;
    void disableEvents() override;
    EventStats getEventStats() override;
    uint32_t getLastEventSeq() override;

private:
    Harness::IClock* m_clock = nullptr;
    EventSeqFn m_eventSeqFn = nullptr;
};

} // namespace Services
