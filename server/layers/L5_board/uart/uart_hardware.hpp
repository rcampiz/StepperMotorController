/**
 * @file uart_hardware.hpp
 * @brief Hardware UART driver using STM32 USART peripheral
 *
 * Encapsulates all CMSIS register access for USART GPIO, clocks,
 * baud rate, interrupts, and data transfer.  Implements IByteChannel
 * so that L1 transport code never touches hardware registers.
 *
 * Owns the RX ring buffer and ISR handler — L1 sees only the
 * IByteChannel abstraction (hasData / read / write / flush).
 */

#ifndef UART_HARDWARE_HPP
#define UART_HARDWARE_HPP

#include <stdint.h>
#include <stddef.h>
#include "X_vendor/CMSIS/stm32f401xe.h"
#include "harness/defs/harness_l5_l1_bytes.hpp"
#include "F_platform/boundary/itask_notifier.hpp"
#include "F_platform/boundary/iclock.hpp"
#include "harness/pins/imicrosecond_clock.hpp"
#include "harness/pins/oob_estop.hpp"

namespace Board {

/**
 * @brief Lock-free single-producer single-consumer ring buffer
 *
 * Producer: USART IRQ handler (writes)
 * Consumer: UartHardware::read / hasData (reads)
 */
template<size_t SIZE>
class RingBuffer {
    static_assert((SIZE & (SIZE - 1)) == 0, "SIZE must be power of 2");
public:
    RingBuffer() : m_head(0), m_tail(0), m_overflowCount(0) {}

    bool available() const {
        return m_head != m_tail;
    }

    size_t count() const {
        return (m_head - m_tail) & (SIZE - 1);
    }

    bool push(uint8_t byte) {
        size_t nextHead = (m_head + 1) & (SIZE - 1);
        if (nextHead == m_tail) {
            m_overflowCount++;
            return false;
        }
        m_buffer[m_head] = byte;
        m_head = nextHead;
        return true;
    }

    bool pop(uint8_t& byte) {
        if (m_head == m_tail) {
            return false;
        }
        byte = m_buffer[m_tail];
        m_tail = (m_tail + 1) & (SIZE - 1);
        return true;
    }

    void clear() {
        m_tail = m_head;
    }

    uint32_t getOverflowCount() const { return m_overflowCount; }
    bool hasOverflowed() const { return m_overflowCount > 0; }
    void clearOverflow() { m_overflowCount = 0; }

private:
    volatile size_t m_head;
    volatile size_t m_tail;
    volatile uint32_t m_overflowCount;
    uint8_t m_buffer[SIZE];
};

class UartHardware : public Transport::IByteChannel {
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
     * @param baudRate    Initial baud rate (e.g. 115200)
     * @param irqPriority NVIC priority for USART RX interrupt
     */
    UartHardware(USART_TypeDef* usart, GPIO_TypeDef* gpioPort,
                 uint8_t txPin, uint8_t rxPin,
                 uint8_t af, uint32_t apbClockHz,
                 uint32_t baudRate, uint8_t irqPriority)
        : m_usart(usart), m_gpioPort(gpioPort),
          m_txPin(txPin), m_rxPin(rxPin),
          m_af(af), m_apbClockHz(apbClockHz),
          m_baudRate(baudRate), m_irqPriority(irqPriority) {}

    // --- IByteChannel implementation ---

    bool init() override;

    bool hasData() override {
        return m_rxBuffer.available();
    }

    size_t read(uint8_t* buffer, size_t maxLen) override {
        size_t count = 0;
        while (count < maxLen && m_rxBuffer.pop(buffer[count])) {
            count++;
        }
        return count;
    }

    /**
     * Queue bytes for the TXE interrupt to send (Phase 4 step 2). Returns as soon as the
     * bytes are in the ring, so a 1 KB status push costs the Comms task microseconds, not
     * 90 ms of busy-waiting at 115200 (which starved the Encoder task). When the ring is
     * full the caller sleeps 1 ms at a time while the interrupt drains it; without a clock
     * (pre-scheduler) it spins.
     */
    size_t write(const uint8_t* data, size_t len) override {
        for (size_t i = 0; i < len; i++) {
            while (!m_txBuffer.push(data[i])) {
                m_usart->CR1 |= USART_CR1_TXEIE;         // make sure the drain is running
                if (m_txClock != nullptr) {
                    m_txClock->delayMs(1);               // ~11 bytes leave per ms at 115200
                }
            }
        }
        m_usart->CR1 |= USART_CR1_TXEIE;                 // TXE is already set when DR is idle
        return len;
    }
    /// Wait until every queued byte has left the shift register (before a baud change or
    /// a binary transfer).
    void flush() override {
        while (m_txBuffer.available()) {
            m_usart->CR1 |= USART_CR1_TXEIE;
            if (m_txClock != nullptr) {
                m_txClock->delayMs(1);
            }
        }
        while ((m_usart->SR & USART_SR_TC) == 0) {}
    }
    /// Task context sleeps instead of spinning while the TX ring is full (composition root).
    void setTxClock(Platform::IClock* clock) { m_txClock = clock; }
    size_t txPending() const { return m_txBuffer.count(); }

