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
#include <cstdint>

namespace Tasks {

// Task configuration
constexpr uint32_t COMMS_TASK_STACK_SIZE = 512;
constexpr UBaseType_t COMMS_TASK_PRIORITY = tskIDLE_PRIORITY + 3;
constexpr TickType_t COMMS_POLL_PERIOD_MS = 10;      // Command polling
constexpr TickType_t TELEMETRY_PERIOD_MS = 100;      // Telemetry publish rate

/**
 * @brief Transport type selection
 */
enum class TransportType : uint8_t {
    VCP_UART,   // USART2 via ST-LINK/J-Link Virtual COM Port
    RTT         // SEGGER RTT channel 0
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
void vCommsTask(void* pvParameters);

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

} // namespace Tasks

#endif // COMMS_TASK_HPP
