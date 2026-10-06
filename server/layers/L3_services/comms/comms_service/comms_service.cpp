/**
 * @file comms_service.cpp
 * @brief L3 CommsService implementation (moved from F_platform/tasks/comms_task.cpp)
 */

#include "L3_services/comms/comms_service/comms_service.hpp"
#include "harness/pins/async_event.hpp"
#include "harness/trace/interface_trace.hpp"
#include <string.h>

namespace Services {

CommsService g_commsService;
CommsService* CommsService::s_self = nullptr;

void CommsService::init(Protocol::ICommandProcessor& processor,
                        Platform::IClock& clock,
                        IMotorEventReceiver& events,
                        IEmergencyStop& emergencyStop,
                        TelemetryStore& telemetry,
                        UI::UIModeManager* uiMode,
                        IMotorTaskControl* motorCtrl)
{
    s_self = this;
    m_processor = &processor;
    m_clock = &clock;
    m_events = &events;
    m_emergencyStop = &emergencyStop;
    m_telemetry = &telemetry;
    m_motorCtrl = motorCtrl;
    if (uiMode != nullptr) {
        uiMode->setJoyEventCallback(&CommsService::joyEventCallback);
    }
}

void CommsService::onStart()
{
    if (m_processor != nullptr) {
        m_processor->emitBanner();
    }
}

void CommsService::tick()
{
    if (m_processor == nullptr || m_clock == nullptr) {
        return;
    }

    // Process incoming commands
    m_processor->process();
    m_processor->checkBaudRevert();

    // Publish periodic telemetry if enabled
    if (m_telemetryPeriodMs > 0 && m_telemetry != nullptr) {
        uint32_t now = m_clock->getTickMs();
        if ((now - m_lastTelemetryTime) >= m_telemetryPeriodMs) {
            m_lastTelemetryTime = now;
            m_processor->emitTelemetry(m_telemetry->getSnapshot());
        }
    }

    // Drain and send async events
    if (m_events != nullptr) {
        AsyncEvent evt;
        while (m_events->receive(evt)) {
            m_eventSeq++;
            m_processor->emitEvent(evt, m_eventSeq, m_clock->getTickMs());
        }
    }

    // Heartbeat watchdog
    if (m_heartbeatTimeoutMs > 0 && !m_commsTimedOut) {
        uint32_t elapsed = m_clock->getTickMs() - m_lastHeartbeatMs;
        if (elapsed >= m_heartbeatTimeoutMs) {
            m_commsTimedOut = true;
            ITRACE(Harness::ITrace::L3_F_SAFETY, "[L3~L3]", "hbTimeout");
            if (m_emergencyStop != nullptr) {
                m_emergencyStop->emergencyStop();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// ISafetyActions
// ---------------------------------------------------------------------------

uint16_t CommsService::getMotorStatusReg()
{
    Harness::MotorDebugParams info;
    if (m_motorCtrl != nullptr && m_motorCtrl->getDebugInfo(info)) {
        return info.status;
    }
    return 0;
}

void CommsService::clearCommsTimeout()
{
    m_commsTimedOut = false;
    if (m_clock != nullptr) {
        m_lastHeartbeatMs = m_clock->getTickMs();
    }
}

uint32_t CommsService::setHeartbeatTimeout(uint32_t timeoutMs)
{
    if (timeoutMs == 0) {
        m_heartbeatTimeoutMs = 0;
        m_commsTimedOut = false;
        return 0;
    }
    if (timeoutMs < HEARTBEAT_TIMEOUT_MIN_MS) timeoutMs = HEARTBEAT_TIMEOUT_MIN_MS;
    if (timeoutMs > HEARTBEAT_TIMEOUT_MAX_MS) timeoutMs = HEARTBEAT_TIMEOUT_MAX_MS;
    m_heartbeatTimeoutMs = timeoutMs;
    if (m_clock != nullptr) {
        m_lastHeartbeatMs = m_clock->getTickMs();
    }
    m_commsTimedOut = false;
    return timeoutMs;
}

void CommsService::heartbeatReceived(uint32_t seq)
{
    if (m_clock != nullptr) {
        m_lastHeartbeatMs = m_clock->getTickMs();
    }
    m_lastHeartbeatSeq = seq;
}

void CommsService::getHeartbeatStatus(bool& enabled, uint32_t& timeoutMs,
                                      uint32_t& lastSeq, uint32_t& remainingMs,
                                      bool& timedOut)
{
    enabled = (m_heartbeatTimeoutMs > 0);
    timeoutMs = m_heartbeatTimeoutMs;
    lastSeq = m_lastHeartbeatSeq;
    timedOut = m_commsTimedOut;
    remainingMs = 0;
    if (m_heartbeatTimeoutMs > 0 && !m_commsTimedOut && m_clock != nullptr) {
        uint32_t elapsed = m_clock->getTickMs() - m_lastHeartbeatMs;
        remainingMs = (elapsed < m_heartbeatTimeoutMs) ? (m_heartbeatTimeoutMs - elapsed) : 0;
    }
}

// ---------------------------------------------------------------------------
// IDispatchStats / misc
// ---------------------------------------------------------------------------

void CommsService::getStats(DispatchStats& out)
{
    if (m_processor != nullptr) {
        m_processor->getDispatchStats(out);
    } else {
        memset(&out, 0, sizeof(out));
    }
}

void CommsService::enableTelemetry(bool enable)
{
    setTelemetryPeriod(enable ? TELEMETRY_PERIOD_MS : 0);
}

uint32_t CommsService::setTelemetryPeriod(uint32_t ms)
{
    if (ms == 0) {
        m_telemetryPeriodMs = 0;
        return 0;
    }
    if (ms < TELEMETRY_PERIOD_MIN_MS) ms = TELEMETRY_PERIOD_MIN_MS;
    if (ms > TELEMETRY_PERIOD_MAX_MS) ms = TELEMETRY_PERIOD_MAX_MS;
    m_telemetryPeriodMs = ms;
    if (m_clock != nullptr) {
        m_lastTelemetryTime = m_clock->getTickMs();
    }
    return ms;
}

uint32_t CommsService::setTelemetryPeriodStatic(uint32_t ms)
{
    return (s_self != nullptr) ? s_self->setTelemetryPeriod(ms) : 0;
}

uint32_t CommsService::getTelemetryPeriodStatic()
{
    return (s_self != nullptr) ? s_self->m_telemetryPeriodMs : 0;
}

uint32_t CommsService::lastEventSeqStatic()
{
    return (s_self != nullptr) ? s_self->m_eventSeq : 0;
}

void CommsService::joyEventCallback(const UI::JoyEvent& event)
{
    if (s_self != nullptr && s_self->m_processor != nullptr) {
        s_self->m_processor->emitJoystick(event.direction, event.pressed);
    }
}

} // namespace Services
