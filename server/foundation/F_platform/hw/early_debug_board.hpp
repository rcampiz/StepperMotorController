/**
 * @file early_debug_board.hpp
 * @brief Init contract for pre-scheduler debug output (Platform namespace, reached via harness/pins/)
 *
 * Bare-metal UART output for debugging init failures before the
 * scheduler starts. F_platform provides the implementation.
 */

#pragma once

#include <stdint.h>

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

/**
 * @brief Hand USART2 to the protocol transport: every later debugPrint/debugPrintln is
 *        dropped (and counted) instead of interleaving with protocol bytes (review F16).
 *
 * The composition root calls this once the init ladder has logged everything and before the
 * scheduler starts. The fault hooks keep printing through EarlyDebug directly: when the RTOS
 * is gone the bare-metal path is the one that still works.
 */
void retireDebugOutput();

/// Number of debugPrint/debugPrintln calls dropped since retireDebugOutput().
uint32_t debugPrintsDropped();

} // namespace Platform
