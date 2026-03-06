/**
 * @file system_dispatch.cpp
 * @brief L3 ISystemDispatcher implementation
 */

#include "system_dispatch.hpp"
#include "L3_services/infra/tick_timer/tick_timer.hpp"
#include "L3_services/infra/timing_service/timing_service.hpp"
#include "L3_services/config/device_config/device_config.hpp"
#include "L3_services/motion/motion.hpp"

namespace Services {

uint32_t SystemDispatch::getTickUs() {
    return TickTimer_GetTick();
}

uint32_t SystemDispatch::getTickMs() {
    return m_clock->getTickMs();
}

void SystemDispatch::setTransportDelay(uint32_t ms) {
    Timing::setTransportDelay(ms);
}

uint32_t SystemDispatch::getTransportDelay() {
    return Timing::getTransportDelay();
}

void SystemDispatch::getDeviceInfo(uint16_t& deviceId, uint8_t& role) {
    deviceId = g_deviceConfig.getDeviceId();
    role = static_cast<uint8_t>(g_deviceConfig.getRole());
}

bool SystemDispatch::setDeviceId(uint16_t id) {
    return g_deviceConfig.setDeviceId(id);
}

Harness::ServiceStatus SystemDispatch::setRole(uint8_t role) {
    auto r = static_cast<WheelRole>(role);
    if (g_deviceConfig.setRole(r)) {
        return Harness::ServiceStatus::Ok;
    }
    return Harness::ServiceStatus::FlashError;
}

void SystemDispatch::enableEvents(uint8_t mask, uint16_t currentStatus) {
    motion.stepper.event.enable(mask, currentStatus);
}

void SystemDispatch::disableEvents() {
    motion.stepper.event.disable();
}

Harness::ISystemDispatcher::EventStats SystemDispatch::getEventStats() {
    MotorEventStats st = motion.stepper.event.getStats();
    return {st.sent, st.lostCritical, st.lostInfo, st.enableMask, st.queueDepth};
}

uint32_t SystemDispatch::getLastEventSeq() {
    return (m_eventSeqFn != nullptr) ? m_eventSeqFn() : 0;
}

} // namespace Services
