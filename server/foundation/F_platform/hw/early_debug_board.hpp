/**
 * @file early_debug_board.hpp
 * @brief Init contract for pre-scheduler debug output (Platform namespace, reached via harness/pins/)
 *
 * Bare-metal UART output for debugging init failures before the
 * scheduler starts. F_platform provides the implementation.
 */

#pragma once

namespace Platform {

/**
 * @brief Initialize debug UART (call before any print)
 */
void initDebugOutput();

/**
 * @brief Print string to debug UART (no newline)
 */
void debugPrint(const char* str);

/**
 * @brief Print string to debug UART with newline
 */
void debugPrintln(const char* str);

} // namespace Platform
