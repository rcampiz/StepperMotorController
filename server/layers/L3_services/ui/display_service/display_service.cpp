/**
 * @file display_service.cpp
 * @brief L3 DisplayService implementation (moved from F_platform/tasks/display_task.cpp)
 */

#include "L3_services/ui/display_service/display_service.hpp"
#include "harness/trace/interface_trace.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include "L3_services/motion/motion.hpp"
#include "L3_services/config/device_config/device_config.hpp"
#include "L3_services/ui/screens/encoder_screen.hpp"
#include "L3_services/ui/theme.hpp"
#include "L3_services/safety/safety_service/safety_service.hpp"
#include "L3_services/dispatch/command_queue/command_queue.hpp"
#include <stdio.h>
#include <string.h>

namespace Services {

DisplayService g_displayService;
DisplayService* DisplayService::s_self = nullptr;

DisplayService::DisplayService()
    : m_mainMenu("Main Menu"),
      m_imageMenu("Images"),
      m_saverScreen(&m_care),
      m_careScreen(&m_care, &DisplayService::onRunRecovery)
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

    // Order: what the operator reaches for first; the diagnostics after; care last
    m_mainMenu.addItem("Encoder");
    m_mainMenu.addItem("Motion");
    m_mainMenu.addItem("Driver");
    m_mainMenu.addItem("Telemetry");
    m_mainMenu.addItem("Images");
    m_mainMenu.addItem("Trace");
    m_mainMenu.addItem("Tasks");
    m_mainMenu.addItem("Dispatcher");
    m_mainMenu.addItem("Architecture");
    m_mainMenu.addItem("Device Info");
    m_mainMenu.addItem("Memory");
    m_mainMenu.addItem("Colour test");
    m_mainMenu.addItem("Screen Care");
    m_mainMenu.setSelectionCallback(&DisplayService::onMainMenuSelect);

    m_screenManager.init(&m_mainMenu, m_lcd);
    m_screenManager.setClock(m_clock);      // animations run on time, not on frames
    m_screenManager.push(&m_bootScreen);   // boot into colour test; any button -> main menu
    m_bootHandoff = true;                  // ...and after BOOT_SCREEN_MS, the saver
    m_bootShownMs = m_clock->getTickMs();
    m_lastInputMs = m_bootShownMs;
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
    switch (index) {
        case 0:  d.m_screenManager.push(&d.m_encoderScreen); break;
        case 1:  d.m_screenManager.push(&d.m_motionScreen); break;
        case 2:  d.m_screenManager.push(&d.m_configScreen); break;
        case 3:  d.m_screenManager.push(&d.m_graphScreen); break;
        case 4:
            d.populateImageMenu();
            d.m_imageMenu.setSelectionCallback(&DisplayService::onImageMenuSelect);
            d.m_screenManager.push(&d.m_imageMenu);
            break;
        case 5:  d.m_screenManager.push(&d.m_traceScreen); break;
        case 6:  d.m_screenManager.push(&d.m_taskMonitorScreen); break;
        case 7:  d.m_screenManager.push(&d.m_dispatcherScreen); break;
        case 8:  d.m_screenManager.push(&d.m_archScreen); break;
        case 9:  d.m_screenManager.push(&d.m_deviceInfoScreen); break;
        case 10: d.m_screenManager.push(&d.m_memoryScreen); break;
        case 11: d.m_screenManager.push(&d.m_bootScreen); break;
        case 12: d.m_screenManager.push(&d.m_careScreen); break;
        default: break;
    }
    return true;   // stay in menu (it remains on the stack under the new screen)
}

void DisplayService::onRunRecovery()
{
    DisplayService& d = *s_self;
    d.m_recoveryScreen.setMinutes(d.m_care.recoveryMinutes);
    d.m_screenManager.push(&d.m_recoveryScreen);
}

bool DisplayService::latched() const
{
    const auto st = Services::g_commandQueue.getState();
    return st == Services::ControllerState::ESTOP || st == Services::ControllerState::FAULT;
}

void DisplayService::triggerEstop()
{
    if (latched()) return;                                  // already stopped
    Services::Safety::emergencyStop();
    motion.stepper.event.post(EventType::ESTOP, EVT_SOURCE_LCD);   // the host learns it was the board
}

