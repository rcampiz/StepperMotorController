/**
 * @file display_task.hpp
 * @brief Display refresh task for ST7789 LCD
 *
 * Supports two operation modes:
 * - LOCAL: Periodically updates LCD with telemetry data, joystick navigates pages
 * - REMOTE: Upstream controls display via commands, joystick events forwarded
 *
 * Priority: Lowest (tskIDLE_PRIORITY + 1)
 */

#ifndef DISPLAY_TASK_HPP
#define DISPLAY_TASK_HPP

#include "harness/pins/ischeduler.hpp"
#include <stddef.h>
#include <stdint.h>

// Forward declarations
namespace Harness { class ICanvas; class IJoystick; class IClock;
                    class IRemoteDisplay; class IFlashImageAccess;
                    class IIndicatorRenderer; }

namespace Scheduler {

// Task configuration
constexpr uint32_t DISPLAY_TASK_STACK_SIZE = 640;  // uxTaskGetSystemState + snprintf + trig
constexpr uint32_t DISPLAY_TASK_PRIORITY = 1;
constexpr uint32_t DISPLAY_REFRESH_PERIOD_MS = 50; // 20 Hz refresh

/**
 * @brief Display pages/screens for LOCAL mode
 */
enum class DisplayPage : uint8_t {
    Status,        // Main status (position, speed, encoder)
    MotorDetail,   // Detailed motor info
    EncoderDetail, // Detailed encoder info
    System,        // System info (uptime, heap, etc.)
    Debug          // Debug/log output
};

/**
 * @brief Initialize display task resources
 *
 * Stores injected dependencies, sets up screen manager and menus.
 * Call before vTaskStartScheduler().
 *
 * @param lcd         LCD driver (wired by composition root)
 * @param joystick    Joystick driver (may be nullptr if not available)
 * @param clock       System clock (for delays and tick timing)
 * @param flashImages Flash image storage service
 * @param indicator   Motion indicator rendering service
 * @param scheduler   Scheduler for suspend/resume (injected, no Platform::resources)
 * @return true on success
 */
bool DisplayTask_Init(Harness::ICanvas& lcd, Harness::IJoystick* joystick,
                      Harness::IClock& clock,
                      Harness::IFlashImageAccess& flashImages,
                      Harness::IIndicatorRenderer& indicator,
                      Harness::IScheduler& scheduler);

/**
 * @brief Display task entry point
 * @param pvParameters Unused
 */
void vDisplayTask(void* pvParameters);

/**
 * @brief Set current display page (LOCAL mode only)
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
 */
void DisplayTask_Refresh();

// =============================================================================
// Remote Rendering API (for REMOTE mode)
// =============================================================================

/**
 * @brief Clear the display with a color
 * @param color RGB565 color value
 */
void DisplayTask_RemoteClear(uint16_t color);

/**
 * @brief Draw text at position
 * @param x, y Top-left position
 * @param text Null-terminated string
 * @param fg Foreground color (RGB565)
 * @param bg Background color (RGB565)
 */
void DisplayTask_RemoteText(uint16_t x, uint16_t y, const char* text,
                            uint16_t fg, uint16_t bg);

/**
 * @brief Draw a rectangle
 * @param x, y Top-left position
 * @param w, h Width and height
 * @param color RGB565 color
 * @param filled true for filled rectangle, false for outline
 */
void DisplayTask_RemoteRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            uint16_t color, bool filled);

/**
 * @brief Draw a line
 * @param x0, y0 Start point
 * @param x1, y1 End point
 * @param color RGB565 color
 */
void DisplayTask_RemoteLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                            uint16_t color);

/**
 * @brief Draw a bitmap from raw RGB565 data
 * @param x, y Top-left position
 * @param w, h Bitmap dimensions
 * @param data Pointer to RGB565 byte data (big-endian)
 * @param len Length of data in bytes
 */
void DisplayTask_RemoteBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                              const uint8_t* data, size_t len);

/**
 * @brief Draw motion indicator (arrow / rotation ring / chevrons)
 * @param angle_deg Direction angle (0-359, 0=up, clockwise)
 * @param rotation_dir Rotation direction (-1=CCW, 0=none, 1=CW)
 * @param has_translation Whether translational motion is active
 */
void DisplayTask_RemoteIndicator(uint16_t angle_deg, int8_t rotation_dir,
                                  bool has_translation);

/**
 * @brief Get pointer to LCD driver (for advanced use)
 * @return Pointer to LCD instance, or nullptr if not initialized
 */
Harness::ICanvas* DisplayTask_GetLCD();

// =============================================================================
// Bitmap Streaming API (for binary transfer)
// =============================================================================

/**
 * @brief Start streaming bitmap data to display
 *
 * Sets up the LCD window for receiving raw RGB565 data.
 * Must call DisplayTask_StreamBitmapData() to send data,
 * and DisplayTask_StreamBitmapEnd() when done.
 *
 * @param x, y Top-left position
 * @param w, h Bitmap dimensions
 * @return true if streaming started, false if LCD not initialized or already streaming
 */
bool DisplayTask_StreamBitmapStart(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

/**
 * @brief Stream bitmap data chunk
 *
 * Must be called between Start and End.
 *
 * @param data Pointer to raw RGB565 byte data
 * @param len Number of bytes to transfer
 */
void DisplayTask_StreamBitmapData(const uint8_t* data, size_t len);

/**
 * @brief End bitmap streaming
 *
 * Releases LCD resources held by Start.
 */
void DisplayTask_StreamBitmapEnd();

/**
 * @brief Check if bitmap streaming is active
 * @return true if currently streaming
 */
bool DisplayTask_IsStreaming();

// =============================================================================
// Task Control API
// =============================================================================

/**
 * @brief Display task handle (for suspend/resume)
 */
extern Harness::TaskHandle g_displayTaskHandle;

/**
 * @brief Suspend display task
 *
 * Stops all LCD updates and joystick reads.
 * Useful for isolating SPI1 for motor-only testing.
 */
void DisplayTask_Suspend();

/**
 * @brief Resume display task
 */
void DisplayTask_Resume();

/**
 * @brief Get IRemoteDisplay interface implemented by this task
 * @return Pointer to static IRemoteDisplay instance (valid after init)
 */
Harness::IRemoteDisplay* DisplayTask_GetRemoteInterface();

} // namespace Scheduler

#endif // DISPLAY_TASK_HPP
