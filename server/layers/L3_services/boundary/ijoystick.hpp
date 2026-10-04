/**
 * @file boundary/ijoystick.hpp
 * @brief Consumer port — Services side of the Services::IJoystick harness connection
 *
 * Structurally identical to Services::IJoystick.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

#include "harness/pins/ui_types.hpp"

namespace Services {

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

} // namespace Services
