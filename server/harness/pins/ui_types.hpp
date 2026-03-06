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

} // namespace UI
