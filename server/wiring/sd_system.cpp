/**
 * @file sd_system.cpp
 * @brief ServiceDispatcher — ISystemDispatcher implementation
 */

#include "service_dispatcher.hpp"
#include "L3_services/infra/tick_timer.hpp"
#include "L3_services/infra/timing_service.hpp"
#include "L3_services/config/device_config.hpp"
#include "L3_services/dispatch/event_service.hpp"
#include "F_platform/tasks/comms_task.hpp"
#include <FreeRTOS.h>
#include <task.h>

uint32_t ServiceDispatcher::getTickUs() {
    return Services::TickTimer_GetTick();
}

uint32_t ServiceDispatcher::getTickMs() {
    return xTaskGetTickCount();
}

void ServiceDispatcher::setTransportDelay(uint32_t ms) {
    Services::Timing::setTransportDelay(ms);
}

uint32_t ServiceDispatcher::getTransportDelay() {
    return Services::Timing::getTransportDelay();
}

void ServiceDispatcher::getDeviceInfo(uint16_t& deviceId, uint8_t& role) {
    deviceId = Services::g_deviceConfig.getDeviceId();
    role = static_cast<uint8_t>(Services::g_deviceConfig.getRole());
}

bool ServiceDispatcher::setDeviceId(uint16_t id) {
    return Services::g_deviceConfig.setDeviceId(id);
}

Comms::ServiceStatus ServiceDispatcher::setRole(uint8_t role) {
    auto r = static_cast<Services::WheelRole>(role);
    if (Services::g_deviceConfig.setRole(r)) {
        return Comms::ServiceStatus::Ok;
    }
    return Comms::ServiceStatus::FlashError;
}

void ServiceDispatcher::enableEvents(uint8_t mask, uint16_t currentStatus) {
    Services::Event::enable(mask, currentStatus);
}

void ServiceDispatcher::disableEvents() {
    Services::Event::disable();
}

Comms::ISystemDispatcher::EventStats ServiceDispatcher::getEventStats() {
    Services::Event::Stats st = Services::Event::getStats();
    return {st.sent, st.lostCritical, st.lostInfo, st.enableMask, st.queueDepth};
}

uint32_t ServiceDispatcher::getLastEventSeq() {
    return Tasks::CommsTask_GetLastEventSeq();
}