void DisplayService::attemptClear()
{
    char why[48] = {};
    // Was this stop the host watchdog firing (the host went away)? Then clearing here hands
    // the board to the person at it: the watchdog goes off, or it would fire again a moment
    // later with no host to feed it. The host arms it again when it next connects. A host
    // that is still feeding heartbeats keeps its watchdog.
    Services::Safety::HeartbeatStatus hb{};
    Services::Safety::getHeartbeatStatus(hb);
    const auto r = Services::Safety::clearFault(why, sizeof why);
    if (r == Services::Safety::Result::OK) {
        motion.stepper.event.post(EventType::ESTOP_CLEAR, EVT_SOURCE_LCD);
        if (hb.enabled && hb.timedOut) {
            Services::Safety::setHeartbeatTimeout(0);
            m_estopScreen.setMessage("cleared, host watchdog off");
        } else {
            m_estopScreen.setMessage("cleared");
        }
    } else if (r == Services::Safety::Result::FAULT_ACTIVE) {
        m_estopScreen.setMessage(why);                      // e.g. "Hardware fault active: UVLO"
    } else {
        m_estopScreen.setMessage("not latched");
    }
    m_clearing = false;
    m_estopScreen.setProgress(0);
}

void DisplayService::estopTick(UI::UIMode mode)
{
    const auto st = Services::g_commandQueue.getState();
    const bool isLatched = st == Services::ControllerState::ESTOP || st == Services::ControllerState::FAULT;
    UI::IScreen* top = m_screenManager.current();
    if (isLatched) {
        if (mode == UI::UIMode::REMOTE) {
            m_uiMode->setMode(UI::UIMode::LOCAL);           // the board shows a stop itself
            return;                                         // the mode change repaints next tick
        }
        if (top != &m_estopScreen) {
            if (top == &m_saverScreen || top == &m_recoveryScreen) m_screenManager.pop();
            m_estopScreen.setFault(st == Services::ControllerState::FAULT);
            m_clearing = false;
            m_clearArmed = false;
            m_screenManager.push(&m_estopScreen);
        }
    } else if (top == &m_estopScreen) {
        m_screenManager.pop();
    }
}

