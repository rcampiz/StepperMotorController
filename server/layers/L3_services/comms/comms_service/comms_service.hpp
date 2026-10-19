/**
 * @file comms_service.hpp
 * @brief L3 CommsService — Comms task entry point (H_TASKS_L3)
 *
 * Drives the L2 command processor (H_L2_L3), drains async events, publishes telemetry,
 * runs the heartbeat watchdog and forwards joystick events. Also provides the L3-internal
 * ISafetyActions (heartbeat state, motor status) and IDispatchStats ports.
 * Replaces the body of the Comms task and the SafetyActionsImpl / CommsTask_* API.
 */
#pragma once

#include "L3_services/boundary/icomms_service.hpp"
#include "L3_services/boundary/isafety_actions.hpp"
#include "L3_services/boundary/idispatch_stats.hpp"
#include "L3_services/boundary/imotor_event_receiver.hpp"
#include "L3_services/boundary/iemergency_stop.hpp"
#include "L3_services/boundary/imotor_task_control.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include "L3_services/ui/ui_mode/ui_mode.hpp"
#include "harness/defs/harness_l2_l3.hpp"
#include "F_platform/boundary/iclock.hpp"

namespace Services {

class CommsService : public ICommsService,
                     public ISafetyActions,
                     public IDispatchStats {
public:
    static constexpr uint32_t TELEMETRY_PERIOD_MS = 100;       // default when enabled without a rate
    static constexpr uint32_t TELEMETRY_PERIOD_MIN_MS = 20;    // SYST:TLM:RATE clamp (F9)
    static constexpr uint32_t TELEMETRY_PERIOD_MAX_MS = 5000;
    static constexpr uint32_t HEARTBEAT_TIMEOUT_MIN_MS = 100;
    static constexpr uint32_t HEARTBEAT_TIMEOUT_MAX_MS = 5000;

    void init(Protocol::ICommandProcessor& processor,
              Platform::IClock& clock,
              IMotorEventReceiver& events,
              IEmergencyStop& emergencyStop,
              TelemetryStore& telemetry,
              UI::UIModeManager* uiMode,
              IMotorTaskControl* motorCtrl);

    // ICommsService (Comms task)
    void onStart() override;
    void tick() override;

    // ISafetyActions (SafetyService)
    bool getMotorFaults(uint8_t& faults) override;
    bool isMotorInStandby() override;
    bool estopTiming(uint32_t& pinUs, uint32_t& hizUs) override;
    void clearCommsTimeout() override;
    uint32_t setHeartbeatTimeout(uint32_t ms) override;
    void heartbeatReceived(uint32_t seq) override;
    void getHeartbeatStatus(bool& enabled, uint32_t& timeoutMs,
                            uint32_t& lastSeq, uint32_t& remainingMs,
                            bool& timedOut) override;

    // IDispatchStats (dispatcher screen)
    void getStats(DispatchStats& out) override;

    void enableTelemetry(bool enable);
    bool isTelemetryEnabled() const { return m_telemetryPeriodMs > 0; }
    /// F9: push period in ms (clamped to MIN..MAX), 0 = off. Returns the accepted value.
    uint32_t setTelemetryPeriod(uint32_t ms);
    uint32_t getTelemetryPeriod() const { return m_telemetryPeriodMs; }
    uint32_t getLastEventSeq() const { return m_eventSeq; }
    bool commsTimedOut() const { return m_commsTimedOut; }

    /// Plain-function accessor for SystemDispatch::setEventSeqFn.
    static uint32_t lastEventSeqStatic();
    /// Plain-function accessors for SystemDispatch::setTelemetryFns.
    static uint32_t setTelemetryPeriodStatic(uint32_t ms);
    static uint32_t getTelemetryPeriodStatic();

private:
    static void joyEventCallback(const UI::JoyEvent& event);
    static CommsService* s_self;

    Protocol::ICommandProcessor* m_processor = nullptr;
    Platform::IClock*            m_clock = nullptr;
    IMotorEventReceiver*         m_events = nullptr;
    IEmergencyStop*              m_emergencyStop = nullptr;
    TelemetryStore*              m_telemetry = nullptr;
    IMotorTaskControl*           m_motorCtrl = nullptr;

    uint32_t m_telemetryPeriodMs = 0;     // 0 = off (boot default)
    uint32_t m_lastTelemetryTime = 0;
    uint32_t m_heartbeatTimeoutMs = 0;   // 0 = disabled
    uint32_t m_lastHeartbeatMs = 0;
    uint32_t m_lastHeartbeatSeq = 0;
    bool     m_commsTimedOut = false;
    uint32_t m_eventSeq = 0;             // monotonic wire sequence number
};

extern CommsService g_commsService;

} // namespace Services
