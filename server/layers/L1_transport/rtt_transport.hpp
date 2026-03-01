/**
 * @file rtt_transport.hpp
 * @brief SEGGER RTT transport implementation
 *
 * Uses RTT channel 0 for bidirectional console/command traffic.
 * Channel 1 is reserved for SystemView.
 *
 * Requires: Middlewares/SEGGER/RTT/SEGGER_RTT.h
 */

#ifndef RTT_TRANSPORT_HPP
#define RTT_TRANSPORT_HPP

#include "F_platform/interfaces/itransport.hpp"

namespace Comms {

class RttTransport : public ITransport {
public:
    /**
     * @brief Construct RTT transport
     * @param channel RTT channel to use (default 0)
     */
    explicit RttTransport(unsigned channel = 0);

    bool init() override;
    bool available() override;
    size_t read(uint8_t* buffer, size_t maxLen) override;
    bool readByte(uint8_t& byte, uint32_t timeoutMs) override;
    size_t write(const uint8_t* data, size_t len) override;
    size_t print(const char* str) override;
    size_t println(const char* str) override;
    void flush() override;

private:
    unsigned m_channel;
};

} // namespace Comms

#endif // RTT_TRANSPORT_HPP
