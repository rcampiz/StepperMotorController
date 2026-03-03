/**
 * @file uart_transport.hpp
 * @brief UART transport implementation (ITransport over IUartBus)
 *
 * Provides interrupt-driven RX with a 256-byte ring buffer and
 * blocking TX.  All hardware register access is delegated to an
 * IUartBus implementation injected at construction time.
 *
 * No CMSIS types appear in this file.
 */

#ifndef UART_TRANSPORT_HPP
#define UART_TRANSPORT_HPP

#include "F_platform/hal/itransport.hpp"
#include "F_platform/hal/iclock.hpp"
#include "F_platform/hal/iuart_bus.hpp"
#include <stdint.h>
#include <stddef.h>

namespace Comms {

/**
 * @brief Lock-free single-producer single-consumer ring buffer
 *
 * Producer: USART IRQ handler (writes)
 * Consumer: UartTransport methods (reads)
 */
template<size_t SIZE>
class RingBuffer {
    static_assert((SIZE & (SIZE - 1)) == 0, "SIZE must be power of 2");
public:
    RingBuffer() : m_head(0), m_tail(0), m_overflowCount(0) {}

    /**
     * @brief Check if buffer has data
     */
    bool available() const {
        return m_head != m_tail;
    }

    /**
     * @brief Get number of bytes available to read
     */
    size_t count() const {
        return (m_head - m_tail) & (SIZE - 1);
    }

    /**
     * @brief Get free space in buffer
     */
    size_t free() const {
        return SIZE - 1 - count();
    }

    /**
     * @brief Push byte into buffer (called from ISR)
     * @return true if successful, false if buffer full (increments overflow counter)
     */
    bool push(uint8_t byte) {
        size_t nextHead = (m_head + 1) & (SIZE - 1);
        if (nextHead == m_tail) {
            m_overflowCount++;  // Track dropped bytes
            return false;
        }
        m_buffer[m_head] = byte;
        m_head = nextHead;
        return true;
    }

    /**
     * @brief Pop byte from buffer
     * @return true if successful, false if buffer empty
     */
    bool pop(uint8_t& byte) {
        if (m_head == m_tail) {
            return false;  // Buffer empty
        }
        byte = m_buffer[m_tail];
        m_tail = (m_tail + 1) & (SIZE - 1);
        return true;
    }

    /**
     * @brief Peek at next byte without removing
     */
    bool peek(uint8_t& byte) const {
        if (m_head == m_tail) {
            return false;
        }
        byte = m_buffer[m_tail];
        return true;
    }

    /**
     * @brief Clear the buffer
     */
    void clear() {
        m_tail = m_head;
    }

    /**
     * @brief Get overflow count (bytes dropped due to full buffer)
     */
    uint32_t getOverflowCount() const {
        return m_overflowCount;
    }

    /**
     * @brief Check if overflow has occurred since last clear
     */
    bool hasOverflowed() const {
        return m_overflowCount > 0;
    }

    /**
     * @brief Clear overflow counter
     */
    void clearOverflow() {
        m_overflowCount = 0;
    }

private:
    volatile size_t m_head;       // Write index (ISR)
    volatile size_t m_tail;       // Read index (task)
    volatile uint32_t m_overflowCount;  // Dropped byte counter
    uint8_t m_buffer[SIZE];
};

/**
 * @brief UART transport with interrupt-driven RX
 *
 * Delegates all hardware access to an IUartBus.
 * Ring buffer and ISR routing live here (transport concerns).
 *
 * @note Single instance only - ISR routes to s_instance.
 *       Creating multiple instances will assert in init().
 */
class UartTransport : public ITransport {
public:
    /**
     * @brief Construct UART transport
     * @param bus       Hardware UART driver (IUartBus implementation)
     * @param clock     Platform clock for delays and timeouts
     * @param baudRate  Baud rate (default 115200)
     * @param irqPriority  NVIC priority for USART RX interrupt (default 6)
     */
    UartTransport(IUartBus& bus, IClock& clock,
                  uint32_t baudRate = 115200, uint8_t irqPriority = 6);

    bool init() override;
    bool available() override;
    size_t read(uint8_t* buffer, size_t maxLen) override;
    bool readByte(uint8_t& byte, uint32_t timeoutMs) override;
    size_t write(const uint8_t* data, size_t len) override;
    size_t print(const char* str) override;
    size_t println(const char* str) override;
    void flush() override;

    /**
     * @brief Change baud rate at runtime
     *
     * Waits for current TX to complete, reconfigures hardware, clears RX buffer.
     */
    bool setBaudRate(uint32_t baudRate) override;

    /**
     * @brief Handle USART RX interrupt (called from ISR)
     */
    void handleIRQ();

    /**
     * @brief Get singleton instance for ISR access
     */
    static UartTransport* getInstance() { return s_instance; }

    /**
     * @brief Check if RX buffer has overflowed (bytes were dropped)
     */
    bool hasOverflowed() const { return m_rxBuffer.hasOverflowed(); }

    /**
     * @brief Get count of dropped bytes due to buffer overflow
     */
    uint32_t getOverflowCount() const { return m_rxBuffer.getOverflowCount(); }

    /**
     * @brief Clear overflow flag/counter
     */
    void clearOverflow() { m_rxBuffer.clearOverflow(); }

private:
    IUartBus& m_bus;
    IClock* m_clock;
    uint32_t m_baudRate;
    uint8_t m_irqPriority;
    RingBuffer<256> m_rxBuffer;

    static UartTransport* s_instance;
};

} // namespace Comms

// C linkage for IRQ handler
extern "C" void USART2_IRQHandler(void);

#endif // UART_TRANSPORT_HPP
