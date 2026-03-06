/**
 * @file uart_transport.cpp
 * @brief UART transport implementation
 *
 * Implements ITransport over an IByteChannel.  All hardware register
 * access and ISR handling live in the channel — this file has no
 * CMSIS includes and no interrupt code.
 */

#include "L1_transport/uart_transport.hpp"
#include <string.h>

namespace Transport {

UartTransport::UartTransport(Harness::IByteChannel& channel, Harness::IClock& clock)
    : m_channel(channel), m_clock(&clock), m_baudRate(0) {}

bool UartTransport::init() {
    return m_channel.init();
}

bool UartTransport::available() {
    return m_channel.hasData();
}

size_t UartTransport::read(uint8_t* buffer, size_t maxLen) {
    return m_channel.read(buffer, maxLen);
}

bool UartTransport::readByte(uint8_t& byte, uint32_t timeoutMs) {
    // Try immediate read
    uint8_t buf;
    if (m_channel.read(&buf, 1) == 1) {
        byte = buf;
        return true;
    }

    // If timeout is 0, return immediately
    if (timeoutMs == 0) {
        return false;
    }

    // Wait with timeout using 1ms delay to avoid spinning hot
    uint32_t startTick = m_clock->getTickMs();

    while ((m_clock->getTickMs() - startTick) < timeoutMs) {
        if (m_channel.read(&buf, 1) == 1) {
            byte = buf;
            return true;
        }
        // Sleep 1ms to avoid CPU spin under heavy load
        m_clock->delayMs(1);
    }

    return false;
}

size_t UartTransport::write(const uint8_t* data, size_t len) {
    return m_channel.write(data, len);
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
    m_channel.flush();
}

bool UartTransport::setBaudRate(uint32_t baudRate) {
    // Wait for transmission complete
    m_channel.flush();

    // Small delay to let the last byte propagate through the USB bridge
    m_clock->delayMs(10);

    // Reconfigure hardware baud rate
    m_channel.setBaudRate(baudRate);

    m_baudRate = baudRate;
    return true;
}

} // namespace Transport
