/**
 * @file timing_service.hpp
 * @brief Transport delay compensation
 *
 * Pure data service — no RTOS, no hardware, no heap.
 * Stores the configured transport delay for latency compensation.
 *
 * Usage:
 *   - Client measures RTT via heartbeat, sends SYST:DELAY <RTT/2>
 *   - Sync service reads delay to adjust START timing
 *   - Control service reads delay for setpoint projection (v2)
 */

#pragma once
#include <stdint.h>

namespace Services::Timing {

void     setTransportDelay(uint32_t ms);
uint32_t getTransportDelay();

} // namespace Services::Timing
