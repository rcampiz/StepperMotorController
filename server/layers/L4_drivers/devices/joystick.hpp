/**
 * @file joystick.hpp
 * @brief 5-way joystick driver for X-NUCLEO-GFX01M2
 *
 * GPIO pins are injected via constructor — no CMSIS dependency.
 * All pins are active low with external pull-ups.
 */

#ifndef JOYSTICK_HPP
#define JOYSTICK_HPP

#include "harness/pins/igpio.hpp"
#include "harness/pins/ijoystick.hpp"

namespace Drivers {

class Joystick : public Harness::IJoystick {
public:
    enum class Direction : uint8_t {
        None   = 0,
        Left   = 1,
        Right  = 2,
        Up     = 3,
        Down   = 4,
        Center = 5
    };

    struct State {
        bool left   : 1;
        bool right  : 1;
        bool up     : 1;
        bool down   : 1;
        bool center : 1;

        bool any() const { return left || right || up || down || center; }
    };

    /**
     * @brief Construct with pre-configured GPIO references
     *
     * All pins must be configured as inputs with pull-up before construction.
     */
    Joystick(Harness::IGPIO& left, Harness::IGPIO& right, Harness::IGPIO& up, Harness::IGPIO& down, Harness::IGPIO& center)
        : m_left(left), m_right(right), m_up(up), m_down(down), m_center(center) {}

    State read() const {
        State s;
        s.left   = !m_left.read();
        s.right  = !m_right.read();
        s.up     = !m_up.read();
        s.down   = !m_down.read();
        s.center = !m_center.read();
        return s;
    }

    Direction readDirectionRaw() const {
        State s = read();
        if (s.center) return Direction::Center;
        if (s.left)   return Direction::Left;
        if (s.right)  return Direction::Right;
        if (s.up)     return Direction::Up;
        if (s.down)   return Direction::Down;
        return Direction::None;
    }

    bool left()   const { return !m_left.read(); }
    bool right()  const { return !m_right.read(); }
    bool up()     const { return !m_up.read(); }
    bool down()   const { return !m_down.read(); }
    bool center() const { return !m_center.read(); }

    const char* directionName(Direction d) const {
        switch (d) {
            case Direction::Left:   return "LEFT";
            case Direction::Right:  return "RIGHT";
            case Direction::Up:     return "UP";
            case Direction::Down:   return "DOWN";
            case Direction::Center: return "CENTER";
            default:                return "NONE";
        }
    }

    // --- IJoystick interface (returns UI::JoyDirection) ---

    UI::JoyDirection readDirection() override {
        Direction d = readDirectionRaw();
        switch (d) {
            case Direction::Left:   return UI::JoyDirection::LEFT;
            case Direction::Right:  return UI::JoyDirection::RIGHT;
            case Direction::Up:     return UI::JoyDirection::UP;
            case Direction::Down:   return UI::JoyDirection::DOWN;
            case Direction::Center: return UI::JoyDirection::CENTER;
            default:                return UI::JoyDirection::NONE;
        }
    }

    const char* directionName(UI::JoyDirection d) override {
        switch (d) {
            case UI::JoyDirection::LEFT:   return "LEFT";
            case UI::JoyDirection::RIGHT:  return "RIGHT";
            case UI::JoyDirection::UP:     return "UP";
            case UI::JoyDirection::DOWN:   return "DOWN";
            case UI::JoyDirection::CENTER: return "CENTER";
            default:                       return "NONE";
        }
    }

private:
    Harness::IGPIO& m_left;
    Harness::IGPIO& m_right;
    Harness::IGPIO& m_up;
    Harness::IGPIO& m_down;
    Harness::IGPIO& m_center;
};

} // namespace Drivers

#endif // JOYSTICK_HPP
