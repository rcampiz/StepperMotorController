/**
 * @file ui_types.hpp
 * @brief L3-specific UI types: screen classification
 *
 * The UIMode enum, UIModeManager, and joystick types live in Foundation
 * (F_platform/ui/ui_mode.hpp) since they're needed by L2 and Foundation tasks.
 */

#ifndef UI_TYPES_HPP
#define UI_TYPES_HPP

#include <stdint.h>

namespace UI {

/**
 * @brief Screen types for local mode
 */
enum class ScreenType : uint8_t {
    STATUS,    ///< Telemetry display (existing pages)
    MENU,      ///< List-based navigation
    TERMINAL,  ///< Scrolling text console
    IMAGE,     ///< Full-screen bitmap
    CANVAS     ///< Remote-driven arbitrary rendering
};

} // namespace UI

#endif // UI_TYPES_HPP
