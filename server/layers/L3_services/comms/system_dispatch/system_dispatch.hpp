/**
 * @file system_dispatch.hpp
 * @brief L3 implementation of ISystemDispatcher
 *
 * Timing uses IClock harness pin + L3 TickTimer/TimingService.
 * Device config uses L3 DeviceConfigManager.
 * Events use motion.stepper.event.
 * Event sequence and telemetry period use injected function pointers (CommsService).
 */

#pragma once

#include "L3_services/boundary/isystem_dispatcher.hpp"
#include "F_platform/boundary/iclock.hpp"
#include <stdint.h>

namespace Services {

class SystemDispatch : public Services::ISystemDispatcher {
public:
    using EventSeqFn = uint32_t(*)();
    using TelemetrySetFn = uint32_t(*)(uint32_t);
    using TelemetryGetFn = uint32_t(*)();

    SystemDispatch() = default;
    SystemDispatch(Platform::IClock* clock, EventSeqFn eventSeqFn)
        : m_clock(clock), m_eventSeqFn(eventSeqFn) {}

    void setClock(Platform::IClock* c) { m_clock = c; }
    void setEventSeqFn(EventSeqFn fn) { m_eventSeqFn = fn; }
    void setTelemetryFns(TelemetrySetFn set, TelemetryGetFn get) { m_tlmSetFn = set; m_tlmGetFn = get; }

    uint32_t getTickUs() override;
    uint32_t getTickMs() override;
    void getTelemetry(Protocol::TelemetrySnapshot& out) override;
    void setTransportDelay(uint32_t ms) override;
    uint32_t getTransportDelay() override;
    void getDeviceInfo(uint16_t& deviceId, uint8_t& role) override;
    bool setDeviceId(uint16_t id) override;
    Harness::ServiceStatus setRole(uint8_t role) override;
    void enableEvents(uint8_t mask, uint8_t currentFaults, uint16_t currentStatus) override;
    void disableEvents() override;
    EventStats getEventStats() override;
    uint32_t getLastEventSeq() override;
    uint32_t setTelemetryPeriod(uint32_t ms) override;
    uint32_t getTelemetryPeriod() override;

private:
    Platform::IClock* m_clock = nullptr;
    EventSeqFn m_eventSeqFn = nullptr;
    TelemetrySetFn m_tlmSetFn = nullptr;
    TelemetryGetFn m_tlmGetFn = nullptr;
};

} // namespace Services
