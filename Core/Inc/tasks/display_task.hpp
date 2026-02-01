/**
 * @file display_task.hpp
 * @brief Display refresh task for ST7789 LCD
 *
 * Periodically updates LCD with telemetry data.
 * Handles joystick input for local UI navigation.
 * Priority: Lowest (tskIDLE_PRIORITY + 1)
 */

#ifndef DISPLAY_TASK_HPP
#define DISPLAY_TASK_HPP

#include "FreeRTOS.h"
#include "task.h"
#include <cstdint>

namespace Tasks {

// Task configuration
constexpr uint32_t DISPLAY_TASK_STACK_SIZE = 256;
constexpr UBaseType_t DISPLAY_TASK_PRIORITY = tskIDLE_PRIORITY + 1;
constexpr TickType_t DISPLAY_REFRESH_PERIOD_MS = 100;  // 10 Hz refresh

/**
 * @brief Display pages/screens
 */
enum class DisplayPage : uint8_t {
    Status,         // Main status (position, speed, encoder)
    MotorDetail,    // Detailed motor info
    EncoderDetail,  // Detailed encoder info
    System,         // System info (uptime, heap, etc.)
    Debug           // Debug/log output
};

/**
 * @brief Initialize display task resources
 *
 * Initializes LCD driver, clears screen.
 * Call before vTaskStartScheduler().
 *
 * @return true on success
 */
bool DisplayTask_Init();

/**
 * @brief Display task entry point
 * @param pvParameters Unused
 */
void vDisplayTask(void* pvParameters);

/**
 * @brief Set current display page
 * @param page Page to display
 */
void DisplayTask_SetPage(DisplayPage page);

/**
 * @brief Get current display page
 * @return Current page
 */
DisplayPage DisplayTask_GetPage();

/**
 * @brief Force immediate refresh
 *
 * Signals task to refresh display on next cycle.
 */
void DisplayTask_Refresh();

} // namespace Tasks

#endif // DISPLAY_TASK_HPP
