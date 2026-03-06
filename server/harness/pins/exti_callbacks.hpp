/**
 * @file exti_callbacks.hpp
 * @brief EXTI interrupt callback registration — boundary contract
 *
 * Allows Scheduler/L3 code to register callbacks for EXTI lines
 * without including CMSIS. ISR handlers live in L5 and call through
 * these registered callbacks.
 */

#pragma once

#include <stdint.h>

namespace Harness {

using ExtiCallback = void(*)();

/** Register a callback for an EXTI line (0-15) */
void setExtiCallback(uint8_t line, ExtiCallback cb);

/** Get the registered callback for an EXTI line. Returns nullptr if none. */
ExtiCallback getExtiCallback(uint8_t line);

} // namespace Harness