void DisplayService::careTick(uint32_t nowMs)
{
    UI::IScreen* top = m_screenManager.current();
    if (top == &m_estopScreen) return;                      // no saver over a stop
    if (top == &m_recoveryScreen) {
        if (m_recoveryScreen.finished()) m_screenManager.pop();
        return;
    }
    if (top == &m_saverScreen) return;
    // Boot: the colour test shows for BOOT_SCREEN_MS, then the saver (waking lands in the
    // menu); a button before then leaves the colour test as usual.
    if (m_bootHandoff) {
        if (top != &m_bootScreen) {
            m_bootHandoff = false;
        } else if ((nowMs - m_bootShownMs) >= BOOT_SCREEN_MS) {
            m_bootHandoff = false;
            m_screenManager.pop();
            m_screenManager.push(&m_saverScreen);
            return;
        }
    }
    if (m_care.saverAfterMs != 0 && (nowMs - m_lastInputMs) >= m_care.saverAfterMs) {
        m_screenManager.push(&m_saverScreen);
    }
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
    // A panel mounted upside down is viewed from the other side: the stick's directions turn
    // with it, so LEFT stays "back" and UP stays "up" for the viewer
    if (m_lcd != nullptr && m_lcd->isRotated180()) {
        switch (dir) {
            case UI::JoyDirection::UP:    dir = UI::JoyDirection::DOWN;  break;
            case UI::JoyDirection::DOWN:  dir = UI::JoyDirection::UP;    break;
            case UI::JoyDirection::LEFT:  dir = UI::JoyDirection::RIGHT; break;
            case UI::JoyDirection::RIGHT: dir = UI::JoyDirection::LEFT;  break;
            default: break;
        }
    }

    // E-STOP: a quick double tap of CENTER, in any mode and on any screen
    if (dir == UI::JoyDirection::CENTER && m_prevRaw != UI::JoyDirection::CENTER) {
        const uint32_t now = m_clock->getTickMs();
        if (m_lastTapMs != 0 && (now - m_lastTapMs) <= DOUBLE_TAP_MS) {
            m_lastTapMs = 0;
            m_centerPending = false;     // the first press never reaches the screen
            m_swallowCenter = true;      // nor does this one
            triggerEstop();
        } else {
            m_lastTapMs = now;
        }
    }
    m_prevRaw = dir;

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
        // The way out from the board itself: hold CENTER 2 s, the same gesture as "home"
        // in local mode. The host's next drawing command finds the board in LOCAL mode.
        const uint32_t now = m_clock->getTickMs();
        const bool fired = centerLongPress(dir == UI::JoyDirection::CENTER, now);
        if (m_indicator != nullptr) {
            // The hold fills the hint plate, so the press is seen to register (owner 2026-10-21)
            uint32_t pct = 0;
            if (m_centerHeld && !m_longPressFired) {
                const uint32_t held = now - m_centerDownMs;
                pct = held >= LONG_PRESS_MS ? 100u : held * 100u / LONG_PRESS_MS;
            }
            m_indicator->setHoldProgress(static_cast<uint8_t>(pct));
        }
        if (fired) {
            m_uiMode->setMode(UI::UIMode::LOCAL);
        }
        return;
    }

    // The E-STOP page owns the stick: holding CENTER fills the bar and clears at the end.
    // The hold only counts once CENTER has been let go since the page came up, so the
    // second press of the double tap that stopped the motor cannot start clearing it.
    if (m_screenManager.current() == &m_estopScreen) {
        m_centerPending = false;
        m_swallowCenter = false;
        if (dir != UI::JoyDirection::CENTER) m_clearArmed = true;
        if (dir == UI::JoyDirection::CENTER && m_clearArmed) {
            const uint32_t now = m_clock->getTickMs();
            if (!m_clearing) {
                m_clearing = true;
                m_clearStartMs = now;
            }
            const uint32_t held = now - m_clearStartMs;
            m_estopScreen.setProgress(static_cast<uint8_t>(held >= CLEAR_MS ? 100u : held * 100u / CLEAR_MS));
            if (held >= CLEAR_MS) attemptClear();      // resets the hold: still held, it starts over
        } else if (m_clearing) {
            m_clearing = false;
            m_estopScreen.setProgress(0);
        }
        m_lastDir = dir;
        (void)centerLongPress(false, 0);
        m_lastInputMs = m_clock->getTickMs();
        return;
    }

    // A CENTER press that was not followed by a second one within the window: deliver it now
    if (m_centerPending && (m_clock->getTickMs() - m_centerPendingMs) > DOUBLE_TAP_MS) {
        m_centerPending = false;
        m_screenManager.handleInput(UI::JoyDirection::CENTER, true);
    }

    // Any move is activity for the saver; the first one while it shows just wakes the panel
    if (dir != UI::JoyDirection::NONE) {
        m_lastInputMs = m_clock->getTickMs();
        if (m_screenManager.current() == &m_saverScreen) {
            if (dir != m_lastDir) m_screenManager.pop();
            m_lastDir = dir;
            (void)centerLongPress(false, 0);
            return;
        }
    }

    // LOCAL mode: long-press CENTER returns to the main menu
    if (centerLongPress(dir == UI::JoyDirection::CENTER, m_clock->getTickMs())) {
        m_screenManager.popToRoot();
        m_lastDir = dir;
        return;   // skip normal edge handling this cycle
    }

    if (dir != m_lastDir) {
        if (dir == UI::JoyDirection::CENTER) {
            if (m_swallowCenter) {
                m_swallowCenter = false;                 // the E-STOP's second press
            } else {
                m_centerPending = true;                  // wait: it may be the first of two
                m_centerPendingMs = m_clock->getTickMs();
            }
        } else if (dir != UI::JoyDirection::NONE) {
            if (m_centerPending) {                       // keep the order of the presses
                m_centerPending = false;
                m_screenManager.handleInput(UI::JoyDirection::CENTER, true);
            }
            m_screenManager.handleInput(dir, true);
        }
        m_lastDir = dir;
        m_holdStartMs = m_clock->getTickMs();
        m_lastRepeatMs = m_holdStartMs;
    } else if (dir == UI::JoyDirection::UP || dir == UI::JoyDirection::DOWN) {
        // Auto-repeat for held UP/DOWN only (LEFT/RIGHT switch modes), on the clock
        const uint32_t now = m_clock->getTickMs();
        if ((now - m_holdStartMs) >= HOLD_DELAY_MS && (now - m_lastRepeatMs) >= HOLD_REPEAT_MS) {
            m_lastRepeatMs = now;
            m_screenManager.handleInput(dir, true);
        }
    }
}

bool DisplayService::centerLongPress(bool centerDown, uint32_t nowMs)
{
    if (!centerDown) {
        m_centerHeld = false;
        m_longPressFired = false;
        return false;
    }
    if (!m_centerHeld) {
        m_centerHeld = true;
        m_centerDownMs = nowMs;
    }
    if (!m_longPressFired && (nowMs - m_centerDownMs) >= LONG_PRESS_MS) {
        m_longPressFired = true;
        return true;
    }
    return false;
}

