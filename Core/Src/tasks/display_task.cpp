/**
 * @file display_task.cpp
 * @brief Display task implementation with dual-mode UI support
 */

#include "tasks/display_task.hpp"
#include "drivers/lcd_st7789.hpp"
#include "drivers/joystick.hpp"
#include "drivers/spi_bus.hpp"
#include "drivers/spi_manager.hpp"
#include "comms/telemetry.hpp"
#include "ui/ui_mode.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include <stddef.h>
#include <stdint.h>

namespace Tasks {

// Forward declarations for render functions
static void renderStatusPage(LCD& lcd, const Comms::TelemetrySnapshot& telem);
static void renderMotorPage(LCD& lcd, const Comms::TelemetrySnapshot& telem);
static void renderEncoderPage(LCD& lcd, const Comms::TelemetrySnapshot& telem);
static void renderSystemPage(LCD& lcd, const Comms::TelemetrySnapshot& telem);
static void renderDebugPage(LCD& lcd, const Comms::TelemetrySnapshot& telem);

// Helper to convert Joystick::Direction to UI::JoyDirection
static UI::JoyDirection convertDirection(Joystick::Direction dir);

// Display state
static DisplayPage s_currentPage = DisplayPage::Status;
static bool s_refreshPending = false;
static DisplayPage s_lastRenderedPage = DisplayPage::Status;

// Driver instances (created in init)
static SPIBus* s_spi = nullptr;
static LCD* s_lcd = nullptr;
static Joystick* s_joystick = nullptr;

bool DisplayTask_Init()
{
    // Initialize UI mode manager
    if (!UI::g_uiMode.init(UI::UIMode::LOCAL)) {
        return false;
    }

    // Create SPIBus wrapper around SPI1 from manager
    // LCD (ST7789) uses SPI1 with Mode0
    ISPIBus* spi1 = g_spiManager.getSPI1();
    if (spi1 == nullptr) {
        return false;
    }
    s_spi = new SPIBus(*spi1);
    if (s_spi == nullptr) {
        return false;
    }

    // Initialize LCD driver with shared SPI bus
    s_lcd = new LCD(*s_spi);
    if (s_lcd == nullptr) {
        return false;
    }
    s_lcd->init();

    // Simple test: solid red screen
    s_lcd->fillScreen(LCD::RED);

    // Initialize joystick
    s_joystick = new Joystick();

    s_currentPage = DisplayPage::Status;
    s_lastRenderedPage = DisplayPage::Status;
    s_refreshPending = false;  // Don't refresh - keep blue screen

    return true;
}

void vDisplayTask(void* pvParameters)
{
    (void)pvParameters;

    TickType_t lastWakeTime = xTaskGetTickCount();
    Joystick::Direction lastDir = Joystick::Direction::None;

    while (true) {
        // Handle joystick input
        if (s_joystick != nullptr) {
            Joystick::Direction dir = s_joystick->readDirection();

            // Detect new press (edge detection)
            if (dir != lastDir) {
                bool wasPressed = (dir != Joystick::Direction::None);

                // In REMOTE mode, forward joystick events upstream
                if (UI::g_uiMode.getMode() == UI::UIMode::REMOTE) {
                    UI::JoyEvent event;
                    event.direction = convertDirection(dir);
                    event.pressed = wasPressed;
                    event.timestamp = xTaskGetTickCount();
                    UI::g_uiMode.reportJoyEvent(event);
                } else {
                    // LOCAL mode: handle page navigation
                    switch (dir) {
                        case Joystick::Direction::Left:
                            if (s_currentPage > DisplayPage::Status) {
                                s_currentPage = static_cast<DisplayPage>(
                                    static_cast<uint8_t>(s_currentPage) - 1);
                                s_refreshPending = true;
                            }
                            break;

                        case Joystick::Direction::Right:
                            if (s_currentPage < DisplayPage::Debug) {
                                s_currentPage = static_cast<DisplayPage>(
                                    static_cast<uint8_t>(s_currentPage) + 1);
                                s_refreshPending = true;
                            }
                            break;

                        case Joystick::Direction::Center:
                            s_refreshPending = true;
                            break;

                        default:
                            break;
                    }
                }
                lastDir = dir;
            }
        }

        // Update display only in LOCAL mode
        // DISABLED FOR BLUE SCREEN TEST
#if 0
        if (s_lcd != nullptr && UI::g_uiMode.getMode() == UI::UIMode::LOCAL) {
            // Get current telemetry
            Comms::TelemetrySnapshot telem = Comms::g_telemetry.getSnapshot();

            // Clear screen on page change
            if (s_currentPage != s_lastRenderedPage) {
                s_lcd->fillScreen(LCD::BLACK);
                s_lastRenderedPage = s_currentPage;
            }

            // Render current page
            switch (s_currentPage) {
                case DisplayPage::Status:
                    renderStatusPage(*s_lcd, telem);
                    break;

                case DisplayPage::MotorDetail:
                    renderMotorPage(*s_lcd, telem);
                    break;

                case DisplayPage::EncoderDetail:
                    renderEncoderPage(*s_lcd, telem);
                    break;

                case DisplayPage::System:
                    renderSystemPage(*s_lcd, telem);
                    break;

                case DisplayPage::Debug:
                    renderDebugPage(*s_lcd, telem);
                    break;
            }

            s_refreshPending = false;
        }
#endif

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

// =============================================================================
// Helper functions
// =============================================================================

static UI::JoyDirection convertDirection(Joystick::Direction dir)
{
    switch (dir) {
        case Joystick::Direction::Left:   return UI::JoyDirection::LEFT;
        case Joystick::Direction::Right:  return UI::JoyDirection::RIGHT;
        case Joystick::Direction::Up:     return UI::JoyDirection::UP;
        case Joystick::Direction::Down:   return UI::JoyDirection::DOWN;
        case Joystick::Direction::Center: return UI::JoyDirection::CENTER;
        default:                          return UI::JoyDirection::NONE;
    }
}

// =============================================================================
// Remote Rendering API
// =============================================================================

void DisplayTask_RemoteClear(uint16_t color)
{
    if (s_lcd != nullptr) {
        s_lcd->fillScreen(color);
    }
}

void DisplayTask_RemoteText(uint16_t x, uint16_t y, const char* text,
                            uint16_t fg, uint16_t bg)
{
    if (s_lcd != nullptr && text != nullptr) {
        s_lcd->drawString(x, y, text, fg, bg);
    }
}

void DisplayTask_RemoteRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            uint16_t color, bool filled)
{
    if (s_lcd != nullptr) {
        if (filled) {
            s_lcd->fillRect(x, y, w, h, color);
        } else {
            s_lcd->drawRect(x, y, w, h, color);
        }
    }
}

void DisplayTask_RemoteLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                            uint16_t color)
{
    if (s_lcd != nullptr) {
        s_lcd->drawLine(x0, y0, x1, y1, color);
    }
}

void DisplayTask_RemoteBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                              const uint8_t* data, size_t len)
{
    if (s_lcd != nullptr && data != nullptr) {
        s_lcd->drawBitmapRaw(x, y, w, h, data, len);
    }
}

