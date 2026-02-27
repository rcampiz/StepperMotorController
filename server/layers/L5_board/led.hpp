/**
 * @file led.hpp
 * @brief LED class for STM32F401RE
 *
 * Provides an LED control class that inherits from GPIO,
 * with simplified on/off/blink methods.
 */

#ifndef LED_HPP
#define LED_HPP

#include "L5_board/gpio.hpp"

/**
 * @brief LED class for controlling LEDs
 *
 * Inherits from GPIO and provides LED-specific functionality
 * such as on(), off(), and blink() methods.
 */
class LED : public GPIO {
public:
    /**
     * @brief Construct a new LED object
     * @param gpio_port Pointer to GPIO port (GPIOA, GPIOB, etc.)
     * @param gpio_pin Pin number (0-15)
     * @param speed Output speed (default: Low)
     */
    LED(GPIO_TypeDef* gpio_port, uint8_t gpio_pin,
        GPIOSpeed speed = GPIOSpeed::Low)
        : GPIO(gpio_port, gpio_pin) {
        init(speed);
    }

    /**
     * @brief Initialize the LED pin
     * @param speed Output speed configuration
     */
    void init(GPIOSpeed speed = GPIOSpeed::Low) {
        setMode(GPIOMode::Output);
        setOutputType(GPIOOutputType::PushPull);
        setSpeed(speed);
        setPull(GPIOPull::None);
        off();  // Start with LED off
    }

    /**
     * @brief Turn the LED on
     */
    void on() {
        write(true);
    }

    /**
     * @brief Turn the LED off
     */
    void off() {
        write(false);
    }

    /**
     * @brief Check if LED is currently on
     * @return true if LED is on, false otherwise
     */
    bool isOn() const {
        return read();
    }

    /**
     * @brief Blink the LED once
     * @param delay_func Pointer to delay function (in milliseconds)
     * @param on_time Time LED is on (ms)
     * @param off_time Time LED is off (ms)
     */
    void blink(void (*delay_func)(uint32_t), uint32_t on_time, uint32_t off_time) {
        on();
        delay_func(on_time);
        off();
        delay_func(off_time);
    }
};

#endif // LED_HPP