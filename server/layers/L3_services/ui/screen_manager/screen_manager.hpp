/**
 * @file screen_manager.hpp
 * @brief Stack-based screen navigation engine
 *
 * Manages a stack of IScreen pointers. The root screen (main menu)
 * is always at the bottom. push() overlays a new screen; pop()
 * returns to the previous one; popToRoot() returns to root.
 *
 * No RTOS headers. No driver dependencies.
 */

#ifndef SCREEN_MANAGER_HPP
#define SCREEN_MANAGER_HPP

#include "ui/screen.hpp"
#include "ui/chrome/chrome.hpp"
#include "F_platform/boundary/iclock.hpp"
#include <stdint.h>

namespace UI {

constexpr uint8_t SCREEN_STACK_MAX = 8;

class ScreenManager {
public:
    /**
     * @brief Initialize with root screen and LCD
     * @param rootScreen The main menu (always at stack bottom)
     * @param lcd LCD driver for rendering
     */
    void init(IScreen* rootScreen, Services::ICanvas* lcd);

    /**
     * @brief Push a screen onto the stack and activate it
     * @param screen Screen to push (must not be nullptr)
     */
    void push(IScreen* screen);

    /**
     * @brief Pop the current screen and re-activate the one below
     *
     * Does nothing if only the root screen remains.
     */
    void pop();

    /**
     * @brief Pop all screens back to root (main menu)
     */
    void popToRoot();

    /**
     * @brief Get the current (topmost) screen
     * @return Current screen, or nullptr if not initialized
     */
    IScreen* current() const;

    /**
     * @brief Get stack depth
     * @return Number of screens on the stack
     */
    uint8_t depth() const { return m_depth; }

    /**
     * @brief Render the current screen
     *
     * Clears the display if the screen requests a full redraw,
     * then calls render() on the current screen.
     */
    void render();

    /// The clock that paces animations (the selected row). Without one, each render
    /// counts as 50 ms (one 20 Hz frame).
    void setClock(Platform::IClock* clock) { m_clock = clock; }

    /**
     * @brief Route input to the current screen
     *
     * If the screen returns EXIT_SCREEN, pops it automatically.
     *
     * @param dir Joystick direction
     * @param pressed true if pressed, false if released
     */
    void handleInput(JoyDirection dir, bool pressed);

    /// The status line chrome screens show; redrawn on the next render when it changed
    void setStatus(const StatusLine& s);

private:
    StatusLine m_status = {};
    bool m_statusDirty = true;
    const char* m_drawnHint = nullptr;   ///< the footer's CENTER hint as last drawn
    IScreen* m_stack[SCREEN_STACK_MAX] = {};
    uint8_t m_depth = 0;
    Services::ICanvas* m_lcd = nullptr;
    bool m_needsClear = false;  ///< Next render should clear screen first
    Platform::IClock* m_clock = nullptr;
    uint32_t m_frameMs = 0;     ///< the stand-in clock when there is none
    uint32_t m_renderMs = 0;    ///< the stand-in clock for IScreen::setNowMs (50 ms a render)
};

} // namespace UI

#endif // SCREEN_MANAGER_HPP
