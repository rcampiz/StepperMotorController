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
#include "L3_services/ui/screens/memory_screen.hpp"
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
#include "L3_services/ui/screens/estop_screen.hpp"
#include "L3_services/ui/screen_care.hpp"
#include "L3_services/ui/display_service/panel_sleep.hpp"
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
    // Holds are timed on the clock, not counted in ticks: a tick that runs long (a busy page
    // such as the E-STOP's) stretched a 3 s hold to 6 s (bench 2026-10-19)
    static constexpr uint32_t LONG_PRESS_MS  = 2000;   ///< hold CENTER -> main menu / leave REMOTE
    static constexpr uint32_t HOLD_DELAY_MS  = 400;    ///< UP/DOWN held: first repeat after
    static constexpr uint32_t HOLD_REPEAT_MS = 100;    ///< then one every

private:
    void handleJoystick();
    bool latched() const;
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
    UI::MemoryScreen     m_memoryScreen;
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
    UI::PanelSleep       m_panelSleep;   ///< the saver's last stage: panel black and asleep
    UI::SaverScreen      m_saverScreen;
    UI::RecoveryScreen   m_recoveryScreen;
    UI::ScreenCareScreen m_careScreen;
    UI::EstopScreen      m_estopScreen;
    // E-STOP from the joystick: two CENTER presses within DOUBLE_TAP_MS; clearing it: hold
    // CENTER for CLEAR_MS on the E-STOP page
    static constexpr uint32_t DOUBLE_TAP_MS = 400;
    static constexpr uint32_t CLEAR_MS = 3000;       ///< hold CENTER this long to clear
    UI::JoyDirection m_prevRaw = UI::JoyDirection::NONE;
    uint32_t m_lastTapMs = 0;
    bool     m_clearing = false;     ///< CENTER held (armed) on the E-STOP page
    uint32_t m_clearStartMs = 0;
    bool m_clearArmed = false;       ///< CENTER released once since the page came up
    // A CENTER press waits DOUBLE_TAP_MS before it reaches the screen: if a second press
    // comes, it was an E-STOP and neither press selects anything
    bool m_centerPending = false;
    uint32_t m_centerPendingMs = 0;
    bool m_swallowCenter = false;    ///< the second press of a double tap
    void triggerEstop();
    void attemptClear();
    void estopTick(UI::UIMode mode);
    uint32_t m_lastInputMs = 0;      ///< for the saver's quiet time
    UI::UIMode m_lastMode = UI::UIMode::LOCAL;  ///< to repaint when remote mode ends
    bool     m_bootHandoff = false;  ///< boot colour test -> saver after BOOT_SCREEN_MS
    uint32_t m_bootShownMs = 0;
    static constexpr uint32_t BOOT_SCREEN_MS = 5000;
    uint32_t m_imageSlotMap[UI::MENU_MAX_ITEMS] = {};
    uint32_t m_activeSlot = 0;

    UI::JoyDirection m_lastDir = UI::JoyDirection::NONE;
    bool     m_centerHeld = false;   ///< CENTER down (for the long press)
    bool     m_longPressFired = false;
    uint32_t m_centerDownMs = 0;
    uint32_t m_holdStartMs = 0;      ///< UP/DOWN held since (auto-repeat)
    uint32_t m_lastRepeatMs = 0;
    /// CENTER long press: true once, when CENTER has been held LONG_PRESS_MS
    bool centerLongPress(bool centerDown, uint32_t nowMs);
};

extern DisplayService g_displayService;

} // namespace Services
