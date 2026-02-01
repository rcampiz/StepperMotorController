/**
 * @file rtt_transport.cpp
 * @brief SEGGER RTT transport implementation
 */

#include "comms/rtt_transport.hpp"
// TODO: Uncomment when SEGGER RTT is added
// #include "SEGGER_RTT.h"
#include <cstring>

namespace Comms {

RttTransport::RttTransport(unsigned channel)
    : m_channel(channel)
{
}

bool RttTransport::init()
{
    // RTT is auto-initialized by SEGGER_RTT_Init() on first use
    // Optionally configure buffer sizes here
    return true;
}

bool RttTransport::available()
{
    // TODO: return SEGGER_RTT_HasKey() for channel 0
    // or SEGGER_RTT_GetBytesInBuffer(m_channel) > 0
    return false;
}

size_t RttTransport::read(uint8_t* buffer, size_t maxLen)
{
    // TODO: return SEGGER_RTT_Read(m_channel, buffer, maxLen);
    (void)buffer;
    (void)maxLen;
    return 0;
}

bool RttTransport::readByte(uint8_t& byte, uint32_t timeoutMs)
{
    // TODO: Poll with timeout using SEGGER_RTT_GetKey() or Read
    (void)byte;
    (void)timeoutMs;
    return false;
}

size_t RttTransport::write(const uint8_t* data, size_t len)
{
    // TODO: return SEGGER_RTT_Write(m_channel, data, len);
    (void)data;
    (void)len;
    return 0;
}

size_t RttTransport::print(const char* str)
{
    return write(reinterpret_cast<const uint8_t*>(str), strlen(str));
}

size_t RttTransport::println(const char* str)
{
    size_t n = print(str);
    n += write(reinterpret_cast<const uint8_t*>("\r\n"), 2);
    return n;
}

void RttTransport::flush()
{
    // RTT doesn't require explicit flush - writes go directly to buffer
}

} // namespace Comms
