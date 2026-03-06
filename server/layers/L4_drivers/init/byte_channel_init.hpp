/**
 * @file byte_channel_init.hpp
 * @brief L4 init — byte channel construction
 *
 * Constructs concrete IByteChannel implementations:
 *   VCP_UART — cascades to L5 comms_board for UartHardware
 *   RTT      — uses L4 internal RttDriver
 */

#pragma once

#include <stdint.h>

namespace Harness { class IByteChannel; }

namespace Drivers {

enum class ChannelType : uint8_t {
    VCP_UART,  ///< USART2 via ST-LINK/J-Link Virtual COM Port
    RTT        ///< SEGGER RTT channel 0
};

struct ByteChannelResult {
    Harness::IByteChannel* channel = nullptr;
};

/**
 * @brief Initialize a byte channel
 *
 * @param out  Output struct with IByteChannel pointer
 * @param type Channel type to construct
 * @return true on success
 */
bool initByteChannel(ByteChannelResult& out, ChannelType type);

} // namespace Drivers
