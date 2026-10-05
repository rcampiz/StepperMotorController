/**
 * @file ui_types.hpp
 * @brief UI type definitions — boundary contract
 *
 * UIMode, JoyDirection, JoyEvent — shared between L2 (protocol),
 * L3 (screens), and Foundation (display task).
 */

#pragma once

#include <stdint.h>

namespace UI {

enum class UIMode : uint8_t {
    LOCAL,
    REMOTE
};

enum class JoyDirection : uint8_t {
    NONE = 0,
    LEFT,
    RIGHT,
    UP,
    DOWN,
    CENTER
};

struct JoyEvent {
    JoyDirection direction;
    bool pressed;
    uint32_t timestamp;
};

using JoyEventCallback = void(*)(const JoyEvent& event);

/// Wire name of a joystick direction (shared by L2 formatting and L3 UI).
inline const char* joyDirectionName(JoyDirection dir) {
    switch (dir) {
        case JoyDirection::NONE:   return "NONE";
        case JoyDirection::LEFT:   return "LEFT";
        case JoyDirection::RIGHT:  return "RIGHT";
        case JoyDirection::UP:     return "UP";
        case JoyDirection::DOWN:   return "DOWN";
        case JoyDirection::CENTER: return "CENTER";
        default:                   return "UNKNOWN";
    }
}

} // namespace UI
