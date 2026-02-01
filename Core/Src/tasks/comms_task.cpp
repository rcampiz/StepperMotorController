/**
 * @file comms_task.cpp
 * @brief Communications task implementation
 */

#include "tasks/comms_task.hpp"
#include "comms/transport_interface.hpp"
#include "comms/uart_transport.hpp"
#include "comms/rtt_transport.hpp"
#include "comms/command_parser.hpp"
#include "comms/telemetry.hpp"
#include "FreeRTOS.h"
#include "task.h"

namespace Tasks {

// Forward declaration
static void publishTelemetry();

// Transport and parser instances
static Comms::ITransport* s_transport = nullptr;
static Comms::CommandParser* s_parser = nullptr;

// Telemetry publishing state
static bool s_telemetryEnabled = false;
static TickType_t s_lastTelemetryTime = 0;

bool CommsTask_Init(TransportType transport)
{
    // NOTE: Telemetry is initialized in main.cpp before tasks are created

    // Create transport based on selection
    switch (transport) {
        case TransportType::VCP_UART:
            s_transport = new Comms::UartTransport(115200);
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

    // Create command parser
    s_parser = new Comms::CommandParser(*s_transport);
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
        // Process incoming commands
        if (s_parser != nullptr) {
            s_parser->process();
        }

        // Publish periodic telemetry if enabled
        if (s_telemetryEnabled) {
            TickType_t now = xTaskGetTickCount();
            if ((now - s_lastTelemetryTime) >= pdMS_TO_TICKS(TELEMETRY_PERIOD_MS)) {
                s_lastTelemetryTime = now;
                publishTelemetry();
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
    char buf[128];

    // Simple integer-to-string without snprintf (embedded friendly)
    s_transport->print("TELEM: pos=");
    // TODO: Convert snap.motor.position to string
    s_transport->print(" spd=");
    // TODO: Convert snap.motor.speed to string
    s_transport->print(" enc=");
    // TODO: Convert snap.encoder.count to string
    s_transport->print(" vel=");
    // TODO: Convert snap.encoder.velocity to string
    s_transport->println("");

    (void)buf;  // Suppress unused warning for now
}

} // namespace Tasks
