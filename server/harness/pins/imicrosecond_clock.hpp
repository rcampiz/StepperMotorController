/**
 * @file imicrosecond_clock.hpp
 * @brief Harness connector for Board::IMicrosecondClock + global accessors
 *
 * Interface definition owned by Board:: (L5_board/boundary/imicrosecond_clock.hpp).
 * This connector provides the global accessor (wired by composition root).
 */

#pragma once

#include "L5_board/boundary/imicrosecond_clock.hpp"

namespace Harness {

/// Global accessor (wired by composition root)
void setMicrosecondClock(Board::IMicrosecondClock* clock);
Board::IMicrosecondClock* microsecondClock();

} // namespace Harness
