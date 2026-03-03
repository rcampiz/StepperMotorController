/**
 * @file velocity_quality.hpp
 * @brief Encoder velocity measurement quality signal
 *
 * Shared between encoder task (producer), speed trim controller (consumer),
 * telemetry (reporting), and IEncoder interface.
 */

#pragma once

#include <stdint.h>

enum class VelocityQuality : uint8_t {
    GOOD            = 0,  // Sufficient edges, filter populated
    LOW_CONFIDENCE  = 1,  // Fewer than 2 samples in window
    STALE           = 2,  // No encoder edges for > 200ms
    INVALID         = 3   // Encoder fault or not initialized
};