void DisplayService::tick()
{
    if (!m_enabled) {
        return;                 // LCD_DISABLE: no joystick polling, no rendering, no SPI1 traffic
    }
    handleJoystick();

    // Leaving remote mode (joystick hold or UI:MODE LOCAL): the host drew over the menu, so
    // repaint it from the top and let the indicator start clean next time
    const UI::UIMode mode = m_uiMode->getMode();
    if (mode != m_lastMode) {
        if (mode == UI::UIMode::LOCAL) {
            m_screenManager.popToRoot();
            m_lastInputMs = m_clock->getTickMs();
        }
        if (m_indicator != nullptr) m_indicator->invalidate();
        m_lastMode = mode;
    }

    estopTick(mode);
    hostHoldTick();

    // Panel sleep: the saver's last stage, a black frame and no more traffic (the controller
    // keeps refreshing it from its memory). DISPOFF/SLPIN are not used: this glass shows WHITE
    // with the controller off (bench 2026-10-18). Asleep only while the saver is in charge; a
    // move (the saver pops), an E-STOP page or any client taking the screen wakes it, every time.
    if (m_lcd != nullptr) {
        const bool saverInCharge = mode == UI::UIMode::LOCAL && m_screenManager.current() == &m_saverScreen;
        switch (m_panelSleep.tick(saverInCharge, m_clock->getTickMs(), m_care.sleepAfterMs)) {
            case UI::PanelSleep::Action::SLEEP:
                m_lcd->waitForVSync();
                m_lcd->fillScreen(UI::Theme::BLACK);
                break;
            case UI::PanelSleep::Action::WAKE:
                if (m_indicator != nullptr) m_indicator->invalidate();   // REMOTE: redraw it whole
                break;
            default:
                break;
        }
        if (m_panelSleep.asleep()) return;   // nothing is drawn while the panel is off
    }

    if (mode == UI::UIMode::REMOTE && m_indicator != nullptr) {
        m_indicator->tick(m_clock->getTickMs());   // eases toward the host's last indicator target
    }

    if (m_lcd != nullptr && mode == UI::UIMode::LOCAL) {
        careTick(m_clock->getTickMs());
        updateStatusLine();
        ITRACE(Harness::ITrace::L4_LCD, "[UI>L4]", "lcd.render");
        m_screenManager.render();
    }
}

void DisplayService::updateStatusLine()
{
    Protocol::TelemetrySnapshot t = Services::g_telemetryStore.getSnapshot();
    UI::StatusLine s = {};

    // Left: drive state and the encoder's speed in rpm (the shaft, not the command)
    long rpm = static_cast<long>(static_cast<int64_t>(t.encoder.velocity) * 60 / UI::ENCODER_CPR);
    if (rpm < 0) rpm = -rpm;
    const char* state;
    if (t.motor.stalled)   { state = "STALL"; s.leftColor = UI::Theme::RED; }
    else if (t.motor.hiZ)  { state = "HiZ";   s.leftColor = UI::Theme::DIM; }
    else if (t.motor.busy) { state = "RUN";   s.leftColor = UI::Theme::GREEN; }
    else                   { state = "HOLD";  s.leftColor = UI::Theme::CYAN; }
    snprintf(s.left, sizeof(s.left), "%s %ldrpm", state, rpm);

    // Middle: drive mode and the resolution in force (the driver caps what was asked)
    const auto& cfg = Services::motion.stepper.config;
    const unsigned usteps = cfg.getEffectiveMicrosteps();
    if (usteps <= 1) {
        snprintf(s.mid, sizeof(s.mid), "%s 1", cfg.getDriveMode() ? "CM" : "VM");
    } else {
        snprintf(s.mid, sizeof(s.mid), "%s 1/%u", cfg.getDriveMode() ? "CM" : "VM", usteps);
    }
    s.midColor = UI::Theme::VIOLET;

    // Right: the wheel this board drives, or OK when it has no role yet
    const char* role = Services::roleToString(Services::g_deviceConfig.getRole());
    bool hasRole = role != nullptr && role[0] != '\0' && strcmp(role, "NONE") != 0 && strcmp(role, "UNASSIGNED") != 0;
    snprintf(s.right, sizeof(s.right), "%s", hasRole ? role : "OK");
    s.rightColor = hasRole ? UI::Theme::YELLOW : UI::Theme::GREEN;

    m_screenManager.setStatus(s);
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

bool DisplayService::panelRegister(uint8_t reg, const uint8_t* data, uint8_t len)
{
    return m_lcd != nullptr && m_lcd->writePanelRegister(reg, data, len);
}

void DisplayService::estopHold(uint8_t pct)
{
    // Comms task: two stores; the Display task reads them on its next tick (a torn pair only
    // shows one tick of an old value)
    m_hostHoldMs = m_clock->getTickMs();
    m_hostHoldPct = pct;
}

void DisplayService::hostHoldTick()
{
    // The host's clear hold fills the E-STOP page's bar while it is fresh and CENTER is not held
    // here (the board's own hold draws it then)
    if (m_screenManager.current() != &m_estopScreen || m_clearing) {
        m_hostHoldShown = 0;
        return;
    }
    const uint32_t age = m_clock->getTickMs() - m_hostHoldMs;
    const uint8_t pct = (age <= HOST_HOLD_STALE_MS) ? m_hostHoldPct : 0;
    if (pct != m_hostHoldShown) {
        m_estopScreen.setProgress(pct);
        m_hostHoldShown = pct;
    }
}

void DisplayService::indicator(uint16_t angle, int8_t rotation, bool translation, uint8_t speedPct, uint8_t rotPct)
{
    if (m_indicator != nullptr) m_indicator->draw({angle, rotation, translation, speedPct, rotPct});
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
