/**
 * @file display_task.cpp
 * @brief Display task implementation
 */

#include "tasks/display_task.hpp"
#include "drivers/lcd_st7789.hpp"
#include "drivers/joystick.hpp"
#include "comms/telemetry.hpp"
#include "FreeRTOS.h"
#include "task.h"

namespace Tasks {

// Display state
static DisplayPage s_currentPage = DisplayPage::Status;
static bool s_refreshPending = false;

// Driver instances (created in init)
static LCD* s_lcd = nullptr;
static Joystick* s_joystick = nullptr;

bool DisplayTask_Init()
{
    // TODO: Initialize SPI bus and LCD driver
    // s_lcd = new LCD(...);
    // s_lcd->init();
    // s_lcd->fillScreen(LCD::BLACK);

    // Initialize joystick
    s_joystick = new Joystick();

    s_currentPage = DisplayPage::Status;
    s_refreshPending = true;

    return true;
}

void vDisplayTask(void* pvParameters)
{
    (void)pvParameters;

    TickType_t lastWakeTime = xTaskGetTickCount();
    Joystick::Direction lastDir = Joystick::Direction::None;

    while (true) {
        // Handle joystick input for page navigation
        if (s_joystick != nullptr) {
            Joystick::Direction dir = s_joystick->readDirection();

            // Detect new press (edge detection)
            if (dir != lastDir) {
                lastDir = dir;

                switch (dir) {
                    case Joystick::Direction::Left:
                        // Previous page
                        if (s_currentPage > DisplayPage::Status) {
                            s_currentPage = static_cast<DisplayPage>(
                                static_cast<uint8_t>(s_currentPage) - 1);
                            s_refreshPending = true;
                        }
                        break;

                    case Joystick::Direction::Right:
                        // Next page
                        if (s_currentPage < DisplayPage::Debug) {
                            s_currentPage = static_cast<DisplayPage>(
                                static_cast<uint8_t>(s_currentPage) + 1);
                            s_refreshPending = true;
                        }
                        break;

                    case Joystick::Direction::Center:
                        // Force refresh
                        s_refreshPending = true;
                        break;

                    default:
                        break;
                }
            }
        }

        // Update display
        if (s_lcd != nullptr) {
            // Get current telemetry
            Comms::TelemetrySnapshot telem = Comms::g_telemetry.getSnapshot();

            // Render current page
            switch (s_currentPage) {
                case DisplayPage::Status:
                    renderStatusPage(telem);
                    break;

                case DisplayPage::MotorDetail:
                    renderMotorPage(telem);
                    break;

                case DisplayPage::EncoderDetail:
                    renderEncoderPage(telem);
                    break;

                case DisplayPage::System:
                    renderSystemPage(telem);
                    break;

                case DisplayPage::Debug:
                    renderDebugPage(telem);
                    break;
            }

            s_refreshPending = false;
        }

        // Wait for next refresh cycle
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(DISPLAY_REFRESH_PERIOD_MS));
    }
}

void DisplayTask_SetPage(DisplayPage page)
{
    s_currentPage = page;
    s_refreshPending = true;
}

DisplayPage DisplayTask_GetPage()
{
    return s_currentPage;
}

void DisplayTask_Refresh()
{
    s_refreshPending = true;
}

// Page rendering functions (stubs)
static void renderStatusPage(const Comms::TelemetrySnapshot& telem)
{
    // TODO: Render main status display
    // Motor position, speed, encoder count
    (void)telem;
}

static void renderMotorPage(const Comms::TelemetrySnapshot& telem)
{
    // TODO: Render detailed motor info
    (void)telem;
}

static void renderEncoderPage(const Comms::TelemetrySnapshot& telem)
{
    // TODO: Render detailed encoder info
    (void)telem;
}

static void renderSystemPage(const Comms::TelemetrySnapshot& telem)
{
    // TODO: Render system info (uptime, heap, etc.)
    (void)telem;
}

static void renderDebugPage(const Comms::TelemetrySnapshot& telem)
{
    // TODO: Render debug/log output
    (void)telem;
}

} // namespace Tasks
