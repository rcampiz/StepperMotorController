/**
 * @file screen.hpp
 * @brief Base screen interface for local UI rendering
 *
 * Defines the IScreen interface that all screen types implement.
 * Screens handle their own rendering and input processing.
 */

#ifndef SCREEN_HPP
#define SCREEN_HPP

#include "harness/pins/icanvas.hpp"
#include "harness/pins/ui_types.hpp"
#include "ui/ui_types.hpp"
#include <stdint.h>

namespace UI {

/**
 * @brief Input action result from screen
 */
enum class InputResult : uint8_t {
    HANDLED,      ///< Input was consumed by the screen
    UNHANDLED,    ///< Input should be handled by parent
    EXIT_SCREEN,  ///< Screen requests to close
    SWITCH_SCREEN ///< Screen requests switching to another screen
};

/**
 * @brief Base interface for all screen types
 *
 * Screens manage their own state, rendering, and input handling.
 * The display task calls render() periodically and handleInput()
 * when joystick events occur.
 */
class IScreen {
public:
    virtual ~IScreen() = default;

    /**
     * @brief Get the screen type
     * @return Screen type identifier
     */
    virtual ScreenType getType() const = 0;

    /**
     * @brief Render the screen to the LCD
     * @param lcd LCD driver instance
     *
     * Called periodically by display task. Screen should render
     * its current state. May be called frequently, so should be
     * efficient and avoid unnecessary redraws.
     */
    virtual void render(Harness::ICanvas& lcd) = 0;

    /**
     * @brief Handle joystick input
     * @param dir Joystick direction
     * @param pressed true if pressed, false if released
     * @return Result indicating how input was handled
     */
    virtual InputResult handleInput(JoyDirection dir, bool pressed) = 0;

    /**
     * @brief Called when screen becomes active
     *
     * Use for initialization that should happen each time
     * the screen is shown (not just on construction).
     */
    virtual void onActivate() {}

    /**
     * @brief Called when screen is about to be deactivated
     *
     * Use for cleanup before switching to another screen.
     */
    virtual void onDeactivate() {}

    /**
     * @brief Check if screen needs a full redraw
     * @return true if screen content has changed significantly
     */
    virtual bool needsFullRedraw() const { return false; }

    /**
     * @brief Clear the needs-redraw flag after rendering
     */
    virtual void clearRedrawFlag() {}
};

} // namespace UI

#endif // SCREEN_HPP
