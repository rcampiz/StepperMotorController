/**
 * @file joystick.hpp
 * @brief 5-way joystick driver for X-NUCLEO-GFX01M2
 *
 * Pin assignments (directly active low with internal pull-ups):
 *   KEY_LEFT   - PC7  (CN10 area, D9)
 *   KEY_CENTER - PB6  (CN10 area, D10)
 *   KEY_DOWN   - PC9  (CN10-1)
 *   KEY_RIGHT  - PC10 (CN7-1)
 *   KEY_UP     - PC12 (CN7-3)
 */

#ifndef JOYSTICK_HPP
#define JOYSTICK_HPP

#include "stm32f401xe.h"
#include "board/board_pins.hpp"

class Joystick {
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

    Joystick() {
        initPins();
    }

    State read() const {
        State s;
        s.left   = !(Pins::Joystick::LEFT_PORT->IDR   & (1 << Pins::Joystick::LEFT_PIN));
        s.right  = !(Pins::Joystick::RIGHT_PORT->IDR  & (1 << Pins::Joystick::RIGHT_PIN));
        s.up     = !(Pins::Joystick::UP_PORT->IDR     & (1 << Pins::Joystick::UP_PIN));
        s.down   = !(Pins::Joystick::DOWN_PORT->IDR   & (1 << Pins::Joystick::DOWN_PIN));
        s.center = false;  // PB6 reassigned to encoder EA
        return s;
    }

    Direction readDirection() const {
        State s = read();
        if (s.center) return Direction::Center;
        if (s.left)   return Direction::Left;
        if (s.right)  return Direction::Right;
        if (s.up)     return Direction::Up;
        if (s.down)   return Direction::Down;
        return Direction::None;
    }

    bool left()   const { return !(Pins::Joystick::LEFT_PORT->IDR   & (1 << Pins::Joystick::LEFT_PIN)); }
    bool right()  const { return !(Pins::Joystick::RIGHT_PORT->IDR  & (1 << Pins::Joystick::RIGHT_PIN)); }
    bool up()     const { return !(Pins::Joystick::UP_PORT->IDR     & (1 << Pins::Joystick::UP_PIN)); }
    bool down()   const { return !(Pins::Joystick::DOWN_PORT->IDR   & (1 << Pins::Joystick::DOWN_PIN)); }
    bool center() const { return false; }  // PB6 reassigned to encoder EA

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

private:
    void initPins() {
        RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;

        configureInput(Pins::Joystick::LEFT_PORT,   Pins::Joystick::LEFT_PIN);
        configureInput(Pins::Joystick::RIGHT_PORT,  Pins::Joystick::RIGHT_PIN);
        configureInput(Pins::Joystick::UP_PORT,     Pins::Joystick::UP_PIN);
        configureInput(Pins::Joystick::DOWN_PORT,   Pins::Joystick::DOWN_PIN);
        // NOTE: CENTER (PB6) skipped — pin reassigned to encoder EA (TIM4_CH1)
    }

    void configureInput(GPIO_TypeDef* port, uint8_t pin) {
        port->MODER &= ~(0x3 << (pin * 2));   // Input mode
        port->PUPDR &= ~(0x3 << (pin * 2));
        port->PUPDR |= (0x1 << (pin * 2));    // Pull-up
    }
};

#endif // JOYSTICK_HPP