LCD* DisplayTask_GetLCD()
{
    return s_lcd;
}

bool DisplayTask_StreamBitmapStart(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if (s_lcd == nullptr) {
        return false;
    }
    if (s_lcd->isStreaming()) {
        return false;
    }
    s_lcd->streamBitmapStart(x, y, w, h);
    return true;
}

void DisplayTask_StreamBitmapData(const uint8_t* data, size_t len)
{
    if (s_lcd != nullptr && data != nullptr) {
        s_lcd->streamBitmapData(data, len);
    }
}

void DisplayTask_StreamBitmapEnd()
{
    if (s_lcd != nullptr) {
        s_lcd->streamBitmapEnd();
    }
}

bool DisplayTask_IsStreaming()
{
    return s_lcd != nullptr && s_lcd->isStreaming();
}

// Task handle for suspend/resume
TaskHandle_t g_displayTaskHandle = nullptr;

void DisplayTask_Suspend()
{
    if (g_displayTaskHandle != nullptr) {
        vTaskSuspend(g_displayTaskHandle);
    }
}

void DisplayTask_Resume()
{
    if (g_displayTaskHandle != nullptr) {
        vTaskResume(g_displayTaskHandle);
    }
}

// =============================================================================
// Layout constants
static constexpr uint16_t MARGIN_LEFT = 4;
static constexpr uint16_t LINE_HEIGHT = 12;
static constexpr uint16_t VALUE_X = 140;  // Right-aligned value position

