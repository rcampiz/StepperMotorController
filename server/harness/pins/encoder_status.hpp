/**
 * @file encoder_status.hpp
 * @brief Standalone encoder hardware status enum
 *
 * Single source of truth for encoder status reporting.
 * Used by encoder_task (producer) and ControlModeManager (consumer).
 */

#pragma once

#include <stdint.h>

namespace Harness {

/**
 * @brief Encoder hardware status
 */
enum class EncoderStatus : uint8_t {
    NOT_PRESENT  = 0,  // Encoder not detected or init failed
    INITIALIZING = 1,  // Encoder init in progress
    READY        = 2,  // Encoder operational
    FAULT        = 3   // Encoder error detected
};

} // namespace Harness
