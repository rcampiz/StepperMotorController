/**
 * @file display_task.cpp
 * @brief Display task implementation with dual-mode UI support
 */

#include "F_platform/tasks/display_task.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include "L4_drivers/devices/joystick.hpp"
#include "L4_drivers/spi/spi_bus.hpp"
#include "L5_board/spi/spi_manager.hpp"
#include "L2_protocol/telemetry.hpp"
#include "L3_services/infra/indicator_service.hpp"
#include "ui/ui_mode.hpp"
#include "ui/screen_manager.hpp"
#include "ui/menu_screen.hpp"
#include "ui/screens/boot_color_screen.hpp"
#include "ui/screens/device_info_screen.hpp"
#include "ui/screens/encoder_screen.hpp"
#include "ui/screens/motion_screen.hpp"
#include "ui/screens/config_screen.hpp"
#include "ui/screens/graph_screen.hpp"
#include "ui/screens/image_view_screen.hpp"
#include "ui/screens/trace_screen.hpp"
#include "ui/screens/task_monitor_screen.hpp"
#include "ui/screens/dispatcher_screen.hpp"
#include "ui/screens/arch_screen.hpp"
#include "L3_services/infra/flash_image_service.hpp"
#include "L3_services/infra/trace.hpp"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/task.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace Tasks {

// Helper to convert Joystick::Direction to UI::JoyDirection
static UI::JoyDirection convertDirection(Joystick::Direction dir);

// Screen manager and screen instances (static allocation, no heap)
static UI::ScreenManager s_screenManager;
static UI::MenuScreen s_mainMenu("Main Menu");
static UI::BootColorScreen s_bootScreen;
static UI::DeviceInfoScreen s_deviceInfoScreen;
static UI::EncoderScreen s_encoderScreen;
static UI::MotionScreen s_motionScreen;
static UI::ConfigScreen s_configScreen;
static UI::GraphScreen s_graphScreen;
static UI::MenuScreen s_imageMenu("Flash Images");
static UI::MenuScreen s_imageActionMenu;
static UI::ImageViewScreen s_imageViewScreen;
static uint32_t s_imageSlotMap[UI::MENU_MAX_ITEMS];
static uint32_t s_activeSlot = 0;
static UI::TraceScreen s_traceScreen;
static UI::TaskMonitorScreen s_taskMonitorScreen;
static UI::DispatcherScreen s_dispatcherScreen;
static UI::ArchScreen s_archScreen;

// Forward declaration
static void populateImageMenu();

// Image action callback: View or Erase
static bool onImageActionSelect(uint8_t index) {
    if (index == 0) {
        // View
        s_imageViewScreen.setSlot(s_activeSlot);
        s_screenManager.push(&s_imageViewScreen);
        return true;
    } else if (index == 1) {
        // Erase
        Services::g_flashImageService.eraseSlot(s_activeSlot);
        populateImageMenu();  // Refresh browser to reflect deletion
        return false;  // Pop action menu, back to browser
    }
    return true;
}

// Populate image menu by scanning flash slots
static void populateImageMenu() {
    s_imageMenu.clearItems();

    auto& svc = Services::g_flashImageService;
    if (!svc.isAvailable()) {
        s_imageMenu.addItem("Flash unavailable", nullptr, false);
        return;
    }

    uint32_t maxSlots = svc.maxSlots();
    uint8_t found = 0;
    uint8_t probe[4];

    for (uint32_t slot = 0; slot < maxSlots && found < UI::MENU_MAX_ITEMS; slot++) {
        if (svc.readSlotChunk(slot, 0, probe, 4)) {
            // Non-0xFF means slot has image data (erased flash = all 0xFF)
            if (probe[0] != 0xFF || probe[1] != 0xFF ||
                probe[2] != 0xFF || probe[3] != 0xFF) {
                char name[UI::MENU_ITEM_NAME_LEN];
                snprintf(name, sizeof(name), "Image %lu", (unsigned long)slot);
                s_imageSlotMap[found] = slot;
                s_imageMenu.addItem(name);
                found++;
            }
        }
    }

    if (found == 0) {
        s_imageMenu.addItem("No images found", nullptr, false);
    }
}

// Image menu callback: show View/Erase actions for selected image
static bool onImageMenuSelect(uint8_t index) {
    s_activeSlot = s_imageSlotMap[index];

    char title[UI::MENU_ITEM_NAME_LEN];
    snprintf(title, sizeof(title), "Image %lu", (unsigned long)s_activeSlot);

    s_imageActionMenu.clearItems();
    s_imageActionMenu.setTitle(title);
    s_imageActionMenu.addItem("View");
    s_imageActionMenu.addItem("Erase");
    s_imageActionMenu.setSelectionCallback(onImageActionSelect);
    s_imageActionMenu.setSelectedIndex(0);

    s_screenManager.push(&s_imageActionMenu);
    return true;
}

// Menu callback: push the selected screen
static bool onMainMenuSelect(uint8_t index) {
    UI::IScreen* screens[] = {
        &s_bootScreen, &s_deviceInfoScreen, &s_encoderScreen,
        &s_motionScreen, &s_configScreen, &s_graphScreen
    };
    if (index < 6) {
        s_screenManager.push(screens[index]);
    } else if (index == 6) {
        populateImageMenu();
        s_imageMenu.setSelectionCallback(onImageMenuSelect);
        s_screenManager.push(&s_imageMenu);
    } else if (index == 7) {
        s_screenManager.push(&s_traceScreen);
    } else if (index == 8) {
        s_screenManager.push(&s_taskMonitorScreen);
    } else if (index == 9) {
        s_screenManager.push(&s_dispatcherScreen);
    } else if (index == 10) {
        s_screenManager.push(&s_archScreen);
    }
    return true;  // Stay in menu (it remains on the stack under the new screen)
}

