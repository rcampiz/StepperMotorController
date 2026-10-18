/**
 * @file display_service.hpp
 * @brief L3 DisplayService — owns the local UI (screens, joystick, UI mode) and remote drawing
 *
 * Replaces the body of the Display task. The task sheet only calls tick() every
 * DISPLAY_REFRESH_PERIOD_MS (H_TASKS_L3). The LCD and joystick arrive through H_L3_L4.
 * Also provides the L3-internal IRemoteDisplay port used by the display dispatcher
 * (Comms task context) for REMOTE-mode drawing; serialisation relies on the LOCAL/REMOTE
 * mode check plus the SPI1 bus mutex, as before.
 */
#pragma once

#include "harness/defs/harness_l3_l4.hpp"
#include "L3_services/boundary/idisplay_service.hpp"
#include "L3_services/boundary/idisplay_control.hpp"
#include "L3_services/boundary/iremote_display.hpp"
#include "L3_services/boundary/iflash_image_access.hpp"
#include "L3_services/boundary/iindicator_renderer.hpp"
#include "L3_services/ui/ui_mode/ui_mode.hpp"
#include "L3_services/ui/screen_manager/screen_manager.hpp"
#include "L3_services/ui/menu_screen/menu_screen.hpp"
#include "L3_services/ui/screens/boot_color_screen.hpp"
#include "L3_services/ui/screens/device_info_screen.hpp"
#include "L3_services/ui/screens/encoder_screen.hpp"
#include "L3_services/ui/screens/motion_screen.hpp"
#include "L3_services/ui/screens/config_screen.hpp"
#include "L3_services/ui/screens/graph_screen.hpp"
#include "L3_services/ui/screens/image_view_screen.hpp"
#include "L3_services/ui/screens/trace_screen.hpp"
#include "L3_services/ui/screens/task_monitor_screen.hpp"
#include "L3_services/ui/screens/dispatcher_screen.hpp"
#include "L3_services/ui/screens/arch_screen.hpp"
#include "L3_services/ui/screens/saver_screen.hpp"
#include "L3_services/ui/screens/recovery_screen.hpp"
#include "L3_services/ui/screens/screen_care_screen.hpp"
#include "L3_services/ui/screen_care.hpp"
#include "F_platform/boundary/iclock.hpp"
#include <stdint.h>
#include <stddef.h>

namespace Services {

class DisplayService : public IDisplayService, public IDisplayControl, public IRemoteDisplay {
public:
    DisplayService();

    bool init(Drivers::ICanvas& lcd,
              Drivers::IJoystick* joystick,
              Platform::IClock& clock,
              IFlashImageAccess& flashImages,
              IIndicatorRenderer& indicator,
              UI::UIModeManager& uiMode);

    // IDisplayService (Display task)
    void tick() override;

    // IDisplayControl (debug service, Comms task): flag read by tick(), no task suspend
    void setDisplayEnabled(bool enabled) override { m_enabled = enabled; }
    bool isDisplayEnabled() const override { return m_enabled; }

    // IRemoteDisplay (display dispatcher, Comms task)
    void clear(uint16_t color) override;
    void text(uint16_t x, uint16_t y, const char* t, uint16_t fg, uint16_t bg) override;
    void rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color, bool filled) override;
    void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) override;
    void bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* data, uint32_t len) override;
    void indicator(uint16_t angle, int8_t rotation, bool translation, uint8_t speedPct, uint8_t rotPct) override;
    bool panelRegister(uint8_t reg, const uint8_t* data, uint8_t len) override;
    bool streamStart(uint16_t x, uint16_t y, uint16_t w, uint16_t h) override;
    void streamData(const uint8_t* data, uint32_t len) override;
    void streamEnd() override;

    /// Joystick navigation constants (polls at 20 Hz)
    static constexpr uint8_t LONG_PRESS_POLLS  = 40;   ///< 2000 ms CENTER -> main menu
    static constexpr uint8_t HOLD_DELAY_POLLS  = 8;    ///< 400 ms before auto-repeat
    static constexpr uint8_t HOLD_REPEAT_POLLS = 2;    ///< 100 ms between repeats

private:
    void handleJoystick();
    void updateStatusLine();
    void populateImageMenu();
    static bool onImageActionSelect(uint8_t index);
    static bool onImageMenuSelect(uint8_t index);
    static bool onMainMenuSelect(uint8_t index);
    static void onRunRecovery();
    void careTick(uint32_t nowMs);
    static DisplayService* s_self;   ///< menu callbacks are plain function pointers

    volatile bool       m_enabled = true;   ///< LCD_DISABLE clears it from the Comms task
    Drivers::ICanvas*   m_lcd = nullptr;
    Drivers::IJoystick* m_joystick = nullptr;
    Platform::IClock*   m_clock = nullptr;
    IFlashImageAccess*  m_flashImages = nullptr;
    IIndicatorRenderer* m_indicator = nullptr;
    UI::UIModeManager*  m_uiMode = nullptr;

    UI::ScreenManager    m_screenManager;
    UI::MenuScreen       m_mainMenu;
    UI::BootColorScreen  m_bootScreen;
    UI::DeviceInfoScreen m_deviceInfoScreen;
    UI::EncoderScreen    m_encoderScreen;
    UI::MotionScreen     m_motionScreen;
    UI::ConfigScreen     m_configScreen;
    UI::GraphScreen      m_graphScreen;
    UI::MenuScreen       m_imageMenu;
    UI::MenuScreen       m_imageActionMenu;
    UI::ImageViewScreen  m_imageViewScreen;
    UI::TraceScreen      m_traceScreen;
    UI::TaskMonitorScreen m_taskMonitorScreen;
    UI::DispatcherScreen m_dispatcherScreen;
    UI::ArchScreen       m_archScreen;
    UI::ScreenCareSettings m_care;
    UI::SaverScreen      m_saverScreen;
    UI::RecoveryScreen   m_recoveryScreen;
    UI::ScreenCareScreen m_careScreen;
    uint32_t m_lastInputMs = 0;      ///< for the saver's quiet time
    UI::UIMode m_lastMode = UI::UIMode::LOCAL;  ///< to repaint when remote mode ends
    bool     m_bootHandoff = false;  ///< boot colour test -> saver after BOOT_SCREEN_MS
    uint32_t m_bootShownMs = 0;
    static constexpr uint32_t BOOT_SCREEN_MS = 5000;
    uint32_t m_imageSlotMap[UI::MENU_MAX_ITEMS] = {};
    uint32_t m_activeSlot = 0;

    UI::JoyDirection m_lastDir = UI::JoyDirection::NONE;
    uint8_t m_centerHoldCount = 0;
    uint8_t m_holdCount = 0;
};

extern DisplayService g_displayService;

} // namespace Services
