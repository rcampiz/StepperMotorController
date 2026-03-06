/**
 * @file display_task.cpp
 * @brief Display task implementation with dual-mode UI support
 */

#include "F_platform/tasks/display_task.hpp"
#include "harness/pins/iclock.hpp"
#include "harness/pins/iremote_display.hpp"
#include "harness/pins/icanvas.hpp"
#include "harness/pins/ijoystick.hpp"
#include "harness/pins/iflash_image_access.hpp"
#include "harness/pins/iindicator_renderer.hpp"
#include "F_platform/ui/ui_mode.hpp"
#include "ui/screen_manager/screen_manager.hpp"
#include "ui/menu_screen/menu_screen.hpp"
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
#include "harness/pins/itrace_context.hpp"
#include "harness/trace/interface_trace.hpp"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace Scheduler {


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

// Injected dependencies (set by DisplayTask_Init)
static Harness::ICanvas* s_lcd = nullptr;
static Harness::IJoystick* s_joystick = nullptr;
static Harness::IClock* s_clock = nullptr;
static Harness::IFlashImageAccess* s_flashImages = nullptr;
static Harness::IIndicatorRenderer* s_indicator = nullptr;
static Harness::IScheduler* s_scheduler = nullptr;

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
        s_flashImages->eraseSlot(s_activeSlot);
        populateImageMenu();  // Refresh browser to reflect deletion
        return false;  // Pop action menu, back to browser
    }
    return true;
}

// Populate image menu by scanning flash slots
static void populateImageMenu() {
    s_imageMenu.clearItems();

    if (!s_flashImages->isAvailable()) {
        s_imageMenu.addItem("Flash unavailable", nullptr, false);
        return;
    }

    uint32_t maxSlots = s_flashImages->maxSlots();
    uint8_t found = 0;
    uint8_t probe[4];

    for (uint32_t slot = 0; slot < maxSlots && found < UI::MENU_MAX_ITEMS; slot++) {
        if (s_flashImages->readSlotChunk(slot, 0, probe, 4)) {
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

bool DisplayTask_Init(Harness::ICanvas& lcd, Harness::IJoystick* joystick,
                      Harness::IClock& clock,
                      Harness::IFlashImageAccess& flashImages,
                      Harness::IIndicatorRenderer& indicator,
                      Harness::IScheduler& scheduler)
{
    s_lcd = &lcd;
    s_joystick = joystick;
    s_clock = &clock;
    s_flashImages = &flashImages;
    s_indicator = &indicator;
    s_scheduler = &scheduler;

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

    uint32_t lastWakeMs = s_clock->getTickMs();
    UI::JoyDirection lastDir = UI::JoyDirection::NONE;

    while (true) {
        Harness::setTraceTaskId(Harness::TASK_DISPLAY);
        Harness::setTraceServiceId(Harness::SVC_UI);

        // Handle joystick input
        if (s_joystick != nullptr) {
            UI::JoyDirection dir = s_joystick->readDirection();

            // In REMOTE mode, forward joystick events upstream
            if (UI::g_uiMode.getMode() == UI::UIMode::REMOTE) {
                if (dir != lastDir) {
                    bool wasPressed = (dir != UI::JoyDirection::NONE);
                    UI::JoyEvent event;
                    event.direction = dir;
                    event.pressed = wasPressed;
                    event.timestamp = s_clock->getTickMs();
                    UI::g_uiMode.reportJoyEvent(event);
                    lastDir = dir;
                }
                s_centerHoldCount = 0;
            } else {
                // LOCAL mode: long-press CENTER detection
                if (dir == UI::JoyDirection::CENTER) {
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
                    if (dir != UI::JoyDirection::NONE) {
                        s_screenManager.handleInput(dir, true);
                    }
                    lastDir = dir;
                    s_holdCount = 0;
                } else if (dir == UI::JoyDirection::UP
                        || dir == UI::JoyDirection::DOWN) {
                    // Auto-repeat for held UP/DOWN only (LEFT/RIGHT switch modes)
                    s_holdCount++;
                    if (s_holdCount >= HOLD_DELAY_POLLS
                        && (s_holdCount % HOLD_REPEAT_POLLS) == 0) {
                        s_screenManager.handleInput(dir, true);
                    }
                }
            }
        }

render:
        // Render current screen in LOCAL mode
        if (s_lcd != nullptr && UI::g_uiMode.getMode() == UI::UIMode::LOCAL) {
            ITRACE(Harness::ITrace::L4_LCD, "[UI>L4]", "lcd.render");
            s_screenManager.render();
        }

        // Wait for next refresh cycle
        s_clock->sleepUntilMs(lastWakeMs, DISPLAY_REFRESH_PERIOD_MS);
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
    s_indicator->draw({angle_deg, rotation_dir, has_translation});
}

Harness::ICanvas* DisplayTask_GetLCD()
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

// Task handle for suspend/resume (opaque, managed by IScheduler)
Harness::TaskHandle g_displayTaskHandle = nullptr;

void DisplayTask_Suspend()
{
    s_scheduler->suspendTask(g_displayTaskHandle);
}

void DisplayTask_Resume()
{
    s_scheduler->resumeTask(g_displayTaskHandle);
}

// ============================================================================
// Harness interface implementation (eliminates wiring adapter)
// ============================================================================

class RemoteDisplayAccess : public Harness::IRemoteDisplay {
public:
    void clear(uint16_t color) override {
        DisplayTask_RemoteClear(color);
    }
    void text(uint16_t x, uint16_t y, const char* t,
              uint16_t fg, uint16_t bg) override {
        DisplayTask_RemoteText(x, y, t, fg, bg);
    }
    void rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
              uint16_t color, bool filled) override {
        DisplayTask_RemoteRect(x, y, w, h, color, filled);
    }
    void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
              uint16_t color) override {
        DisplayTask_RemoteLine(x0, y0, x1, y1, color);
    }
    void bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                const uint8_t* data, uint32_t len) override {
        DisplayTask_RemoteBitmap(x, y, w, h, data, len);
    }
    void indicator(uint16_t angle, int8_t rotation, bool translation) override {
        DisplayTask_RemoteIndicator(angle, rotation, translation);
    }
    bool streamStart(uint16_t x, uint16_t y, uint16_t w, uint16_t h) override {
        return DisplayTask_StreamBitmapStart(x, y, w, h);
    }
    void streamData(const uint8_t* data, uint32_t len) override {
        DisplayTask_StreamBitmapData(data, len);
    }
    void streamEnd() override {
        DisplayTask_StreamBitmapEnd();
    }
};

static RemoteDisplayAccess s_remoteAccess;

Harness::IRemoteDisplay* DisplayTask_GetRemoteInterface() { return &s_remoteAccess; }

} // namespace Scheduler
