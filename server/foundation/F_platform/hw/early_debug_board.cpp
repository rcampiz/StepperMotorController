/**
 * @file early_debug_board.cpp
 * @brief Implements Platform::initDebugOutput/debugPrint/debugPrintln
 *
 * Wraps the inline EarlyDebug functions for use through the init contract (harness/pins/early_debug_board.hpp).
 */

#include "F_platform/hw/early_debug_board.hpp"
#include "F_platform/hw/early_debug.hpp"

namespace Platform {

void initDebugOutput()
{
    EarlyDebug::initUART();
}

void debugPrint(const char* str)
{
    EarlyDebug::print(str);
}

void debugPrintln(const char* str)
{
    EarlyDebug::println(str);
}

} // namespace Platform