// Long-press detection
static uint8_t s_centerHoldCount = 0;
static constexpr uint8_t LONG_PRESS_POLLS = 20; // 1000ms at 20Hz

// Auto-repeat for held directional buttons (UP/DOWN/LEFT/RIGHT)
static uint8_t s_holdCount = 0;
static constexpr uint8_t HOLD_DELAY_POLLS = 8;   // 400ms before repeat starts
static constexpr uint8_t HOLD_REPEAT_POLLS = 2;  // 100ms between repeats

// Driver instances (created in init)
static SPIBus* s_spi = nullptr;
static LCD* s_lcd = nullptr;
static Joystick* s_joystick = nullptr;

bool DisplayTask_Init()
{
    // UI mode manager is initialized by main.cpp (composition root)

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

    // Initialize indicator service (service layer, no RTOS dependency)
    Services::g_indicatorService.init(s_lcd);

    // Initialize joystick
    s_joystick = new Joystick();

    // Set up main menu
    s_mainMenu.addItem("Boot Color Screen");
    s_mainMenu.addItem("Device Info");
    s_mainMenu.addItem("Encoder Monitor");
    s_mainMenu.addItem("Motion Control");
    s_mainMenu.addItem("Driver Config");
    s_mainMenu.addItem("Telemetry Graph");
    s_mainMenu.addItem("Flash Images");
    s_mainMenu.addItem("Trace Monitor");
    s_mainMenu.addItem("Task Monitor");
    s_mainMenu.addItem("Dispatcher");
    s_mainMenu.addItem("Architecture");
    s_mainMenu.setSelectionCallback(onMainMenuSelect);

    // Initialize screen manager with main menu as root
    s_screenManager.init(&s_mainMenu, s_lcd);

    // Boot into color test screen (any button → main menu)
    s_screenManager.push(&s_bootScreen);

    return true;
}

void vDisplayTask(void* pvParameters)
{
    (void)pvParameters;

    TickType_t lastWakeTime = xTaskGetTickCount();
    Joystick::Direction lastDir = Joystick::Direction::None;

    while (true) {
        Trace::setCurrentTaskId(Trace::TASK_DISPLAY);
        Trace::setCurrentServiceId(Trace::SVC_UI);

        // Handle joystick input
        if (s_joystick != nullptr) {
            Joystick::Direction dir = s_joystick->readDirection();

            // In REMOTE mode, forward joystick events upstream
            if (UI::g_uiMode.getMode() == UI::UIMode::REMOTE) {
                if (dir != lastDir) {
                    bool wasPressed = (dir != Joystick::Direction::None);
                    UI::JoyEvent event;
                    event.direction = convertDirection(dir);
                    event.pressed = wasPressed;
                    event.timestamp = xTaskGetTickCount();
                    UI::g_uiMode.reportJoyEvent(event);
                    lastDir = dir;
                }
                s_centerHoldCount = 0;
            } else {
                // LOCAL mode: long-press CENTER detection
                if (dir == Joystick::Direction::Center) {
                    s_centerHoldCount++;
                    if (s_centerHoldCount == LONG_PRESS_POLLS) {
                        // Long-press CENTER → return to main menu
                        s_screenManager.popToRoot();
                        lastDir = dir;
                        // Skip normal edge handling this cycle
                        goto render;
                    }
                } else {
                    s_centerHoldCount = 0;
                }

                // Edge-detected input → screen manager
                if (dir != lastDir) {
                    if (dir != Joystick::Direction::None) {
                        s_screenManager.handleInput(convertDirection(dir), true);
                    }
                    lastDir = dir;
                    s_holdCount = 0;
                } else if (dir == Joystick::Direction::Up
                        || dir == Joystick::Direction::Down) {
                    // Auto-repeat for held UP/DOWN only (LEFT/RIGHT switch modes)
                    s_holdCount++;
                    if (s_holdCount >= HOLD_DELAY_POLLS
                        && (s_holdCount % HOLD_REPEAT_POLLS) == 0) {
                        s_screenManager.handleInput(convertDirection(dir), true);
                    }
                }
            }
        }

render:
        // Render current screen in LOCAL mode
        if (s_lcd != nullptr && UI::g_uiMode.getMode() == UI::UIMode::LOCAL) {
            s_screenManager.render();
        }

        // Wait for next refresh cycle
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(DISPLAY_REFRESH_PERIOD_MS));
    }
}

void DisplayTask_SetPage(DisplayPage /*page*/)
{
    // Legacy API — no-op with screen manager
}

DisplayPage DisplayTask_GetPage()
{
    return DisplayPage::Status;
}

void DisplayTask_Refresh()
{
    // Legacy API — screen manager handles refresh automatically
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

void DisplayTask_RemoteIndicator(uint16_t angle_deg, int8_t rotation_dir,
                                  bool has_translation)
{
    Services::g_indicatorService.draw({angle_deg, rotation_dir, has_translation});
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

} // namespace Tasks
