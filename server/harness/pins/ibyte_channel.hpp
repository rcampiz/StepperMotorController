/**
 * @file ibyte_channel.hpp
 * @brief Abstract byte channel interface
 *
 * Defines the minimal contract L1 transport needs from a byte source/sink.
 * Implemented by hardware drivers (UartHardware in L5, RttDriver in L4)
 * which are injected at construction time by the wiring/composition root.
 *
 * L1 transport code includes only this interface — never hardware headers.
 */

#ifndef IBYTE_CHANNEL_HPP
#define IBYTE_CHANNEL_HPP

#include <stddef.h>
#include <stdint.h>

namespace Harness {

class IByteChannel {
public:
    virtual ~IByteChannel() = default;

    /**
     * @brief Initialize the channel hardware
     * @return true on success
     */
    virtual bool init() = 0;

    /**
     * @brief Check if receive data is available
     * @return true if at least one byte can be read
     */
    virtual bool hasData() = 0;

    /**
     * @brief Read up to maxLen bytes from the channel
     * @param buffer Destination buffer
     * @param maxLen Maximum bytes to read
     * @return Number of bytes actually read (0 if none available)
     */
    virtual size_t read(uint8_t* buffer, size_t maxLen) = 0;

    /**
     * @brief Write bytes to the channel
     * @param data Source buffer
     * @param len Number of bytes to write
     * @return Number of bytes written
     */
    virtual size_t write(const uint8_t* data, size_t len) = 0;

    /**
     * @brief Flush any buffered output (wait for transmission complete)
     */
    virtual void flush() {}

    /**
     * @brief Reconfigure channel baud rate (UART-specific, no-op for others)
     * @param baudRate New baud rate
     * @return true if supported and applied
     */
    virtual bool setBaudRate(uint32_t baudRate) { (void)baudRate; return false; }
};

} // namespace Harness

#endif // IBYTE_CHANNEL_HPP
