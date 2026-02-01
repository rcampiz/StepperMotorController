/**
 * @file transport_interface.hpp
 * @brief Abstract transport interface for command/telemetry channels
 *
 * Supports two implementations:
 *   - UartTransport: USART2 via ST-LINK/J-Link VCP
 *   - RttTransport: SEGGER RTT channel 0
 */

#ifndef TRANSPORT_INTERFACE_HPP
#define TRANSPORT_INTERFACE_HPP

#include <cstdint>
#include <cstddef>

namespace Comms {

/**
 * @brief Abstract base class for bidirectional transport
 */
class ITransport {
public:
    virtual ~ITransport() = default;

    /**
     * @brief Initialize the transport layer
     * @return true on success
     */
    virtual bool init() = 0;

    /**
     * @brief Check if receive data is available
     * @return true if at least one byte can be read
     */
    virtual bool available() = 0;

    /**
     * @brief Non-blocking read
     * @param buffer Destination buffer
     * @param maxLen Maximum bytes to read
     * @return Number of bytes actually read (0 if none available)
     */
    virtual size_t read(uint8_t* buffer, size_t maxLen) = 0;

    /**
     * @brief Read single byte (blocking with timeout)
     * @param byte Output byte
     * @param timeoutMs Timeout in milliseconds
     * @return true if byte received, false on timeout
     */
    virtual bool readByte(uint8_t& byte, uint32_t timeoutMs) = 0;

    /**
     * @brief Write data (may buffer internally)
     * @param data Source buffer
     * @param len Number of bytes to write
     * @return Number of bytes written/queued
     */
    virtual size_t write(const uint8_t* data, size_t len) = 0;

    /**
     * @brief Write null-terminated string
     * @param str String to write
     * @return Number of bytes written
     */
    virtual size_t print(const char* str) = 0;

    /**
     * @brief Write string with newline
     * @param str String to write
     * @return Number of bytes written (including newline)
     */
    virtual size_t println(const char* str) = 0;

    /**
     * @brief Flush any buffered output
     */
    virtual void flush() = 0;
};

} // namespace Comms

#endif // TRANSPORT_INTERFACE_HPP
