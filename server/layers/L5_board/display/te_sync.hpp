/**
 * @file te_sync.hpp
 * @brief L5-internal: EXTI0 tearing-effect sync object (implements Board::ITearingSync)
 */
#pragma once
#include "L5_board/boundary/itearing_sync.hpp"

namespace Board {

/// Configure PA0/EXTI0 + DWT and return the sync object. Idempotent.
ITearingSync& initTearingSync();

} // namespace Board
