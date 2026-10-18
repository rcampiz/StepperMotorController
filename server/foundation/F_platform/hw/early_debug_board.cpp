/**
 * @file early_debug_board.cpp
 * @brief Implements Platform::initDebugOutput/debugPrint/debugPrintln
 *
 * Wraps the inline EarlyDebug functions for use through the init contract (harness/pins/early_debug_board.hpp).
 */

#include "F_platform/hw/early_debug_board.hpp"
#include "F_platform/hw/early_debug.hpp"

namespace Platform {

namespace {
volatile bool     s_retired = false;      // USART2 handed to the protocol transport
volatile uint32_t s_dropped = 0;          // prints refused since then (read with a debugger)
}

void initDebugOutput()
{
    EarlyDebug::initUART();
}

void debugPrint(const char* str)
{
    if (s_retired) { ++s_dropped; return; }
    EarlyDebug::print(str);
}

void debugPrintln(const char* str)
{
    if (s_retired) { ++s_dropped; return; }
    EarlyDebug::println(str);
}

void retireDebugOutput()
{
    s_retired = true;
}

uint32_t debugPrintsDropped()
{
    return s_dropped;
}

} // namespace Platform
