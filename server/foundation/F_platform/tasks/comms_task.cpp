/**
 * @file comms_task.cpp
 * @brief Communications task implementation
 */

#include "F_platform/tasks/comms_task.hpp"
#include "F_platform/rtos/freertos_clock.hpp"
#include "L1_transport/transport_interface.hpp"
#include "L1_transport/uart_transport.hpp"
#include "L1_transport/rtt_transport.hpp"
#include "L2_protocol/command_parser.hpp"
#include "F_platform/adapters/service_dispatcher.hpp"
#include "F_platform/adapters/encoder_adapter.hpp"
#include "F_util/interface_trace.hpp"
#ifdef ENABLE_INTERFACE_TRACE
#include "F_platform/adapters/traced/traced_transport.hpp"
#include "F_platform/adapters/traced/traced_encoder.hpp"
#endif
#include "L2_protocol/telemetry.hpp"
#include "L2_protocol/event_codec.hpp"
#include "L3_services/infra/trace.hpp"
#include "L3_services/dispatch/command_queue.hpp"
#include "L3_services/dispatch/event_service.hpp"
#include "ui/ui_mode.hpp"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/task.h"
#include <stdint.h>
#include <string.h>

namespace Tasks {

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

// Transport and parser instances
static Comms::ITransport* s_transport = nullptr;
static Comms::CommandParser* s_parser = nullptr;

// Telemetry publishing state
static bool s_telemetryEnabled = false;
static TickType_t s_lastTelemetryTime = 0;

// Heartbeat watchdog state (all accessed from CommsTask context only)
static uint32_t s_heartbeatTimeoutMs = 0;   // 0 = disabled
static TickType_t s_lastHeartbeatTick = 0;  // FreeRTOS tick of last HEARTBEAT rx
static uint32_t s_lastHeartbeatSeq = 0;
static bool s_commsTimedOut = false;

// Async event send state (accessed from CommsTask context only)
static uint32_t s_eventSeq = 0;  // monotonic wire sequence number

bool CommsTask_Init(TransportType transport)
{
    // NOTE: Telemetry is initialized in main.cpp before tasks are created

    // Clock for transport timing (static — persists for lifetime of transport)
    static FreeRTOSClock commsClock;

    // Create transport based on selection
    switch (transport) {
        case TransportType::VCP_UART:
            s_transport = new Comms::UartTransport(
                commsClock, 115200,
                configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY + 1);
            break;

        case TransportType::RTT:
            s_transport = new Comms::RttTransport(0);  // Channel 0
            break;
    }

    if (s_transport == nullptr) {
        return false;
    }

    if (!s_transport->init()) {
        return false;
    }

#ifdef ENABLE_INTERFACE_TRACE
    // Wrap transport in traced decorator
    static TracedTransport tracedTransport(*s_transport);
    Comms::ITransport& activeTransport = tracedTransport;
#else
    Comms::ITransport& activeTransport = *s_transport;
#endif

    // Create command dispatcher and parser
    static EncoderAdapter encoderAdapter;
#ifdef ENABLE_INTERFACE_TRACE
    static TracedEncoder tracedEncoder(encoderAdapter);
    static ServiceDispatcher dispatcher(&tracedEncoder);
#else
    static ServiceDispatcher dispatcher(&encoderAdapter);
#endif
    s_parser = new Comms::CommandParser(activeTransport, dispatcher);
    if (s_parser == nullptr) {
        return false;
    }

    return true;
}

void vCommsTask(void* pvParameters)
{
    (void)pvParameters;

    // Wait for system startup
    vTaskDelay(pdMS_TO_TICKS(200));

    // Print welcome banner
    if (s_transport != nullptr) {
        s_transport->println("");
        s_transport->println("========================================");
        s_transport->println("   Stepper Motor Controller v0.1.0");
        s_transport->println("========================================");
        s_transport->println("Type 'HELP' for available commands");
        s_transport->println("");
    }

    TickType_t lastWakeTime = xTaskGetTickCount();

    while (true) {
        Trace::setCurrentTaskId(Trace::TASK_COMMS);
        Trace::setCurrentServiceId(Trace::SVC_COMMS);

        // Process incoming commands
        if (s_parser != nullptr) {
            s_parser->process();
            s_parser->checkBaudRevert();
        }

        // Publish periodic telemetry if enabled
        if (s_telemetryEnabled) {
            TickType_t now = xTaskGetTickCount();
            if ((now - s_lastTelemetryTime) >= pdMS_TO_TICKS(TELEMETRY_PERIOD_MS)) {
                s_lastTelemetryTime = now;
                publishTelemetry();
            }
        }

        // Drain and send async events
        if (s_transport != nullptr && s_parser != nullptr) {
            AsyncEvent evt;
            while (Services::Event::receive(evt)) {
                s_eventSeq++;
                uint32_t ts_ms = static_cast<uint32_t>(xTaskGetTickCount());
                if (s_parser->getFormat() == Comms::ResponseFormat::JSON) {
                    Comms::EventCodec::formatJson(*s_transport, evt, s_eventSeq, ts_ms);
                } else {
                    Comms::EventCodec::formatAscii(*s_transport, evt, s_eventSeq);
                }
            }
        }

        // Check heartbeat watchdog
        if (s_heartbeatTimeoutMs > 0 && !s_commsTimedOut) {
            TickType_t elapsed = xTaskGetTickCount() - s_lastHeartbeatTick;
            if (elapsed >= pdMS_TO_TICKS(s_heartbeatTimeoutMs)) {
                s_commsTimedOut = true;
                ITRACE(ITrace::L3_F_SAFETY, "[L3~L3]", "hbTimeout");
                Services::g_commandQueue.emergencyStop();
            }
        }

        // Sleep until next poll
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(COMMS_POLL_PERIOD_MS));
    }
}

