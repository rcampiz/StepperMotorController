/**
 * @file rtt_transport.cpp
 * @brief RTT transport implementation
 *
 * Implements ITransport over an IByteChannel.  All vendor library
 * access is delegated to the channel — this file has no SEGGER includes.
 */

#include "L1_transport/rtt_transport.hpp"
#include <string.h>

namespace Transport {

RttTransport::RttTransport(Harness::IByteChannel& channel) : m_channel(channel) {}

bool RttTransport::init() {
    return m_channel.init();
}

bool RttTransport::available() {
    return m_channel.hasData();
}

size_t RttTransport::read(uint8_t* buffer, size_t maxLen) {
    return m_channel.read(buffer, maxLen);
}

bool RttTransport::readByte(uint8_t& byte, uint32_t timeoutMs) {
    // Simple polling with timeout
    // In FreeRTOS context, could use vTaskDelay for better efficiency
    (void)timeoutMs;  // For now, just check once
    if (m_channel.hasData()) {
        return m_channel.read(&byte, 1) == 1;
    }
    return false;
}

size_t RttTransport::write(const uint8_t* data, size_t len) {
    return m_channel.write(data, len);
}

size_t RttTransport::print(const char* str) {
    return write(reinterpret_cast<const uint8_t*>(str), strlen(str));
}

size_t RttTransport::println(const char* str) {
    size_t n = print(str);
    n += write(reinterpret_cast<const uint8_t*>("\r\n"), 2);
    return n;
}

void RttTransport::flush() {
    // RTT doesn't require explicit flush - writes go directly to buffer
}

} // namespace Transport
