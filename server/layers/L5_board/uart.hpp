/**
 * @file uart.hpp
 * @brief UART driver for STM32F401RE
 *
 * Provides a simple interface for UART communication.
 * USART2 is connected to the ST-LINK USB CDC converter on the NUCLEO board.
 *
 * Pin mapping for USART2:
 *   TX -> PA2 (Arduino D1, connected to ST-LINK VCP)
 *   RX -> PA3 (Arduino D0, connected to ST-LINK VCP)
 */

#ifndef UART_HPP
#define UART_HPP

#include "stm32f401xe.h"
#include "L5_board/gpio.hpp"
#include <stdint.h>

/**
 * @brief UART baud rate options
 */
enum class UARTBaudRate : uint32_t {
    Baud9600   = 9600,
    Baud19200  = 19200,
    Baud38400  = 38400,
    Baud57600  = 57600,
    Baud115200 = 115200,
    Baud230400 = 230400,
    Baud460800 = 460800,
    Baud921600 = 921600
};

/**
 * @brief UART word length options
 */
enum class UARTWordLength : uint32_t {
    Bits8 = 0x00,  // 8 data bits
    Bits9 = 0x01   // 9 data bits
};

/**
 * @brief UART parity options
 */
enum class UARTParity : uint32_t {
    None = 0x00,   // No parity
    Even = 0x02,   // Even parity
    Odd  = 0x03    // Odd parity
};

/**
 * @brief UART stop bits options
 */
enum class UARTStopBits : uint32_t {
    Bits1   = 0x00,  // 1 stop bit
    Bits0_5 = 0x01,  // 0.5 stop bits
    Bits2   = 0x02,  // 2 stop bits
    Bits1_5 = 0x03   // 1.5 stop bits
};

/**
 * @brief UART driver class
 *
 * Provides methods to initialize and communicate via UART.
 * Supports blocking transmit/receive operations.
 */
class UART {
private:
    USART_TypeDef* uart;
    uint32_t apbClock;  // APB clock frequency in Hz
    bool initialized;

    /**
     * @brief Calculate baud rate register value
     * @param baudRate Desired baud rate
     * @return BRR register value
     */
    uint32_t calculateBRR(uint32_t baudRate) {
        // BRR = (APB_CLK / baudRate)
        // For USART2, APB1 clock is used (typically 84MHz / 2 = 42MHz)
        return (apbClock + (baudRate / 2)) / baudRate;
    }

public:
    /**
     * @brief Construct a new UART object
     * @param uart_instance Pointer to USART peripheral (USART1, USART2, etc.)
     * @param apb_clock APB clock frequency in Hz
     */
    UART(USART_TypeDef* uart_instance = USART2, uint32_t apb_clock = 42000000)
        : uart(uart_instance), apbClock(apb_clock), initialized(false) {
    }

    /**
     * @brief Initialize the UART peripheral
     * @param baudRate Baud rate
     * @param wordLength Word length (8 or 9 bits)
     * @param parity Parity mode
     * @param stopBits Number of stop bits
     */
    void init(UARTBaudRate baudRate = UARTBaudRate::Baud115200,
              UARTWordLength wordLength = UARTWordLength::Bits8,
              UARTParity parity = UARTParity::None,
              UARTStopBits stopBits = UARTStopBits::Bits1) {

        // Enable GPIO port A clock
        RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

        // Configure PA2 (TX) and PA3 (RX) as alternate function
        GPIO txPin(GPIOA, 2);
        GPIO rxPin(GPIOA, 3);

        txPin.setMode(GPIOMode::AltFunc);
        txPin.setAlternateFunction(7);  // AF7 = USART1/2/3
        txPin.setSpeed(GPIOSpeed::High);
        txPin.setPull(GPIOPull::PullUp);

        rxPin.setMode(GPIOMode::AltFunc);
        rxPin.setAlternateFunction(7);  // AF7 = USART1/2/3
        rxPin.setPull(GPIOPull::PullUp);

        // Enable USART2 clock (on APB1)
        if (uart == USART2) {
            RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
        } else if (uart == USART1) {
            RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
        } else if (uart == USART6) {
            RCC->APB2ENR |= RCC_APB2ENR_USART6EN;
        }

        // Disable UART during configuration
        uart->CR1 &= ~USART_CR1_UE;

        // Configure word length
        if (wordLength == UARTWordLength::Bits9) {
            uart->CR1 |= USART_CR1_M;
        } else {
            uart->CR1 &= ~USART_CR1_M;
        }

        // Configure parity
        if (parity == UARTParity::None) {
            uart->CR1 &= ~USART_CR1_PCE;
        } else {
            uart->CR1 |= USART_CR1_PCE;
            if (parity == UARTParity::Odd) {
                uart->CR1 |= USART_CR1_PS;
            } else {
                uart->CR1 &= ~USART_CR1_PS;
            }
        }

        // Configure stop bits
        uart->CR2 &= ~USART_CR2_STOP;
        uart->CR2 |= (static_cast<uint32_t>(stopBits) << 12);

        // Set baud rate
        uart->BRR = calculateBRR(static_cast<uint32_t>(baudRate));

        // Enable transmitter and receiver
        uart->CR1 |= USART_CR1_TE | USART_CR1_RE;

        // Enable UART
        uart->CR1 |= USART_CR1_UE;

        initialized = true;
    }