// Page rendering functions
static void renderStatusPage(LCD& lcd, const Comms::TelemetrySnapshot& telem)
{
    uint16_t y = 4;

    // Title
    lcd.drawString(MARGIN_LEFT, y, "STATUS", LCD::CYAN, LCD::BLACK);
    y += LINE_HEIGHT + 4;

    // Motor position
    lcd.drawString(MARGIN_LEFT, y, "Position:", LCD::CYAN, LCD::BLACK);
    lcd.drawInt(VALUE_X, y, telem.motor.position, 10, LCD::WHITE, LCD::BLACK);
    y += LINE_HEIGHT;

    // Motor speed
    lcd.drawString(MARGIN_LEFT, y, "Speed:", LCD::CYAN, LCD::BLACK);
    lcd.drawUInt(VALUE_X, y, telem.motor.speed, 10, LCD::WHITE, LCD::BLACK);
    y += LINE_HEIGHT;

    // Motor state
    lcd.drawString(MARGIN_LEFT, y, "State:", LCD::CYAN, LCD::BLACK);
    const char* state;
    if (telem.motor.hiZ) {
        state = "HiZ";
    } else if (telem.motor.busy) {
        state = "Moving";
    } else {
        state = "Idle";
    }
    lcd.drawString(90, y, state, LCD::WHITE, LCD::BLACK);
    y += LINE_HEIGHT;

    // Separator
    y += 4;
    lcd.drawHLine(MARGIN_LEFT, y, LCD::WIDTH - 2 * MARGIN_LEFT, LCD::GRAY);
    y += 8;

    // Encoder count
    lcd.drawString(MARGIN_LEFT, y, "Encoder:", LCD::CYAN, LCD::BLACK);
    lcd.drawInt(VALUE_X, y, telem.encoder.count, 10, LCD::WHITE, LCD::BLACK);
    y += LINE_HEIGHT;

    // Encoder velocity
    lcd.drawString(MARGIN_LEFT, y, "Velocity:", LCD::CYAN, LCD::BLACK);
    lcd.drawInt(VALUE_X, y, telem.encoder.velocity, 10, LCD::WHITE, LCD::BLACK);
    y += LINE_HEIGHT;

    // Index status
    lcd.drawString(MARGIN_LEFT, y, "Index:", LCD::CYAN, LCD::BLACK);
    lcd.drawString(90, y, telem.encoder.indexSeen ? "Yes" : "No", LCD::WHITE, LCD::BLACK);
    y += LINE_HEIGHT;

    // Separator
    y += 4;
    lcd.drawHLine(MARGIN_LEFT, y, LCD::WIDTH - 2 * MARGIN_LEFT, LCD::GRAY);
    y += 8;

    // Stall indicator
    if (telem.motor.stalled) {
        lcd.drawString(MARGIN_LEFT, y, "** STALL **", LCD::RED, LCD::BLACK);
    }
}

static void renderMotorPage(LCD& lcd, const Comms::TelemetrySnapshot& telem)
{
    uint16_t y = 4;

    lcd.drawString(MARGIN_LEFT, y, "MOTOR DETAIL", LCD::CYAN, LCD::BLACK);
    y += LINE_HEIGHT + 4;

    lcd.drawString(MARGIN_LEFT, y, "Position:", LCD::CYAN, LCD::BLACK);
    lcd.drawInt(VALUE_X, y, telem.motor.position, 10, LCD::WHITE, LCD::BLACK);
    y += LINE_HEIGHT;

    lcd.drawString(MARGIN_LEFT, y, "Target:", LCD::CYAN, LCD::BLACK);
    lcd.drawInt(VALUE_X, y, telem.motor.targetPosition, 10, LCD::WHITE, LCD::BLACK);
    y += LINE_HEIGHT;

    lcd.drawString(MARGIN_LEFT, y, "Speed:", LCD::CYAN, LCD::BLACK);
    lcd.drawUInt(VALUE_X, y, telem.motor.speed, 10, LCD::WHITE, LCD::BLACK);
    y += LINE_HEIGHT;

    lcd.drawString(MARGIN_LEFT, y, "Status:", LCD::CYAN, LCD::BLACK);
    char hexBuf[8];
    hexBuf[0] = '0';
    hexBuf[1] = 'x';
    uint16_t reg = telem.motor.statusReg;
    for (int i = 3; i >= 0; i--) {
        uint8_t nibble = (reg >> (i * 4)) & 0xF;
        hexBuf[5 - i] = nibble < 10 ? ('0' + nibble) : ('A' + (nibble - 10));
    }
    hexBuf[6] = '\0';
    lcd.drawString(90, y, hexBuf, LCD::WHITE, LCD::BLACK);
    y += LINE_HEIGHT;

    y += 4;
    lcd.drawString(MARGIN_LEFT, y, "Busy:", LCD::CYAN, LCD::BLACK);
    lcd.drawString(90, y, telem.motor.busy ? "Yes" : "No ", LCD::WHITE, LCD::BLACK);
    y += LINE_HEIGHT;

    lcd.drawString(MARGIN_LEFT, y, "HiZ:", LCD::CYAN, LCD::BLACK);
    lcd.drawString(90, y, telem.motor.hiZ ? "Yes" : "No ", LCD::WHITE, LCD::BLACK);
    y += LINE_HEIGHT;

    lcd.drawString(MARGIN_LEFT, y, "Stalled:", LCD::CYAN, LCD::BLACK);
    lcd.drawString(90, y, telem.motor.stalled ? "Yes" : "No ",
                   telem.motor.stalled ? LCD::RED : LCD::WHITE, LCD::BLACK);
}

