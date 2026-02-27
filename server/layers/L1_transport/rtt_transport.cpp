/**
 * @file rtt_transport.cpp
 * @brief SEGGER RTT transport implementation
 */

#include "L1_transport/rtt_transport.hpp"
#include "X_middlewares/SEGGER/RTT/SEGGER_RTT.h"
#include <string.h>

namespace Comms {

RttTransport::RttTransport(unsigned channel) : m_channel(channel) {}

bool RttTransport::init() {
  // RTT is auto-initialized by SEGGER_RTT_Init() on first use
  // Optionally configure buffer sizes here
  return true;
}

bool RttTransport::available() { return SEGGER_RTT_HasData(m_channel) > 0; }

size_t RttTransport::read(uint8_t *buffer, size_t maxLen) {
  return SEGGER_RTT_Read(m_channel, buffer, maxLen);
}

bool RttTransport::readByte(uint8_t &byte, uint32_t timeoutMs) {
  // Simple polling with timeout
  // In FreeRTOS context, could use vTaskDelay for better efficiency
  (void)timeoutMs; // For now, just check once
  if (SEGGER_RTT_HasData(m_channel)) {
    return SEGGER_RTT_Read(m_channel, &byte, 1) == 1;
  }
  return false;
}

size_t RttTransport::write(const uint8_t *data, size_t len) {
  return SEGGER_RTT_Write(m_channel, data, len);
}

size_t RttTransport::print(const char *str) {
  return write(reinterpret_cast<const uint8_t *>(str), strlen(str));
}

size_t RttTransport::println(const char *str) {
  size_t n = print(str);
  n += write(reinterpret_cast<const uint8_t *>("\r\n"), 2);
  return n;
}

void RttTransport::flush() {
  // RTT doesn't require explicit flush - writes go directly to buffer
}

} // namespace Comms
