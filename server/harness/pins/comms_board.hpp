/**
 * @file comms_board.hpp
 * @brief Init contract: L5 provides the host byte channel (UART or RTT)
 *
 * Both channels implement Transport::IByteChannel — the one documented L5 -> L1
 * exception (rule 3). Formalised as harness_l5_l1_bytes.hpp in migration step 5.
 */
#pragma once
#include <stdint.h>

namespace Transport { class IByteChannel; }

namespace Harness {

enum class CommsChannel : uint8_t {
    VCP_UART,   ///< USART2 via ST-LINK Virtual COM Port
    RTT         ///< SEGGER RTT channel 0
};

struct CommsBoardHw {
    Transport::IByteChannel* channel = nullptr;
};

bool initCommsBoard(CommsBoardHw& hw, CommsChannel type);

} // namespace Harness
