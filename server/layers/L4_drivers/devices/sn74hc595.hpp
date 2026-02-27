/**
 * @file sn74hc595.hpp
 * @brief SN74HC595N 8-bit shift register driver for STM32F401RE
 *
 * Driver for controlling the SN74HC595N shift register.
 * Pins must be defined in board_config.hpp
 */

#ifndef SN74HC595_HPP
#define SN74HC595_HPP

#include "L5_board/gpio.hpp"

/**
 * @brief SN74HC595N 8-bit shift register driver
 *
 * Provides methods to shift data into the register and control outputs.
 * GPIO pins must be passed to constructor - no default pin mapping.
 */
class SN74HC595 {
private:
    GPIO* ser;      // Serial data input (SER/DS)
    GPIO* nOE;      // Output enable (active low)
    GPIO* rclk;     // Storage register clock (latch)
    GPIO* srclk;    // Shift register clock
    GPIO* nSRCLR;   // Shift register clear (active low)

    bool ownsGPIOs; // Track if this object owns the GPIO instances

    /**
     * @brief Generate a clock pulse on the shift clock
     */
    void pulseShiftClock() {
        srclk->write(true);
        // Small delay for clock pulse (can be removed if MCU is slow enough)
        for (volatile int i = 0; i < 10; i++);
        srclk->write(false);
    }

    /**
     * @brief Generate a clock pulse on the latch clock
     */
    void pulseLatchClock() {
        rclk->write(true);
        // Small delay for clock pulse
        for (volatile int i = 0; i < 10; i++);
        rclk->write(false);
    }

public:
    /**
     * @brief Construct with port/pin parameters
     *
     * Creates and owns GPIO objects internally.
     * Pin assignments should come from board_config.hpp
     *
     * @param ser_port GPIO port for SER pin
     * @param ser_pin Pin number for SER
     * @param noe_port GPIO port for nOE pin
     * @param noe_pin Pin number for nOE
     * @param rclk_port GPIO port for RCLK pin
     * @param rclk_pin Pin number for RCLK
     * @param srclk_port GPIO port for SRCLK pin
     * @param srclk_pin Pin number for SRCLK
     * @param nsrclr_port GPIO port for nSRCLR pin
     * @param nsrclr_pin Pin number for nSRCLR
     */
    SN74HC595(GPIO_TypeDef* ser_port, uint8_t ser_pin,
              GPIO_TypeDef* noe_port, uint8_t noe_pin,
              GPIO_TypeDef* rclk_port, uint8_t rclk_pin,
              GPIO_TypeDef* srclk_port, uint8_t srclk_pin,
              GPIO_TypeDef* nsrclr_port, uint8_t nsrclr_pin)
        : ser(new GPIO(ser_port, ser_pin)),
          nOE(new GPIO(noe_port, noe_pin)),
          rclk(new GPIO(rclk_port, rclk_pin)),
          srclk(new GPIO(srclk_port, srclk_pin)),
          nSRCLR(new GPIO(nsrclr_port, nsrclr_pin)),
          ownsGPIOs(true) {
        init();
    }

    /**
     * @brief Construct with pre-existing GPIO pointers
     *
     * Does NOT own the GPIO objects - caller is responsible for cleanup.
     *
     * @param ser_gpio Pointer to GPIO for SER pin
     * @param noe_gpio Pointer to GPIO for nOE pin
     * @param rclk_gpio Pointer to GPIO for RCLK pin
     * @param srclk_gpio Pointer to GPIO for SRCLK pin
     * @param nsrclr_gpio Pointer to GPIO for nSRCLR pin
     */
    SN74HC595(GPIO* ser_gpio, GPIO* noe_gpio, GPIO* rclk_gpio,
              GPIO* srclk_gpio, GPIO* nsrclr_gpio)
        : ser(ser_gpio),
          nOE(noe_gpio),
          rclk(rclk_gpio),
          srclk(srclk_gpio),
          nSRCLR(nsrclr_gpio),
          ownsGPIOs(false) {
        init();
    }

    /**
     * @brief Destructor - clean up owned GPIO instances
     */
    ~SN74HC595() {
        if (ownsGPIOs) {
            delete ser;
            delete nOE;
            delete rclk;
            delete srclk;
            delete nSRCLR;
        }
    }

    /**
     * @brief Initialize the shift register pins and clear the register
     */
    void init() {
        // Configure all pins as outputs with pull-down resistors
        ser->setMode(GPIOMode::Output);
        ser->setOutputType(GPIOOutputType::PushPull);
        ser->setSpeed(GPIOSpeed::High);
        ser->setPull(GPIOPull::PullDown);

        nOE->setMode(GPIOMode::Output);
        nOE->setOutputType(GPIOOutputType::PushPull);
        nOE->setSpeed(GPIOSpeed::High);
        nOE->setPull(GPIOPull::PullDown);

        rclk->setMode(GPIOMode::Output);
        rclk->setOutputType(GPIOOutputType::PushPull);
        rclk->setSpeed(GPIOSpeed::High);
        rclk->setPull(GPIOPull::PullDown);

        srclk->setMode(GPIOMode::Output);
        srclk->setOutputType(GPIOOutputType::PushPull);
        srclk->setSpeed(GPIOSpeed::High);
        srclk->setPull(GPIOPull::PullDown);

        nSRCLR->setMode(GPIOMode::Output);
        nSRCLR->setOutputType(GPIOOutputType::PushPull);
        nSRCLR->setSpeed(GPIOSpeed::High);
        nSRCLR->setPull(GPIOPull::PullDown);

        // Set initial states
        ser->write(false);
        srclk->write(false);
        rclk->write(false);
        nSRCLR->write(true);  // Not clearing (active low)
        nOE->write(false);    // Outputs enabled (active low)

        // Clear the shift register
        clear();
    }

    /**
     * @brief Clear the shift register (all outputs LOW)
     */
    void clear() {
        nSRCLR->write(false);  // Assert clear (active low)
        for (volatile int i = 0; i < 10; i++);
        nSRCLR->write(true);   // Release clear
        pulseLatchClock();    // Latch the cleared state
    }

    /**
     * @brief Enable outputs
     */
    void enableOutput() {
        nOE->write(false);  // Active low
    }

    /**
     * @brief Disable outputs (high impedance)
     */
    void disableOutput() {
        nOE->write(true);  // Active low
    }

    /**
     * @brief Shift out a single byte (MSB first)
     *
     * @param data 8-bit data to shift out
     * @param latch If true, latch the data to outputs after shifting
     */
    void shiftOut(uint8_t data, bool latch = true) {
        // Shift out MSB first
        for (int i = 7; i >= 0; i--) {
            // Set data bit
            ser->write((data >> i) & 0x01);

            // Pulse shift clock to load bit
            pulseShiftClock();
        }

        // Latch data to output register if requested
        if (latch) {
            pulseLatchClock();
        }
    }

    /**
     * @brief Shift out multiple bytes (MSB first)
     *
     * @param data Pointer to data array
     * @param length Number of bytes to shift
     * @param latch If true, latch the data to outputs after shifting
     */
    void shiftOutMultiple(const uint8_t* data, uint8_t length, bool latch = true) {
        for (uint8_t i = 0; i < length; i++) {
            shiftOut(data[i], false);  // Don't latch yet
        }

        if (latch) {
            pulseLatchClock();
        }
    }

    /**
     * @brief Write a pattern and latch it to outputs
     *
     * @param pattern 8-bit pattern to display
     */
    void writePattern(uint8_t pattern) {
        shiftOut(pattern, true);
    }
};

#endif // SN74HC595_HPP