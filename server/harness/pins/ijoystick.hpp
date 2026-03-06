/**
 * @file ijoystick.hpp
 * @brief Joystick input interface — boundary contract
 *
 * Abstract joystick for Scheduler tasks. Implemented by Drivers::Joystick.
 * Tasks read direction without seeing GPIO details.
 */

#pragma once

#include "harness/pins/ui_types.hpp"

namespace Harness {

class IJoystick {
public:
    virtual ~IJoystick() = default;

    /**
     * @brief Read the current joystick direction
     * @return Direction (NONE if no button pressed)
     */
    virtual UI::JoyDirection readDirection() = 0;

    /**
     * @brief Get human-readable name for a direction
     */
    virtual const char* directionName(UI::JoyDirection d) = 0;
};

} // namespace Harness
