/**
 * @file panel_sleep.hpp
 * @brief When the LCD panel sleeps and wakes.
 *
 * The saver keeps the IPS glass from holding a still picture, but it still lights every
 * pixel. After the saver has run for the sleep time, the panel is painted black once and
 * nothing more is sent. (The ST7789's DISPOFF/SLPIN would stop the refresh, but this glass
 * shows white with the controller off, so they are not used.) The backlight on the
 * X-NUCLEO-GFX01M2 is wired to 3V3 and cannot be dimmed by firmware.
 *
 * The panel sleeps only while the saver is the screen in charge (LOCAL mode, saver on top).
 * The moment anything else is: a joystick move (the saver pops), an E-STOP page, the host
 * taking the screen (REMOTE: the indicator), it wakes, every time, as often as it slept.
 */

#ifndef PANEL_SLEEP_HPP
#define PANEL_SLEEP_HPP

#include <stdint.h>

namespace UI {

class PanelSleep {
public:
    enum class Action : uint8_t { NONE, SLEEP, WAKE };

    /// Once a tick. `saverInCharge`: LOCAL mode and the saver is the top screen.
    /// `sleepAfterMs`: how long the saver runs before the panel sleeps (0 = never).
    Action tick(bool saverInCharge, uint32_t nowMs, uint32_t sleepAfterMs)
    {
        if (!saverInCharge) {
            m_saverTiming = false;
            if (m_asleep) {
                m_asleep = false;
                return Action::WAKE;
            }
            return Action::NONE;
        }
        if (!m_saverTiming) {
            m_saverTiming = true;
            m_saverSinceMs = nowMs;
        }
        if (!m_asleep && sleepAfterMs != 0 && (nowMs - m_saverSinceMs) >= sleepAfterMs) {
            m_asleep = true;
            return Action::SLEEP;
        }
        return Action::NONE;
    }

    /// Nothing may be drawn while true (the panel holds a black frame; the Display task skips rendering)
    bool asleep() const { return m_asleep; }

private:
    bool m_asleep = false;
    bool m_saverTiming = false;
    uint32_t m_saverSinceMs = 0;
};

} // namespace UI

#endif // PANEL_SLEEP_HPP