static void renderEncoderPage(LCD& lcd, const Comms::TelemetrySnapshot& telem)
{
    uint16_t y = 4;

    lcd.drawString(MARGIN_LEFT, y, "ENCODER DETAIL", LCD::CYAN, LCD::BLACK);
    y += LINE_HEIGHT + 4;

    lcd.drawString(MARGIN_LEFT, y, "Count:", LCD::CYAN, LCD::BLACK);
    lcd.drawInt(VALUE_X, y, telem.encoder.count, 10, LCD::WHITE, LCD::BLACK);
    y += LINE_HEIGHT;

    lcd.drawString(MARGIN_LEFT, y, "Velocity:", LCD::CYAN, LCD::BLACK);
    lcd.drawInt(VALUE_X, y, telem.encoder.velocity, 10, LCD::WHITE, LCD::BLACK);
    y += LINE_HEIGHT;

    lcd.drawString(MARGIN_LEFT, y, "Index:", LCD::CYAN, LCD::BLACK);
    lcd.drawString(90, y, telem.encoder.indexSeen ? "Seen" : "Not seen", LCD::WHITE, LCD::BLACK);
    y += LINE_HEIGHT;

    lcd.drawString(MARGIN_LEFT, y, "Idx Tick:", LCD::CYAN, LCD::BLACK);
    lcd.drawUInt(VALUE_X, y, telem.encoder.indexTick, 10, LCD::WHITE, LCD::BLACK);
}

static void renderSystemPage(LCD& lcd, const Comms::TelemetrySnapshot& telem)
{
    uint16_t y = 4;

    lcd.drawString(MARGIN_LEFT, y, "SYSTEM", LCD::CYAN, LCD::BLACK);
    y += LINE_HEIGHT + 4;

    // Uptime in seconds
    uint32_t uptimeSec = telem.system.uptimeTicks / configTICK_RATE_HZ;
    lcd.drawString(MARGIN_LEFT, y, "Uptime:", LCD::CYAN, LCD::BLACK);
    lcd.drawUInt(VALUE_X, y, uptimeSec, 10, LCD::WHITE, LCD::BLACK);
    lcd.drawString(VALUE_X + 6, y, "s", LCD::WHITE, LCD::BLACK);
    y += LINE_HEIGHT;

    lcd.drawString(MARGIN_LEFT, y, "Free Heap:", LCD::CYAN, LCD::BLACK);
    lcd.drawUInt(VALUE_X, y, telem.system.freeHeap, 10, LCD::WHITE, LCD::BLACK);
    y += LINE_HEIGHT;

    lcd.drawString(MARGIN_LEFT, y, "CPU Load:", LCD::CYAN, LCD::BLACK);
    lcd.drawUInt(VALUE_X, y, telem.system.cpuLoad, 3, LCD::WHITE, LCD::BLACK);
    lcd.drawString(VALUE_X + 6, y, "%", LCD::WHITE, LCD::BLACK);
}

static void renderDebugPage(LCD& lcd, const Comms::TelemetrySnapshot& telem)
{
    (void)telem;
    uint16_t y = 4;

    lcd.drawString(MARGIN_LEFT, y, "DEBUG", LCD::CYAN, LCD::BLACK);
    y += LINE_HEIGHT + 4;

    lcd.drawString(MARGIN_LEFT, y, "No debug data", LCD::GRAY, LCD::BLACK);
}

} // namespace Tasks
