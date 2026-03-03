/**
 * @file irtt_channel.hpp
 * @brief Abstract RTT channel interface
 *
 * Decouples L1 transport from the SEGGER RTT vendor library.
 * Implemented by RttDriver (L5_board) which wraps the actual
 * SEGGER_RTT_Read / SEGGER_RTT_Write calls.
 *
 * No vendor includes appear in this interface.
 */

#ifndef IRTT_CHANNEL_HPP
#define IRTT_CHANNEL_HPP

#include <stddef.h>

class IRttChannel {
public:
    virtual ~IRttChannel() = default;

    /**
     * @brief Initialize the RTT channel (if needed)
     * @return true on success
     */
    virtual bool init() = 0;

    /**
     * @brief Check if receive data is available on this channel
     */
    virtual bool hasData() = 0;

    /**
     * @brief Read up to maxLen bytes from the channel
     * @return Number of bytes actually read
     */
    virtual size_t read(void* buffer, size_t maxLen) = 0;

    /**
     * @brief Write len bytes to the channel
     * @return Number of bytes actually written
     */
    virtual size_t write(const void* data, size_t len) = 0;
};

#endif // IRTT_CHANNEL_HPP
