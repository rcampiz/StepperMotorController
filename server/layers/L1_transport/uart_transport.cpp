/**
 * @file uart_transport.cpp
 * @brief UART transport implementation
 *
 * Implements ITransport over an IUartBus.  All hardware register
 * access is delegated to the bus — this file has no CMSIS includes.
 */

#include "L1_transport/uart_transport.hpp"
#include <string.h>

// Debug: IRQ call counter
volatile uint32_t g_usart2IrqCount = 0;
volatile uint32_t g_usart2RxCount = 0;

namespace Comms {

// Singleton instance for ISR access
UartTransport* UartTransport::s_instance = nullptr;

UartTransport::UartTransport(IUartBus& bus, IClock& clock,
                             uint32_t baudRate, uint8_t irqPriority)
    : m_bus(bus), m_clock(&clock), m_baudRate(baudRate), m_irqPriority(irqPriority) {}

bool UartTransport::init() {
    // Enforce single instance - ISR can only route to one instance
    if (s_instance != nullptr) {
        return false;
    }

    // Set singleton for ISR access
    s_instance = this;

    // Delegate all hardware init (GPIO, clocks, USART, NVIC) to the bus
    return m_bus.init(m_baudRate, m_irqPriority);
}

bool UartTransport::available() {
    return m_rxBuffer.available();
}

size_t UartTransport::read(uint8_t* buffer, size_t maxLen) {
    size_t count = 0;
    while (count < maxLen && m_rxBuffer.pop(buffer[count])) {
        count++;
    }
    return count;
}

bool UartTransport::readByte(uint8_t& byte, uint32_t timeoutMs) {
    // Try immediate read
    if (m_rxBuffer.pop(byte)) {
        return true;
    }

    // If timeout is 0, return immediately
    if (timeoutMs == 0) {
        return false;
    }

    // Wait with timeout using 1ms delay to avoid spinning hot
    uint32_t startTick = m_clock->getTickMs();

    while ((m_clock->getTickMs() - startTick) < timeoutMs) {
        if (m_rxBuffer.pop(byte)) {
            return true;
        }
        // Sleep 1ms to avoid CPU spin under heavy load
        m_clock->delayMs(1);
    }

    return false;
}

size_t UartTransport::write(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        m_bus.writeByte(data[i]);
    }
    return len;
}

size_t UartTransport::print(const char* str) {
    return write(reinterpret_cast<const uint8_t*>(str), strlen(str));
}

size_t UartTransport::println(const char* str) {
    size_t n = print(str);
    n += write(reinterpret_cast<const uint8_t*>("\r\n"), 2);
    return n;
}

void UartTransport::flush() {
    m_bus.waitTxComplete();
}

bool UartTransport::setBaudRate(uint32_t baudRate) {
    // Wait for transmission complete (all bits shifted out)
    m_bus.waitTxComplete();

    // Small delay to let the last byte propagate through the bridge
    m_clock->delayMs(10);

    // Reconfigure hardware baud rate
    m_bus.setBaudRate(baudRate);

    // Clear any garbage from RX buffer
    m_rxBuffer.clear();

    m_baudRate = baudRate;
    return true;
}

void UartTransport::handleIRQ() {
    ::g_usart2IrqCount++;

    // Check for receive data ready
    if (m_bus.hasRxData()) {
        auto byte = m_bus.readRxByte();
        m_rxBuffer.push(byte);  // Overflow counter incremented if full
        ::g_usart2RxCount++;
    }

    // Clear overrun error if set
    if (m_bus.hasOverrunError()) {
        m_bus.clearErrors();
    }
}

} // namespace Comms

// USART2 IRQ Handler
extern "C" void USART2_IRQHandler(void) {
    Comms::UartTransport* instance = Comms::UartTransport::getInstance();
    if (instance != nullptr) {
        instance->handleIRQ();
    }
}
