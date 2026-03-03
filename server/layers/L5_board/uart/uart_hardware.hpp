/**
 * @file uart_hardware.hpp
 * @brief Hardware UART driver using STM32 USART peripheral
 *
 * Encapsulates all CMSIS register access for USART GPIO, clocks,
 * baud rate, interrupts, and data transfer.  Implements IUartBus
 * so that L1 transport code never touches hardware registers.
 *
 * Follows the same pattern as SPIHardware (L5_board/spi/).
 */

#ifndef UART_HARDWARE_HPP
#define UART_HARDWARE_HPP

#include <stdint.h>
#include "X_vendor/CMSIS/stm32f401xe.h"
#include "F_platform/hal/iuart_bus.hpp"

class UartHardware : public IUartBus {
public:
    /**
     * @brief Construct hardware UART on specified peripheral and pins
     *
     * @param usart       USART peripheral (USART1, USART2, USART6)
     * @param gpioPort    GPIO port for TX and RX pins
     * @param txPin       Pin number for TX (0-15)
     * @param rxPin       Pin number for RX (0-15)
     * @param af          Alternate function number (e.g. 7 for USART2)
     * @param apbClockHz  APB bus clock frequency for baud calculation
     */
    UartHardware(USART_TypeDef* usart, GPIO_TypeDef* gpioPort,
                 uint8_t txPin, uint8_t rxPin,
                 uint8_t af, uint32_t apbClockHz)
        : m_usart(usart), m_gpioPort(gpioPort),
          m_txPin(txPin), m_rxPin(rxPin),
          m_af(af), m_apbClockHz(apbClockHz) {}

    // --- IUartBus implementation ---

    bool init(uint32_t baudRate, uint8_t irqPriority) override {
        initGPIO();
        initUSART(baudRate, irqPriority);
        return true;
    }

    void writeByte(uint8_t byte) override {
        while ((m_usart->SR & USART_SR_TXE) == 0) {}
        m_usart->DR = byte;
    }

    bool hasRxData() const override {
        return (m_usart->SR & USART_SR_RXNE) != 0;
    }

    uint8_t readRxByte() override {
        return static_cast<uint8_t>(m_usart->DR);
    }

    bool hasOverrunError() const override {
        return (m_usart->SR & USART_SR_ORE) != 0;
    }

    void clearErrors() override {
        (void)m_usart->DR;  // Reading DR after SR clears ORE
    }

    void waitTxComplete() override {
        while ((m_usart->SR & USART_SR_TC) == 0) {}
    }

    bool setBaudRate(uint32_t baudRate) override {
        // Disable USART
        m_usart->CR1 &= ~USART_CR1_UE;

        // Recalculate and set BRR
        configureBRR(baudRate);

        // Re-enable USART
        m_usart->CR1 |= USART_CR1_UE;

        // Clear any stale RX data
        (void)m_usart->DR;

        return true;
    }

private:
    USART_TypeDef* m_usart;
    GPIO_TypeDef*  m_gpioPort;
    uint8_t        m_txPin;
    uint8_t        m_rxPin;
    uint8_t        m_af;
    uint32_t       m_apbClockHz;

    /**
     * @brief Configure TX and RX GPIO pins as alternate function
     */
    void initGPIO() {
        // Enable GPIOA clock
        RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
        volatile uint32_t dummy = RCC->AHB1ENR;
        (void)dummy;

        // TX pin: alternate function, push-pull, high speed, no pull
        configurePin(m_txPin, m_af, false);

        // RX pin: alternate function, push-pull, high speed, pull-up
        configurePin(m_rxPin, m_af, true);
    }

    /**
     * @brief Configure one GPIO pin for USART alternate function
     */
    void configurePin(uint8_t pin, uint8_t af, bool pullUp) {
        // MODER: 10 = Alternate function
        m_gpioPort->MODER &= ~(3UL << (pin * 2));
        m_gpioPort->MODER |= (2UL << (pin * 2));

        // OTYPER: 0 = Push-pull
        m_gpioPort->OTYPER &= ~(1UL << pin);

        // OSPEEDR: 11 = High speed
        m_gpioPort->OSPEEDR |= (3UL << (pin * 2));

        // PUPDR: 01 = Pull-up, 00 = No pull
        m_gpioPort->PUPDR &= ~(3UL << (pin * 2));
        if (pullUp) {
            m_gpioPort->PUPDR |= (1UL << (pin * 2));
        }

        // AFR: Set alternate function
        uint8_t afrIdx = pin / 8;
        uint8_t afrPos = (pin % 8) * 4;
        m_gpioPort->AFR[afrIdx] &= ~(0xFUL << afrPos);
        m_gpioPort->AFR[afrIdx] |= (static_cast<uint32_t>(af) << afrPos);
    }

    /**
     * @brief Calculate and write BRR for the given baud rate
     *
     * Uses the same formula as the original uart_transport.cpp:
     * USARTDIV = apbClock / (16 * baud), encoded as mantissa.fraction
     */
    void configureBRR(uint32_t baudRate) {
        uint32_t intDiv = ((25 * m_apbClockHz) / (4 * baudRate));
        uint32_t mantissa = intDiv / 100;
        uint32_t fraction = ((intDiv - (mantissa * 100)) * 16 + 50) / 100;
        if (fraction >= 16) {
            mantissa++;
            fraction = 0;
        }
        m_usart->BRR = (mantissa << 4) | (fraction & 0x0F);
    }

    /**
     * @brief Initialize USART peripheral: clocks, CR1/2/3, BRR, NVIC
     */
    void initUSART(uint32_t baudRate, uint8_t irqPriority) {
        // Enable USART clock (APB1)
        RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
        volatile uint32_t dummy = RCC->APB1ENR;
        (void)dummy;

        // Disable and reset
        m_usart->CR1 = 0;
        m_usart->CR2 = 0;
        m_usart->CR3 = 0;

        // Set baud rate
        configureBRR(baudRate);

        // 8N1 + TX + RX + RXNE interrupt
        m_usart->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
        m_usart->CR2 = 0;
        m_usart->CR3 = 0;

        // NVIC
        NVIC_SetPriority(USART2_IRQn, irqPriority);
        NVIC_EnableIRQ(USART2_IRQn);

        // Enable USART
        m_usart->CR1 |= USART_CR1_UE;
    }
};

#endif // UART_HARDWARE_HPP
