/**
 * @file velocity_quality.hpp
 * @brief Encoder velocity measurement quality signal — boundary contract
 *
 * Shared between encoder task (producer), speed trim controller (consumer),
 * telemetry (reporting), and IEncoder interface.
 */

#pragma once

#include <stdint.h>

enum class VelocityQuality : uint8_t {
    GOOD            = 0,
    LOW_CONFIDENCE  = 1,
    STALE           = 2,
    INVALID         = 3
};
