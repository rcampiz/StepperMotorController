/**
 * @file comms_board.hpp
 * @brief Harness contract for comms board initialization
 *
 * Declares init struct and function for the comms (UART) subsystem.
 * L5 provides the implementation; L1 transport calls through this harness.
 */

#pragma once

namespace Harness { class IByteChannel; }

namespace Harness {

struct CommsBoardHw {
    IByteChannel* channel = nullptr;
};

/**
 * @brief Initialize comms board UART hardware
 *
 * Constructs UART hardware for VCP. Implemented by L5 board layer.
 *
 * @param hw Output struct populated with UART hardware pointer
 * @return true on success
 */
bool initCommsBoard(CommsBoardHw& hw);

} // namespace Harness
