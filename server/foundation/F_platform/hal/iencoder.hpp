/**
 * @file iencoder.hpp
 * @brief Interface for encoder hardware abstraction
 *
 * Foundation interface for encoder hardware abstraction.
 * Implemented by EncoderAdapter in wiring/. Enables mock
 * injection for unit testing without real encoder hardware.
 */

#pragma once

#include "F_platform/types/velocity_quality_t.hpp"
#include <stdint.h>

namespace Services {

/**
 * @brief Encoder state snapshot (thread-safe copy)
 */
struct EncoderSnapshot {
    int64_t  count;            // Accumulated encoder count (64-bit, no wrap)
    int32_t  velocity;         // Calculated velocity (counts/sec)
    bool     indexSeen;        // Index pulse seen since last clear
    uint32_t indexTick;        // Tick when index was last seen
    int32_t  revolutions;      // Revolution count from index pulses (signed)
    uint32_t indexPeriodUs;    // Microseconds between last two index pulses
    VelocityQuality velocityQuality;  // Measurement confidence
};

/**
 * @brief Abstract encoder interface
 */
class IEncoder {
public:
    virtual ~IEncoder() = default;

    /** @brief Get full encoder state snapshot (thread-safe) */
    virtual EncoderSnapshot getState() = 0;

    /** @brief Get raw encoder count (fast, direct register read) */
    virtual int32_t getCount() = 0;

    /** @brief Reset encoder count to zero */
    virtual void resetCount() = 0;

    /** @brief Clear index-seen flag */
    virtual void clearIndexFlag() = 0;

    /** @brief Check if encoder hardware is available */
    virtual bool isAvailable() = 0;
};

} // namespace Services
