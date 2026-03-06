/**
 * @file clock_board.hpp
 * @brief Harness contract for system clock initialization
 *
 * Declares the clock init function for the board.
 * L5 provides the implementation (CMSIS register access).
 * Composition root calls through this harness interface.
 */

#pragma once

namespace Harness {

/**
 * @brief Configure system clocks
 *
 * Board-specific: HSE, PLL, flash latency, bus dividers.
 * Implemented by L5 board layer.
 *
 * @return true on success
 */
bool initClockBoard();

} // namespace Harness
