/**
 * @file sn74hc595.hpp
 * @brief SN74HC595N 8-bit shift register driver
 *
 * Driver for controlling the SN74HC595N shift register.
 * GPIO pins are injected via constructor — no CMSIS dependency.
 */

#ifndef SN74HC595_HPP
#define SN74HC595_HPP

#include "harness/pins/igpio.hpp"

namespace Drivers {

/**
 * @brief SN74HC595N 8-bit shift register driver
 *
 * Provides methods to shift data into the register and control outputs.
 * GPIO pins must be passed to constructor - no default pin mapping.
 */
class SN74HC595 {
private:
    Harness::IGPIO& ser;      // Serial data input (SER/DS)
    Harness::IGPIO& nOE;      // Output enable (active low)
    Harness::IGPIO& rclk;     // Storage register clock (latch)
    Harness::IGPIO& srclk;    // Shift register clock
    Harness::IGPIO& nSRCLR;   // Shift register clear (active low)

    /**
     * @brief Generate a clock pulse on the shift clock
     */
    void pulseShiftClock() {
        srclk.write(true);
        // Small delay for clock pulse (can be removed if MCU is slow enough)
        for (volatile int i = 0; i < 10; i++);
        srclk.write(false);
    }

    /**
     * @brief Generate a clock pulse on the latch clock
     */
    void pulseLatchClock() {
        rclk.write(true);
        // Small delay for clock pulse
        for (volatile int i = 0; i < 10; i++);
        rclk.write(false);
    }

public:
    /**
     * @brief Construct with pre-configured GPIO references
     *
     * Caller is responsible for pin mode configuration before construction.
     *
     * @param ser_gpio   GPIO for SER pin (output)
     * @param noe_gpio   GPIO for nOE pin (output)
     * @param rclk_gpio  GPIO for RCLK pin (output)
     * @param srclk_gpio GPIO for SRCLK pin (output)
     * @param nsrclr_gpio GPIO for nSRCLR pin (output)
     */
    SN74HC595(Harness::IGPIO& ser_gpio, Harness::IGPIO& noe_gpio, Harness::IGPIO& rclk_gpio,
              Harness::IGPIO& srclk_gpio, Harness::IGPIO& nsrclr_gpio)
        : ser(ser_gpio),
          nOE(noe_gpio),
          rclk(rclk_gpio),
          srclk(srclk_gpio),
          nSRCLR(nsrclr_gpio) {
        init();
    }

    /**
     * @brief Initialize the shift register pins and clear the register
     */
    void init() {
        // Set initial states
        ser.write(false);
        srclk.write(false);
        rclk.write(false);
        nSRCLR.write(true);  // Not clearing (active low)
        nOE.write(false);    // Outputs enabled (active low)

        // Clear the shift register
        clear();
    }

    /**
     * @brief Clear the shift register (all outputs LOW)
     */
    void clear() {
        nSRCLR.write(false);  // Assert clear (active low)
        for (volatile int i = 0; i < 10; i++);
        nSRCLR.write(true);   // Release clear
        pulseLatchClock();    // Latch the cleared state
    }

    /**
     * @brief Enable outputs
     */
    void enableOutput() {
        nOE.write(false);  // Active low
    }

    /**
     * @brief Disable outputs (high impedance)
     */
    void disableOutput() {
        nOE.write(true);  // Active low
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
            ser.write((data >> i) & 0x01);

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

} // namespace Drivers

#endif // SN74HC595_HPP
