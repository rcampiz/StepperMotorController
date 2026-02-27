/**
 * @file gpio.hpp
 * @brief Base GPIO class for STM32F401RE
 *
 * Provides a base class for GPIO pin control with common functionality
 * for initialization, mode configuration, and basic I/O operations.
 */

#ifndef GPIO_HPP
#define GPIO_HPP

#include "X_vendor/CMSIS/stm32f401xe.h"

/**
 * @brief GPIO pin modes
 */
enum class GPIOMode : uint32_t {
    Input  = 0x00,  // Input mode
    Output = 0x01,  // Output mode
    AltFunc = 0x02, // Alternate function mode
    Analog = 0x03   // Analog mode
};

/**
 * @brief GPIO output types
 */
enum class GPIOOutputType : uint32_t {
    PushPull  = 0x00,  // Push-pull output
    OpenDrain = 0x01   // Open-drain output
};

/**
 * @brief GPIO output speed
 */
enum class GPIOSpeed : uint32_t {
    Low       = 0x00,  // Low speed
    Medium    = 0x01,  // Medium speed
    High      = 0x02,  // High speed
    VeryHigh  = 0x03   // Very high speed
};

/**
 * @brief GPIO pull-up/pull-down configuration
 */
enum class GPIOPull : uint32_t {
    None     = 0x00,  // No pull-up or pull-down
    PullUp   = 0x01,  // Pull-up enabled
    PullDown = 0x02   // Pull-down enabled
};

/**
 * @brief Base GPIO class for pin control
 *
 * Provides common GPIO functionality including initialization,
 * reading, writing, and toggling pins.
 */
class GPIO {
protected:
    GPIO_TypeDef* port;
    uint8_t pin;

public:
    /**
     * @brief Enable the clock for the GPIO port
     */
    void enableClock() {
        if (port == GPIOA) {
            RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
        } else if (port == GPIOB) {
            RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
        } else if (port == GPIOC) {
            RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
        } else if (port == GPIOD) {
            RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
        } else if (port == GPIOE) {
            RCC->AHB1ENR |= RCC_AHB1ENR_GPIOEEN;
        } else if (port == GPIOH) {
            RCC->AHB1ENR |= RCC_AHB1ENR_GPIOHEN;
        }

        // Small delay after enabling clock to ensure it's stable
        for (volatile int i = 0; i < 100; i++);
    }

    /**
     * @brief Set the GPIO pin mode
     * @param mode The desired pin mode
     */
    void setMode(GPIOMode mode) {
        uint32_t moder = port->MODER;
        moder &= ~(0x3UL << (pin * 2));
        moder |= (static_cast<uint32_t>(mode) << (pin * 2));
        port->MODER = moder;
    }

    /**
     * @brief Set the GPIO output type
     * @param type The desired output type
     */
    void setOutputType(GPIOOutputType type) {
        if (type == GPIOOutputType::OpenDrain) {
            port->OTYPER |= (1UL << pin);
        } else {
            port->OTYPER &= ~(1UL << pin);
        }
    }

    /**
     * @brief Set the GPIO output speed
     * @param speed The desired output speed
     */
    void setSpeed(GPIOSpeed speed) {
        uint32_t ospeedr = port->OSPEEDR;
        ospeedr &= ~(0x3UL << (pin * 2));
        ospeedr |= (static_cast<uint32_t>(speed) << (pin * 2));
        port->OSPEEDR = ospeedr;
    }

    /**
     * @brief Set the GPIO pull-up/pull-down configuration
     * @param pull The desired pull configuration
     */
    void setPull(GPIOPull pull) {
        uint32_t pupdr = port->PUPDR;
        pupdr &= ~(0x3UL << (pin * 2));
        pupdr |= (static_cast<uint32_t>(pull) << (pin * 2));
        port->PUPDR = pupdr;
    }

    /**
     * @brief Set the alternate function for the pin
     * @param af Alternate function number (0-15)
     */
    void setAlternateFunction(uint8_t af) {
        // Pins 0-7 use AFRL, pins 8-15 use AFRH
        if (pin < 8) {
            uint32_t afrl = port->AFR[0];
            afrl &= ~(0xFUL << (pin * 4));
            afrl |= ((af & 0x0F) << (pin * 4));
            port->AFR[0] = afrl;
        } else {
            uint32_t afrh = port->AFR[1];
            afrh &= ~(0xFUL << ((pin - 8) * 4));
            afrh |= ((af & 0x0F) << ((pin - 8) * 4));
            port->AFR[1] = afrh;
        }
    }

    /**
     * @brief Construct a new GPIO object
     * @param gpio_port Pointer to GPIO port (GPIOA, GPIOB, etc.)
     * @param gpio_pin Pin number (0-15)
     */
    GPIO(GPIO_TypeDef* gpio_port, uint8_t gpio_pin)
        : port(gpio_port), pin(gpio_pin) {
        enableClock();
    }

    /**
     * @brief Virtual destructor for proper inheritance
     */
    virtual ~GPIO() = default;

    /**
     * @brief Write a digital value to the pin
     * @param value true for HIGH, false for LOW
     */
    void write(bool value) {
        if (value) {
            port->BSRR = (1UL << pin);  // Set bit
        } else {
            port->BSRR = (1UL << (pin + 16));  // Reset bit
        }
    }

    /**
     * @brief Read the current state of the pin
     * @return true if pin is HIGH, false if LOW
     */
    bool read() const {
        return (port->IDR & (1UL << pin)) != 0;
    }

    /**
     * @brief Toggle the pin state
     */
    void toggle() {
        port->ODR ^= (1UL << pin);
    }

    /**
     * @brief Get the port pointer
     * @return Pointer to the GPIO port
     */
    GPIO_TypeDef* getPort() const {
        return port;
    }

    /**
     * @brief Get the pin number
     * @return Pin number (0-15)
     */
    uint8_t getPin() const {
        return pin;
    }
};

#endif // GPIO_HPP