void CommsTask_EnableTelemetry(bool enable)
{
    s_telemetryEnabled = enable;
    if (enable) {
        s_lastTelemetryTime = xTaskGetTickCount();
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

    Comms::TelemetrySnapshot snap = Comms::g_telemetry.getSnapshot();

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
    s_lastHeartbeatTick = xTaskGetTickCount();
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
    s_lastHeartbeatTick = xTaskGetTickCount();
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

    if (s_heartbeatTimeoutMs > 0 && !s_commsTimedOut) {
        TickType_t elapsed = xTaskGetTickCount() - s_lastHeartbeatTick;
        uint32_t elapsed_ms = static_cast<uint32_t>(elapsed);
        if (elapsed_ms < s_heartbeatTimeoutMs) {
            out_remaining_ms = s_heartbeatTimeoutMs - elapsed_ms;
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
    // Reset timer so watchdog doesn't fire during recovery,
    // but keep s_heartbeatTimeoutMs so watchdog stays armed.
    // Client heartbeats are already flowing and will feed the timer.
    // To actually disable the watchdog, use SET_HEARTBEAT 0.
    s_lastHeartbeatTick = xTaskGetTickCount();
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
    const Comms::DispatchStats& src = s_parser->getDispatchStats();
    out.totalCommands = src.totalCommands;
    out.unknownCommands = src.unknownCommands;
    out.parseErrors = src.parseErrors;
    out.recentCount = src.recentCount;
    // Copy recent commands ring buffer (most recent first)
    for (uint8_t i = 0; i < src.recentCount && i < 8; i++) {
        uint8_t idx = (src.recentHead + Comms::DispatchStats::RECENT_SIZE - 1 - i)
                      % Comms::DispatchStats::RECENT_SIZE;
        strncpy(out.recentCmds[i], src.recentCmds[idx], 23);
        out.recentCmds[i][23] = '\0';
    }
}

} // namespace Tasks
