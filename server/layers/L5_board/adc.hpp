/**
 * @file adc.hpp
 * @brief ADC (Analog-to-Digital Converter) driver for STM32F401RE
 *
 * Provides a simple interface for reading analog voltages from ADC channels.
 * The STM32F401RE has a 12-bit ADC with up to 16 channels.
 */

#ifndef ADC_HPP
#define ADC_HPP

#include "stm32f401xe.h"
#include "L5_board/gpio.hpp"

/**
 * @brief ADC sample time options
 *
 * Longer sample times provide more accurate readings but take more time.
 * For high impedance sources, use longer sample times.
 */
enum class ADCSampleTime : uint32_t {
    Cycles3   = 0x00,  // 3 cycles
    Cycles15  = 0x01,  // 15 cycles
    Cycles28  = 0x02,  // 28 cycles
    Cycles56  = 0x03,  // 56 cycles
    Cycles84  = 0x04,  // 84 cycles
    Cycles112 = 0x05,  // 112 cycles
    Cycles144 = 0x06,  // 144 cycles
    Cycles480 = 0x07   // 480 cycles (most accurate)
};

/**
 * @brief ADC resolution options
 */
enum class ADCResolution : uint32_t {
    Bits12 = 0x00,  // 12-bit resolution (0-4095)
    Bits10 = 0x01,  // 10-bit resolution (0-1023)
    Bits8  = 0x02,  // 8-bit resolution (0-255)
    Bits6  = 0x03   // 6-bit resolution (0-63)
};

/**
 * @brief ADC prescaler options (APB2 clock divider)
 */
enum class ADCPrescaler : uint32_t {
    Div2 = 0x00,  // PCLK2 divided by 2
    Div4 = 0x01,  // PCLK2 divided by 4
    Div6 = 0x02,  // PCLK2 divided by 6
    Div8 = 0x03   // PCLK2 divided by 8
};

/**
 * @brief ADC driver class
 *
 * Provides methods to initialize and read from ADC channels.
 * Supports single-shot and continuous conversion modes.
 */
class ADC {
private:
    ADC_TypeDef* adc;
    bool initialized;

    /**
     * @brief Wait for conversion to complete
     * @param timeout Maximum number of iterations to wait
     * @return true if conversion completed, false if timeout
     */
    bool waitForConversion(uint32_t timeout = 100000) {
        uint32_t count = 0;
        while (!(adc->SR & ADC_SR_EOC)) {
            if (++count > timeout) {
                return false;
            }
        }
        return true;
    }

public:
    /**
     * @brief Construct a new ADC object
     * @param adc_instance Pointer to ADC peripheral (ADC1, ADC2, etc.)
     */
    ADC(ADC_TypeDef* adc_instance = ADC1)
        : adc(adc_instance), initialized(false) {
    }

    /**
     * @brief Initialize the ADC peripheral
     * @param prescaler ADC clock prescaler
     * @param sampleTime Default sample time for all channels
     */
    void init(ADCPrescaler prescaler = ADCPrescaler::Div4,
              ADCSampleTime sampleTime = ADCSampleTime::Cycles84) {
        // Enable ADC clock
        RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

        // Set ADC prescaler in common control register
        uint32_t ccr = ADC_COMMON->CCR;
        ccr &= ~ADC_CCR_ADCPRE_Msk;
        ccr |= (static_cast<uint32_t>(prescaler) << ADC_CCR_ADCPRE_Pos);
        ADC_COMMON->CCR = ccr;

        // Set default sample time for all channels (using SMPR2 for channels 0-9)
        uint32_t sample = static_cast<uint32_t>(sampleTime);
        adc->SMPR2 = (sample << 0)  | (sample << 3)  | (sample << 6)  |
                     (sample << 9)  | (sample << 12) | (sample << 15) |
                     (sample << 18) | (sample << 21) | (sample << 24) |
                     (sample << 27);

        // Set default sample time for channels 10-18 (using SMPR1)
        adc->SMPR1 = (sample << 0)  | (sample << 3)  | (sample << 6)  |
                     (sample << 9)  | (sample << 12) | (sample << 15) |
                     (sample << 18) | (sample << 21) | (sample << 24);

        // Set 12-bit resolution (default)
        adc->CR1 &= ~(0x03UL << 24);  // Clear RES bits

        // Disable continuous mode (single conversion)
        adc->CR2 &= ~ADC_CR2_CONT;

        // Enable ADC
        adc->CR2 |= ADC_CR2_ADON;

        // Small delay after enabling ADC
        for (volatile int i = 0; i < 1000; i++);

        initialized = true;
    }

    /**
     * @brief Configure a GPIO pin for analog input
     * @param port GPIO port (GPIOA, GPIOB, etc.)
     * @param pin Pin number (0-15)
     */
    void configurePin(GPIO_TypeDef* port, uint8_t pin) {
        GPIO analogPin(port, pin);
        analogPin.setMode(GPIOMode::Analog);
        analogPin.setPull(GPIOPull::None);
    }

    /**
     * @brief Read a single ADC channel
     * @param channel ADC channel number (0-18)
     * @return 12-bit ADC value (0-4095)
     */
    uint16_t read(uint8_t channel) {
        if (!initialized) {
            return 0;
        }

        // Set the channel to convert
        adc->SQR3 = (channel & 0x1F);

        // Set sequence length to 1 conversion
        adc->SQR1 = 0;

        // Start conversion
        adc->CR2 |= ADC_CR2_SWSTART;

        // Wait for conversion to complete
        if (!waitForConversion()) {
            return 0;  // Timeout
        }

        // Read and return the result
        return static_cast<uint16_t>(adc->DR & 0x0FFF);
    }

    /**
     * @brief Read ADC channel and convert to voltage
     * @param channel ADC channel number (0-18)
     * @param vref Reference voltage in millivolts (default 3300mV for 3.3V)
     * @return Voltage in millivolts
     */
    uint32_t readVoltage(uint8_t channel, uint32_t vref = 3300) {
        uint16_t raw = read(channel);
        return (static_cast<uint32_t>(raw) * vref) / 4095;
    }

    /**
     * @brief Enable continuous conversion mode
     */
    void enableContinuousMode() {
        adc->CR2 |= ADC_CR2_CONT;
    }

    /**
     * @brief Disable continuous conversion mode (single-shot)
     */
    void disableContinuousMode() {
        adc->CR2 &= ~ADC_CR2_CONT;
    }

    /**
     * @brief Check if ADC is initialized
     * @return true if initialized
     */
    bool isInitialized() const {
        return initialized;
    }

    /**
     * @brief Get the raw ADC value from the last conversion
     * @return Last conversion result (useful in continuous mode)
     */
    uint16_t getLastValue() const {
        return static_cast<uint16_t>(adc->DR & 0x0FFF);
    }
};

/**
 * @brief Helper function to map ADC channel to GPIO pin
 *
 * ADC1 Channel mapping for STM32F401RE:
 *   Channel 0  -> PA0
 *   Channel 1  -> PA1
 *   Channel 2  -> PA2
 *   Channel 3  -> PA3
 *   Channel 4  -> PA4
 *   Channel 5  -> PA5
 *   Channel 6  -> PA6
 *   Channel 7  -> PA7
 *   Channel 8  -> PB0
 *   Channel 9  -> PB1
 *   Channel 10 -> PC0
 *   Channel 11 -> PC1
 *   Channel 12 -> PC2
 *   Channel 13 -> PC3
 *   Channel 14 -> PC4
 *   Channel 15 -> PC5
 */

#endif // ADC_HPP