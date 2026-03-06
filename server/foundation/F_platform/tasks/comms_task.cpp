/**
 * @file comms_task.cpp
 * @brief Communications task — pure scheduling shell
 *
 * Receives pre-wired Hardware::CommsHw from the composition root.
 * No FreeRTOS types, no object construction, no L4/L5 includes.
 */

#include "F_platform/tasks/comms_task.hpp"
#include "harness/pins/icommand_processor.hpp"
#include "harness/pins/iclock.hpp"
#include "harness/pins/itransport.hpp"
#include "harness/pins/itelemetry.hpp"
#include "F_platform/types/async_event_types.hpp"
#include "harness/pins/imotor_event_receiver.hpp"
#include "harness/pins/iemergency_stop.hpp"
#include "harness/pins/itrace_context.hpp"
#include "F_platform/ui/ui_mode.hpp"
#include "harness/trace/interface_trace.hpp"
#include <stdint.h>
#include <string.h>

namespace Scheduler {

// Simple integer-to-string helpers (no printf dependency)
static void intToStr(int32_t val, char* buf) {
    if (val < 0) {
        *buf++ = '-';
        val = -val;
    }
    char tmp[12];
    int i = 0;
    do {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    } while (val > 0);
    while (i > 0) {
        *buf++ = tmp[--i];
    }
    *buf = '\0';
}

static void uintToStr(uint32_t val, char* buf) {
    char tmp[12];
    int i = 0;
    do {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    } while (val > 0);
    while (i > 0) {
        *buf++ = tmp[--i];
    }
    *buf = '\0';
}

// Forward declaration
static void publishTelemetry();

// Context from composition root
static Harness::ITransport* s_transport = nullptr;
static Harness::ICommandProcessor* s_parser = nullptr;
static Harness::IClock* s_clock = nullptr;
static Harness::IMotorEventReceiver* s_eventReceiver = nullptr;
static Harness::IEmergencyStop* s_emergencyStop = nullptr;

// Telemetry publishing state
static bool s_telemetryEnabled = false;
static uint32_t s_lastTelemetryTime = 0;

// Heartbeat watchdog state (all accessed from CommsTask context only)
static uint32_t s_heartbeatTimeoutMs = 0;   // 0 = disabled
static uint32_t s_lastHeartbeatMs = 0;
static uint32_t s_lastHeartbeatSeq = 0;
static bool s_commsTimedOut = false;

// Async event send state (accessed from CommsTask context only)
static uint32_t s_eventSeq = 0;  // monotonic wire sequence number

bool CommsTask_Init(Harness::ITransport& transport,
                    Harness::ICommandProcessor& parser,
                    Harness::IClock& clock,
                    Harness::IMotorEventReceiver& eventReceiver,
                    Harness::IEmergencyStop& emergencyStop)
{
    s_transport     = &transport;
    s_parser        = &parser;
    s_clock         = &clock;
    s_eventReceiver = &eventReceiver;
    s_emergencyStop = &emergencyStop;
    return true;
}

void vCommsTask(void* pvParameters)
{
    (void)pvParameters;

    // Wait for system startup
    s_clock->delayMs(200);

    // Print welcome banner
    if (s_transport != nullptr) {
        s_transport->println("");
        s_transport->println("========================================");
        s_transport->println("   Stepper Motor Controller v0.1.0");
        s_transport->println("========================================");
        s_transport->println("Type 'HELP' for available commands");
        s_transport->println("");
    }

    uint32_t lastWakeMs = s_clock->getTickMs();

    while (true) {
        Harness::setTraceTaskId(Harness::TASK_COMMS);
        Harness::setTraceServiceId(Harness::SVC_COMMS);

        // Process incoming commands
        if (s_parser != nullptr) {
            s_parser->process();
            s_parser->checkBaudRevert();
        }

        // Publish periodic telemetry if enabled
        if (s_telemetryEnabled) {
            uint32_t now = s_clock->getTickMs();
            if ((now - s_lastTelemetryTime) >= TELEMETRY_PERIOD_MS) {
                s_lastTelemetryTime = now;
                publishTelemetry();
            }
        }

        // Drain and send async events
        if (s_transport != nullptr && s_parser != nullptr) {
            AsyncEvent evt;
            while (s_eventReceiver->receive(evt)) {
                s_eventSeq++;
                uint32_t ts_ms = s_clock->getTickMs();
                s_parser->formatEvent(*s_transport, evt, s_eventSeq, ts_ms);
            }
        }

        // Check heartbeat watchdog
        if (s_heartbeatTimeoutMs > 0 && !s_commsTimedOut) {
            uint32_t elapsed = s_clock->getTickMs() - s_lastHeartbeatMs;
            if (elapsed >= s_heartbeatTimeoutMs) {
                s_commsTimedOut = true;
                ITRACE(Harness::ITrace::L3_F_SAFETY, "[L3~L3]", "hbTimeout");
                s_emergencyStop->emergencyStop();
            }
        }

        // Sleep until next poll
        s_clock->sleepUntilMs(lastWakeMs, COMMS_POLL_PERIOD_MS);
    }
}

void CommsTask_EnableTelemetry(bool enable)
{
    s_telemetryEnabled = enable;
    if (enable && s_clock != nullptr) {
        s_lastTelemetryTime = s_clock->getTickMs();
    }
}

bool CommsTask_IsTelemetryEnabled()
{
    return s_telemetryEnabled;
}

// Internal: publish telemetry snapshot
static void publishTelemetry()
{
    if (s_transport == nullptr) {
        return;
    }

    Protocol::TelemetrySnapshot snap = Harness::telemetry().getSnapshot();

    // Format and send telemetry line
    // Format: TELEM: pos=<pos> spd=<spd> enc=<enc> vel=<vel>
    char buf[32];

    s_transport->print("TELEM: pos=");
    intToStr(snap.motor.position, buf);
    s_transport->print(buf);

    s_transport->print(" spd=");
    uintToStr(snap.motor.speed, buf);
    s_transport->print(buf);

    s_transport->print(" enc=");
    intToStr(snap.encoder.count, buf);
    s_transport->print(buf);

    s_transport->print(" vel=");
    intToStr(snap.encoder.velocity, buf);
    s_transport->print(buf);

    s_transport->println("");
}

void CommsTask_SendJoyEvent(const char* direction, bool pressed)
{
    if (s_transport == nullptr) {
        return;
    }

    // Format: EVENT JOY <direction> <pressed|released>
    s_transport->print("EVENT JOY ");
    s_transport->print(direction);
    s_transport->println(pressed ? " pressed" : " released");
}

// Callback for UI mode manager joystick events
static void joyEventCallback(const UI::JoyEvent& event)
{
    const char* dirName = UI::UIModeManager::directionName(event.direction);
    CommsTask_SendJoyEvent(dirName, event.pressed);
}

void CommsTask_RegisterJoyCallback()
{
    UI::g_uiMode.setJoyEventCallback(joyEventCallback);
}

// =========================================================================
// Heartbeat watchdog API
// =========================================================================

void CommsTask_HeartbeatReceived(uint32_t seq)
{
    if (s_clock != nullptr) {
        s_lastHeartbeatMs = s_clock->getTickMs();
    }
    s_lastHeartbeatSeq = seq;
}

uint32_t CommsTask_SetHeartbeatTimeout(uint32_t timeout_ms)
{
    if (timeout_ms == 0) {
        s_heartbeatTimeoutMs = 0;
        s_commsTimedOut = false;
        return 0;
    }
    if (timeout_ms < HEARTBEAT_TIMEOUT_MIN_MS) {
        timeout_ms = HEARTBEAT_TIMEOUT_MIN_MS;
    }
    if (timeout_ms > HEARTBEAT_TIMEOUT_MAX_MS) {
        timeout_ms = HEARTBEAT_TIMEOUT_MAX_MS;
    }
    s_heartbeatTimeoutMs = timeout_ms;
    if (s_clock != nullptr) {
        s_lastHeartbeatMs = s_clock->getTickMs();
    }
    s_commsTimedOut = false;
    return timeout_ms;
}

void CommsTask_GetHeartbeatStatus(
    bool& out_enabled, uint32_t& out_timeout_ms,
    uint32_t& out_last_seq, uint32_t& out_remaining_ms,
    bool& out_timed_out)
{
    out_enabled = (s_heartbeatTimeoutMs > 0);
    out_timeout_ms = s_heartbeatTimeoutMs;
    out_last_seq = s_lastHeartbeatSeq;
    out_timed_out = s_commsTimedOut;

    if (s_heartbeatTimeoutMs > 0 && !s_commsTimedOut && s_clock != nullptr) {
        uint32_t elapsed = s_clock->getTickMs() - s_lastHeartbeatMs;
        if (elapsed < s_heartbeatTimeoutMs) {
            out_remaining_ms = s_heartbeatTimeoutMs - elapsed;
        } else {
            out_remaining_ms = 0;
        }
    } else {
        out_remaining_ms = 0;
    }
}

void CommsTask_ClearCommsTimeout()
{
    s_commsTimedOut = false;
    if (s_clock != nullptr) {
        s_lastHeartbeatMs = s_clock->getTickMs();
    }
}

// =========================================================================
// Async event wire sequence accessor
// =========================================================================

uint32_t CommsTask_GetLastEventSeq()
{
    return s_eventSeq;
}

void CommsTask_GetDispatchStats(CommsDispatchStats& out)
{
    if (s_parser == nullptr) {
        memset(&out, 0, sizeof(out));
        return;
    }
    Harness::DispatchStats src = {};
    s_parser->getDispatchStats(src);
    out.totalCommands = src.totalCommands;
    out.unknownCommands = src.unknownCommands;
    out.parseErrors = src.parseErrors;
    out.recentCount = src.recentCount;
    memcpy(out.recentCmds, src.recentCmds, sizeof(out.recentCmds));
}

// IDispatchStats adapter — bridges Scheduler stats to Harness interface
class CommsDispatchStatsAdapter : public Harness::IDispatchStats {
public:
    void getStats(Harness::DispatchStats& out) override {
        if (s_parser != nullptr) {
            s_parser->getDispatchStats(out);
        } else {
            memset(&out, 0, sizeof(out));
        }
    }
};

static CommsDispatchStatsAdapter s_dispatchStatsAdapter;

Harness::IDispatchStats* CommsTask_GetDispatchStatsInterface()
{
    return &s_dispatchStatsAdapter;
}

} // namespace Scheduler
