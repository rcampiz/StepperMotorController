/**
 * @file display_service.cpp
 * @brief L3 DisplayService implementation (moved from F_platform/tasks/display_task.cpp)
 */

#include "L3_services/ui/display_service/display_service.hpp"
#include "harness/trace/interface_trace.hpp"
#include <stdio.h>

namespace Services {

DisplayService g_displayService;
DisplayService* DisplayService::s_self = nullptr;

DisplayService::DisplayService()
    : m_mainMenu("Main Menu"),
      m_imageMenu("Flash Images")
{
}

bool DisplayService::init(Drivers::ICanvas& lcd,
                          Drivers::IJoystick* joystick,
                          Platform::IClock& clock,
                          IFlashImageAccess& flashImages,
                          IIndicatorRenderer& indicator,
                          UI::UIModeManager& uiMode)
{
    s_self = this;
    m_lcd = &lcd;
    m_joystick = joystick;
    m_clock = &clock;
    m_flashImages = &flashImages;
    m_indicator = &indicator;
    m_uiMode = &uiMode;

    m_mainMenu.addItem("Boot Color Screen");
    m_mainMenu.addItem("Device Info");
    m_mainMenu.addItem("Encoder Monitor");
    m_mainMenu.addItem("Motion Control");
    m_mainMenu.addItem("Driver Config");
    m_mainMenu.addItem("Telemetry Graph");
    m_mainMenu.addItem("Flash Images");
    m_mainMenu.addItem("Trace Monitor");
    m_mainMenu.addItem("Task Monitor");
    m_mainMenu.addItem("Dispatcher");
    m_mainMenu.addItem("Architecture");
    m_mainMenu.setSelectionCallback(&DisplayService::onMainMenuSelect);

    m_screenManager.init(&m_mainMenu, m_lcd);
    m_screenManager.push(&m_bootScreen);   // boot into colour test; any button -> main menu
    return true;
}

// ---------------------------------------------------------------------------
// Menu callbacks
// ---------------------------------------------------------------------------

bool DisplayService::onImageActionSelect(uint8_t index)
{
    DisplayService& d = *s_self;
    if (index == 0) {                       // View
        d.m_imageViewScreen.setSlot(d.m_activeSlot);
        d.m_screenManager.push(&d.m_imageViewScreen);
        return true;
    }
    if (index == 1) {                       // Erase
        d.m_flashImages->eraseSlot(d.m_activeSlot);
        d.populateImageMenu();
        return false;                       // pop action menu, back to browser
    }
    return true;
}

void DisplayService::populateImageMenu()
{
    m_imageMenu.clearItems();

    if (!m_flashImages->isAvailable()) {
        m_imageMenu.addItem("Flash unavailable", nullptr, false);
        return;
    }

    uint32_t maxSlots = m_flashImages->maxSlots();
    uint8_t found = 0;
    uint8_t probe[4];

    for (uint32_t slot = 0; slot < maxSlots && found < UI::MENU_MAX_ITEMS; slot++) {
        if (m_flashImages->readSlotChunk(slot, 0, probe, 4)) {
            if (probe[0] != 0xFF || probe[1] != 0xFF || probe[2] != 0xFF || probe[3] != 0xFF) {
                char name[UI::MENU_ITEM_NAME_LEN];
                snprintf(name, sizeof(name), "Image %lu", (unsigned long)slot);
                m_imageSlotMap[found] = slot;
                m_imageMenu.addItem(name);
                found++;
            }
        }
    }

    if (found == 0) {
        m_imageMenu.addItem("No images found", nullptr, false);
    }
}

bool DisplayService::onImageMenuSelect(uint8_t index)
{
    DisplayService& d = *s_self;
    d.m_activeSlot = d.m_imageSlotMap[index];

    char title[UI::MENU_ITEM_NAME_LEN];
    snprintf(title, sizeof(title), "Image %lu", (unsigned long)d.m_activeSlot);

    d.m_imageActionMenu.clearItems();
    d.m_imageActionMenu.setTitle(title);
    d.m_imageActionMenu.addItem("View");
    d.m_imageActionMenu.addItem("Erase");
    d.m_imageActionMenu.setSelectionCallback(&DisplayService::onImageActionSelect);
    d.m_imageActionMenu.setSelectedIndex(0);

    d.m_screenManager.push(&d.m_imageActionMenu);
    return true;
}

bool DisplayService::onMainMenuSelect(uint8_t index)
{
    DisplayService& d = *s_self;
    UI::IScreen* screens[] = {
        &d.m_bootScreen, &d.m_deviceInfoScreen, &d.m_encoderScreen,
        &d.m_motionScreen, &d.m_configScreen, &d.m_graphScreen
    };
    if (index < 6) {
        d.m_screenManager.push(screens[index]);
    } else if (index == 6) {
        d.populateImageMenu();
        d.m_imageMenu.setSelectionCallback(&DisplayService::onImageMenuSelect);
        d.m_screenManager.push(&d.m_imageMenu);
    } else if (index == 7) {
        d.m_screenManager.push(&d.m_traceScreen);
    } else if (index == 8) {
        d.m_screenManager.push(&d.m_taskMonitorScreen);
    } else if (index == 9) {
        d.m_screenManager.push(&d.m_dispatcherScreen);
    } else if (index == 10) {
        d.m_screenManager.push(&d.m_archScreen);
    }
    return true;   // stay in menu (it remains on the stack under the new screen)
}

// ---------------------------------------------------------------------------
// Task entry point
// ---------------------------------------------------------------------------

void DisplayService::handleJoystick()
{
    if (m_joystick == nullptr) {
        return;
    }
    UI::JoyDirection dir = m_joystick->readDirection();

    if (m_uiMode->getMode() == UI::UIMode::REMOTE) {
        // Forward joystick edges upstream
        if (dir != m_lastDir) {
            UI::JoyEvent event;
            event.direction = dir;
            event.pressed = (dir != UI::JoyDirection::NONE);
            event.timestamp = m_clock->getTickMs();
            m_uiMode->reportJoyEvent(event);
            m_lastDir = dir;
        }
        m_centerHoldCount = 0;
        return;
    }

    // LOCAL mode: long-press CENTER returns to the main menu
    if (dir == UI::JoyDirection::CENTER) {
        m_centerHoldCount++;
        if (m_centerHoldCount == LONG_PRESS_POLLS) {
            m_screenManager.popToRoot();
            m_lastDir = dir;
            return;   // skip normal edge handling this cycle
        }
    } else {
        m_centerHoldCount = 0;
    }

    if (dir != m_lastDir) {
        if (dir != UI::JoyDirection::NONE) {
            m_screenManager.handleInput(dir, true);
        }
        m_lastDir = dir;
        m_holdCount = 0;
    } else if (dir == UI::JoyDirection::UP || dir == UI::JoyDirection::DOWN) {
        // Auto-repeat for held UP/DOWN only (LEFT/RIGHT switch modes)
        m_holdCount++;
        if (m_holdCount >= HOLD_DELAY_POLLS && (m_holdCount % HOLD_REPEAT_POLLS) == 0) {
            m_screenManager.handleInput(dir, true);
        }
    }
}

void DisplayService::tick()
{
    if (!m_enabled) {
        return;                 // LCD_DISABLE: no joystick polling, no rendering, no SPI1 traffic
    }
    handleJoystick();

    if (m_lcd != nullptr && m_uiMode->getMode() == UI::UIMode::LOCAL) {
        ITRACE(Harness::ITrace::L4_LCD, "[UI>L4]", "lcd.render");
        m_screenManager.render();
    }
}

// ---------------------------------------------------------------------------
// IRemoteDisplay (REMOTE mode drawing, Comms task context)
// ---------------------------------------------------------------------------

void DisplayService::clear(uint16_t color)
{
    if (m_lcd != nullptr) m_lcd->fillScreen(color);
}

void DisplayService::text(uint16_t x, uint16_t y, const char* t, uint16_t fg, uint16_t bg)
{
    if (m_lcd != nullptr && t != nullptr) m_lcd->drawString(x, y, t, fg, bg);
}

void DisplayService::rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color, bool filled)
{
    if (m_lcd == nullptr) return;
    if (filled) m_lcd->fillRect(x, y, w, h, color);
    else        m_lcd->drawRect(x, y, w, h, color);
}

void DisplayService::line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
    if (m_lcd != nullptr) m_lcd->drawLine(x0, y0, x1, y1, color);
}

void DisplayService::bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* data, uint32_t len)
{
    if (m_lcd != nullptr && data != nullptr) m_lcd->drawBitmapRaw(x, y, w, h, data, len);
}

void DisplayService::indicator(uint16_t angle, int8_t rotation, bool translation)
{
    if (m_indicator != nullptr) m_indicator->draw({angle, rotation, translation});
}

bool DisplayService::streamStart(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if (m_lcd == nullptr || m_lcd->isStreaming()) return false;
    m_lcd->streamBitmapStart(x, y, w, h);
    return true;
}

void DisplayService::streamData(const uint8_t* data, uint32_t len)
{
    if (m_lcd != nullptr && data != nullptr) m_lcd->streamBitmapData(data, len);
}

void DisplayService::streamEnd()
{
    if (m_lcd != nullptr) m_lcd->streamBitmapEnd();
}

} // namespace Services