    uint32_t baudRate() const override { return m_baudRate; }

    bool setBaudRate(uint32_t baudRate) override {
        // Disable USART
        m_usart->CR1 &= ~USART_CR1_UE;

        // Recalculate and set BRR
        configureBRR(baudRate);

        // Re-enable USART
        m_usart->CR1 |= USART_CR1_UE;

        // Clear any stale RX data from hardware and ring buffer
        (void)m_usart->DR;
        m_rxBuffer.clear();

        m_baudRate = baudRate;
        return true;
    }

    // --- ISR support ---

    void handleIRQ() {
        // Check for receive data ready
        if ((m_usart->SR & USART_SR_RXNE) != 0) {
            auto byte = static_cast<uint8_t>(m_usart->DR);
            if (byte == OOB_ESTOP_BYTE && m_oobEnabled) {
                // Out-of-band ESTOP (Phase 4 step 1): acted on here, before the ring, even
                // when the ring is full or the Comms task is deep inside a handler. Never
                // queued, never echoed, no reply (docs/PROTOCOL.md section 5).
                Harness::invokeOobEstop();
                return;
            }
            m_rxBuffer.push(byte);
            // Arrival stamp of the newest byte (TIM5 microseconds): the ESTOP reply reports
            // the time from the line's last byte to the Hi-Z write (Phase 4 Step 0 baseline)
            if (Platform::IMicrosecondClock* us = Harness::microsecondClock()) {
                m_lastRxUs = us->getTickUs();
            }
            if (m_wakeNotifier != nullptr) {
                m_wakeNotifier->notifyFromISR(m_wakeTask);   // RTOS-safe: USART2 IRQ is at NVIC 6
            }
        }

        // Clear overrun error if set
        if ((m_usart->SR & USART_SR_ORE) != 0) {
            (void)m_usart->DR;
        }

        // Transmit: feed the data register from the ring; stop the interrupt when empty
        if ((m_usart->CR1 & USART_CR1_TXEIE) != 0 && (m_usart->SR & USART_SR_TXE) != 0) {
            uint8_t out;
            if (m_txBuffer.pop(out)) {
                m_usart->DR = out;
            } else {
                m_usart->CR1 &= ~USART_CR1_TXEIE;
            }
        }
    }

    static UartHardware* getInstance() { return s_instance; }

    /// Route RX interrupts to a task notification (nullptr disables). Pre-scheduler or task context.
    void setRxWake(Platform::ITaskNotifier* notifier, Platform::TaskHandle task) {
        m_wakeTask = task;
        m_wakeNotifier = notifier;
    }

    // --- Debug accessors ---

    bool hasOverflowed() const { return m_rxBuffer.hasOverflowed(); }
    uint32_t rxOverflowCount() const override { return m_rxBuffer.getOverflowCount(); }
    uint32_t lastRxTickUs() const override { return m_lastRxUs; }
    void setOobEstopEnabled(bool enabled) override { m_oobEnabled = enabled; }
    static constexpr uint8_t OOB_ESTOP_BYTE = 0x18;
    uint32_t getOverflowCount() const { return m_rxBuffer.getOverflowCount(); }
    void clearOverflow() { m_rxBuffer.clearOverflow(); }

private:
    USART_TypeDef* m_usart;
    GPIO_TypeDef*  m_gpioPort;
    uint8_t        m_txPin;
    uint8_t        m_rxPin;
    uint8_t        m_af;
    uint32_t       m_apbClockHz;
    uint32_t       m_baudRate;
    volatile uint32_t m_lastRxUs = 0;
    volatile bool  m_oobEnabled = true;
    uint8_t        m_irqPriority;
    RingBuffer<1024> m_rxBuffer;   // F1.2: 89 ms of host traffic at 115200 baud (was 256 B = 22 ms)
    RingBuffer<4096> m_txBuffer;   // Phase 4 step 2: holds a whole SYST:HELP? (~3.2 KB) or three status pushes
    Platform::IClock* m_txClock = nullptr;
    Platform::ITaskNotifier* m_wakeNotifier = nullptr;   // optional ISR -> Comms task wake
    Platform::TaskHandle     m_wakeTask = nullptr;

    static UartHardware* s_instance;

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

    void initUSART() {
        // Enable USART clock (APB1)
        RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
        volatile uint32_t dummy = RCC->APB1ENR;
        (void)dummy;

        // Disable and reset
        m_usart->CR1 = 0;
        m_usart->CR2 = 0;
        m_usart->CR3 = 0;

        // Set baud rate
        configureBRR(m_baudRate);

        // 8N1 + TX + RX + RXNE interrupt
        m_usart->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
        m_usart->CR2 = 0;
        m_usart->CR3 = 0;

        // NVIC
        NVIC_SetPriority(USART2_IRQn, m_irqPriority);
        NVIC_EnableIRQ(USART2_IRQn);

        // Enable USART
        m_usart->CR1 |= USART_CR1_UE;
    }
};

} // namespace Board

#endif // UART_HARDWARE_HPP
