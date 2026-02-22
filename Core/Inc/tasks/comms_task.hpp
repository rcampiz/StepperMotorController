/**
 * @file comms_task.hpp
 * @brief Communications task for command/telemetry
 *
 * Parses commands from Raspberry Pi, dispatches to MotorTask.
 * Publishes periodic telemetry.
 * Priority: High (tskIDLE_PRIORITY + 3)
 */

#ifndef COMMS_TASK_HPP
#define COMMS_TASK_HPP

#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>

namespace Tasks {

// Task configuration
constexpr uint32_t COMMS_TASK_STACK_SIZE = 2048;  // 8192 bytes (GET_STATUS JSON uses 640B local buf + snprintf)
constexpr UBaseType_t COMMS_TASK_PRIORITY = tskIDLE_PRIORITY + 3;
constexpr TickType_t COMMS_POLL_PERIOD_MS = 10; // Command polling
constexpr TickType_t TELEMETRY_PERIOD_MS = 100; // Telemetry publish rate

/**
 * @brief Transport type selection
 */
enum class TransportType : uint8_t {
  VCP_UART, // USART2 via ST-LINK/J-Link Virtual COM Port
  RTT       // SEGGER RTT channel 0
};

/**
 * @brief Initialize comms task resources
 *
 * Initializes selected transport and command parser.
 * Call before vTaskStartScheduler().
 *
 * @param transport Transport to use
 * @return true on success
 */
bool CommsTask_Init(TransportType transport = TransportType::VCP_UART);

/**
 * @brief Comms task entry point
 * @param pvParameters Unused
 */
void vCommsTask(void *pvParameters);

/**
 * @brief Enable/disable periodic telemetry publishing
 * @param enable true to enable
 */
void CommsTask_EnableTelemetry(bool enable);

/**
 * @brief Check if telemetry is enabled
 * @return true if enabled
 */
bool CommsTask_IsTelemetryEnabled();

/**
 * @brief Send joystick event upstream (for REMOTE mode)
 * @param direction Direction string (LEFT, RIGHT, UP, DOWN, CENTER, NONE)
 * @param pressed true if pressed, false if released
 */
void CommsTask_SendJoyEvent(const char* direction, bool pressed);

/**
 * @brief Register joystick callback with UI mode manager
 *
 * Call after both CommsTask_Init and UI mode init to set up
 * joystick event forwarding in REMOTE mode.
 */
void CommsTask_RegisterJoyCallback();

// -------------------------------------------------------------------------
// Heartbeat watchdog API
// -------------------------------------------------------------------------

constexpr uint32_t HEARTBEAT_TIMEOUT_MIN_MS = 100;
constexpr uint32_t HEARTBEAT_TIMEOUT_MAX_MS = 5000;

/**
 * @brief Notify that a HEARTBEAT command was received
 * @param seq Sequence number from client
 */
void CommsTask_HeartbeatReceived(uint32_t seq);

/**
 * @brief Configure heartbeat watchdog timeout
 * @param timeout_ms Timeout in ms (0=disabled, clamped to [100,5000])
 * @return Accepted timeout value (may be clamped)
 */
uint32_t CommsTask_SetHeartbeatTimeout(uint32_t timeout_ms);

/**
 * @brief Query heartbeat watchdog status
 */
void CommsTask_GetHeartbeatStatus(
    bool& out_enabled, uint32_t& out_timeout_ms,
    uint32_t& out_last_seq, uint32_t& out_remaining_ms,
    bool& out_timed_out);

/**
 * @brief Clear comms timeout latch and disable watchdog
 */
void CommsTask_ClearCommsTimeout();

/**
 * @brief Get the last event wire sequence number (for EVENT_STATUS)
 * @return Most recently assigned event seq (0 if no events sent)
 */
uint32_t CommsTask_GetLastEventSeq();

} // namespace Tasks

#endif // COMMS_TASK_HPP
