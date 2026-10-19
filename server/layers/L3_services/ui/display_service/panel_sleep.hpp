/**
 * @file panel_sleep.hpp
 * @brief When the LCD panel sleeps and wakes.
 *
 * The saver keeps the IPS glass from holding a still picture, but it still lights every
 * pixel. After the saver has run for the sleep time, the panel goes black and into the
 * ST7789's sleep (display off, no refresh). The backlight on the X-NUCLEO-GFX01M2 is wired
 * to 3V3 and cannot be dimmed by firmware.
 *
 * The panel sleeps only while the saver is the screen in charge (LOCAL mode, saver on top).
 * The moment anything else is: a joystick move (the saver pops), an E-STOP page, the host
 * taking the screen (REMOTE: the indicator), it wakes, every time, as often as it slept.
 * The controller needs 120 ms between SLPIN and SLPOUT, so a wake within MIN_SLEEP_MS of
 * sleeping waits that long (the panel stays dark meanwhile).
 */

#ifndef PANEL_SLEEP_HPP
#define PANEL_SLEEP_HPP

#include <stdint.h>

namespace UI {

class PanelSleep {
public:
    enum class Action : uint8_t { NONE, SLEEP, WAKE };
    static constexpr uint32_t MIN_SLEEP_MS = 130;

    /// Once a tick. `saverInCharge`: LOCAL mode and the saver is the top screen.
    /// `sleepAfterMs`: how long the saver runs before the panel sleeps (0 = never).
    Action tick(bool saverInCharge, uint32_t nowMs, uint32_t sleepAfterMs)
    {
        if (!saverInCharge) {
            m_saverTiming = false;
            if (m_asleep && (nowMs - m_sleptMs) >= MIN_SLEEP_MS) {
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
            m_sleptMs = nowMs;
            return Action::SLEEP;
        }
        return Action::NONE;
    }

    /// Nothing may be drawn while true (the panel is off; the Display task skips rendering)
    bool asleep() const { return m_asleep; }

private:
    bool m_asleep = false;
    bool m_saverTiming = false;
    uint32_t m_saverSinceMs = 0;
    uint32_t m_sleptMs = 0;
};

} // namespace UI

#endif // PANEL_SLEEP_HPP
