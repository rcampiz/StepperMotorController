/**
 * @file board_init.hpp
 * @brief L3 service for early board hardware initialization
 *
 * Provides L3-level entry points for Phase 1 hardware init.
 * system_init.cpp calls these; implementations cascade down
 * through harness boundary contracts to L5/F_platform.
 */

#pragma once

namespace Services {

/**
 * @brief Configure system clocks (L3 → harness → L5)
 * @return true on success
 */
bool initClockBoard();

/**
 * @brief Initialize debug UART output (L3 → harness → F_platform)
 */
void initDebugOutput();

} // namespace Services
