/**
 * @file board_init.cpp
 * @brief L3 board init — cascades to L5/F_platform via harness
 */

#include "L3_services/infra/board_init/board_init.hpp"
#include "harness/pins/clock_board.hpp"
#include "harness/pins/early_debug_board.hpp"

namespace Services {

bool initClockBoard() {
    return Harness::initClockBoard();
}

void initDebugOutput() {
    Harness::initDebugOutput();
}

} // namespace Services
