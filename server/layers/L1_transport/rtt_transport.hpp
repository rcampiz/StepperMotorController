/**
 * @file rtt_transport.hpp
 * @brief RTT transport implementation (ITransport over IRttChannel)
 *
 * Delegates all RTT operations to an IRttChannel implementation
 * injected at construction time.
 *
 * No vendor includes appear in this file.
 */

#ifndef RTT_TRANSPORT_HPP
#define RTT_TRANSPORT_HPP

#include "F_platform/hal/itransport.hpp"
#include "F_platform/hal/irtt_channel.hpp"

namespace Comms {

class RttTransport : public ITransport {
public:
    /**
     * @brief Construct RTT transport
     * @param channel  RTT channel driver (IRttChannel implementation)
     */
    explicit RttTransport(IRttChannel& channel);

    bool init() override;
    bool available() override;
    size_t read(uint8_t* buffer, size_t maxLen) override;
    bool readByte(uint8_t& byte, uint32_t timeoutMs) override;
    size_t write(const uint8_t* data, size_t len) override;
    size_t print(const char* str) override;
    size_t println(const char* str) override;
    void flush() override;

private:
    IRttChannel& m_channel;
};

} // namespace Comms

#endif // RTT_TRANSPORT_HPP
