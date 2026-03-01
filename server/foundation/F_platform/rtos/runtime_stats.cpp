/**
 * @file runtime_stats.cpp
 * @brief DWT CYCCNT setup for FreeRTOS run-time statistics
 *
 * Uses the Cortex-M4 Data Watchpoint and Trace (DWT) cycle counter as a
 * free-running 32-bit timer at core clock frequency (84 MHz).
 * Wraps every ~51 seconds — safe for FreeRTOS delta-based accumulation.
 *
 * Hardware-only, independent of both FreeRTOS and SEGGER.
 */

#include "X_vendor/CMSIS/core_cm4.h"
#include <stdint.h>

extern "C" {

void vConfigureTimerForRunTimeStats(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t ulGetRunTimeCounterValue(void)
{
    return DWT->CYCCNT;
}

} // extern "C"
