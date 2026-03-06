/**
 * @file uart_transport.hpp
 * @brief UART transport — ITransport over IByteChannel
 *
 * Thin adapter: delegates byte I/O to an IByteChannel, adds
 * string formatting (print/println) and timeout-based readByte.
 *
 * No CMSIS types, no ring buffers, no ISR handling — those live
 * in the IByteChannel implementation (L5 UartHardware).
 */

#ifndef UART_TRANSPORT_HPP
#define UART_TRANSPORT_HPP

#include "harness/pins/itransport.hpp"
#include "harness/pins/ibyte_channel.hpp"
#include "harness/pins/iclock.hpp"
#include <stdint.h>

namespace Transport {

class UartTransport : public Harness::ITransport {
public:
    /**
     * @brief Construct UART transport
     * @param channel  Byte channel (IByteChannel implementation)
     * @param clock    Platform clock for delays and timeouts
     */
    UartTransport(Harness::IByteChannel& channel, Harness::IClock& clock);

    bool init() override;
    bool available() override;
    size_t read(uint8_t* buffer, size_t maxLen) override;
    bool readByte(uint8_t& byte, uint32_t timeoutMs) override;
    size_t write(const uint8_t* data, size_t len) override;
    size_t print(const char* str) override;
    size_t println(const char* str) override;
    void flush() override;
    bool setBaudRate(uint32_t baudRate) override;

private:
    Harness::IByteChannel& m_channel;
    Harness::IClock* m_clock;
    uint32_t m_baudRate;
};

} // namespace Transport

#endif // UART_TRANSPORT_HPP
