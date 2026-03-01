/**
 * @file ui_mode.hpp
 * @brief Compatibility shim — includes Foundation ui_mode + L3 ui_types
 *
 * UIMode, UIModeManager, joystick types moved to F_platform/ui/ui_mode.hpp.
 * ScreenType stays in ui_types.hpp.
 * This header re-exports both so existing L3 consumers compile unchanged.
 */

#ifndef UI_MODE_HPP
#define UI_MODE_HPP

#include "F_platform/ui/ui_mode.hpp"
#include "ui/ui_types.hpp"

#endif // UI_MODE_HPP