    /**
     * @brief Transmit a single byte (blocking)
     * @param data Byte to transmit
     * @param timeout Maximum number of iterations to wait (0 = infinite)
     * @return true if transmitted successfully, false if timeout
     */
    bool transmit(uint8_t data, uint32_t timeout = 100000) {
        uint32_t count = 0;

        // Wait for TX data register to be empty
        while (!(uart->SR & USART_SR_TXE)) {
            if (timeout && ++count > timeout) {
                return false;
            }
        }

        // Write data to transmit register
        uart->DR = data;
        return true;
    }

    /**
     * @brief Transmit a string (blocking)
     * @param str Null-terminated string to transmit
     * @param timeout Maximum number of iterations to wait per character
     * @return true if transmitted successfully, false if timeout
     */
    bool transmit(const char* str, uint32_t timeout = 100000) {
        while (*str) {
            if (!transmit(static_cast<uint8_t>(*str++), timeout)) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Transmit a buffer (blocking)
     * @param buffer Pointer to data buffer
     * @param length Number of bytes to transmit
     * @param timeout Maximum number of iterations to wait per byte
     * @return true if transmitted successfully, false if timeout
     */
    bool transmit(const uint8_t* buffer, uint32_t length, uint32_t timeout = 100000) {
        for (uint32_t i = 0; i < length; i++) {
            if (!transmit(buffer[i], timeout)) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Receive a single byte (blocking)
     * @param data Pointer to store received byte
     * @param timeout Maximum number of iterations to wait (0 = infinite)
     * @return true if received successfully, false if timeout
     */
    bool receive(uint8_t* data, uint32_t timeout = 100000) {
        uint32_t count = 0;

        // Wait for RX data register to be not empty
        while (!(uart->SR & USART_SR_RXNE)) {
            if (timeout && ++count > timeout) {
                return false;
            }
        }

        // Read data from receive register
        *data = static_cast<uint8_t>(uart->DR & 0xFF);
        return true;
    }

    /**
     * @brief Check if data is available to read
     * @return true if data is available
     */
    bool available() const {
        return (uart->SR & USART_SR_RXNE) != 0;
    }

    /**
     * @brief Check if transmit is complete
     * @return true if transmission is complete
     */
    bool transmitComplete() const {
        return (uart->SR & USART_SR_TC) != 0;
    }

    /**
     * @brief Flush the receive buffer
     */
    void flushReceive() {
        volatile uint8_t dummy;
        while (uart->SR & USART_SR_RXNE) {
            dummy = uart->DR;
        }
        (void)dummy;  // Suppress unused variable warning
    }

    /**
     * @brief Check if UART is initialized
     * @return true if initialized
     */
    bool isInitialized() const {
        return initialized;
    }

    /**
     * @brief Print formatted string (simple version)
     * @param str String to print
     */
    void print(const char* str) {
        transmit(str);
    }

    /**
     * @brief Print string with newline
     * @param str String to print
     */
    void println(const char* str) {
        transmit(str);
        transmit("\r\n");
    }

    /**
     * @brief Print a single character
     * @param c Character to print
     */
    void print(char c) {
        transmit(static_cast<uint8_t>(c));
    }

    /**
     * @brief Print unsigned integer
     * @param num Number to print
     */
    void print(uint32_t num) {
        char buffer[12];  // Max 10 digits + null + sign
        int i = 0;

        if (num == 0) {
            transmit('0');
            return;
        }

        // Convert to string (reverse order)
        while (num > 0) {
            buffer[i++] = '0' + (num % 10);
            num /= 10;
        }

        // Print in correct order
        while (i > 0) {
            transmit(static_cast<uint8_t>(buffer[--i]));
        }
    }

    /**
     * @brief Print signed integer
     * @param num Number to print
     */
    void print(int32_t num) {
        if (num < 0) {
            transmit('-');
            num = -num;
        }
        print(static_cast<uint32_t>(num));
    }
};

#endif // UART_HPP