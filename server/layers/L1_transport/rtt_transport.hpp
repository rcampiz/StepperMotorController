/**
 * @file rtt_transport.hpp
 * @brief RTT transport — ITransport over IByteChannel
 *
 * Delegates all RTT operations to an IByteChannel implementation
 * injected at construction time.
 *
 * No vendor includes appear in this file.
 */

#ifndef RTT_TRANSPORT_HPP
#define RTT_TRANSPORT_HPP

#include "harness/pins/itransport.hpp"
#include "harness/pins/ibyte_channel.hpp"

namespace Transport {

class RttTransport : public Harness::ITransport {
public:
    /**
     * @brief Construct RTT transport
     * @param channel  Byte channel (IByteChannel implementation)
     */
    explicit RttTransport(Harness::IByteChannel& channel);

    bool init() override;
    bool available() override;
    size_t read(uint8_t* buffer, size_t maxLen) override;
    bool readByte(uint8_t& byte, uint32_t timeoutMs) override;
    size_t write(const uint8_t* data, size_t len) override;
    size_t print(const char* str) override;
    size_t println(const char* str) override;
    void flush() override;

private:
    Harness::IByteChannel& m_channel;
};

} // namespace Transport

#endif // RTT_TRANSPORT_HPP
