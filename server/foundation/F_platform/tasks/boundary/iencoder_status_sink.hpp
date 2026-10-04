/**
 * @file boundary/iencoderstatussink.hpp
 * @brief Consumer port — Scheduler side of the Services::IEncoderStatusSink harness connection
 *
 * Structurally identical to Services::IEncoderStatusSink.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

#include "harness/pins/encoder_status.hpp"
#include <stdint.h>

namespace Scheduler {

/** @brief Encoder telemetry data (decouples task from Protocol::EncoderTelemetry) */
struct EncoderTelemetryData {
    int64_t count = 0;
    int32_t velocity = 0;
    bool indexSeen = false;
    uint32_t indexTick = 0;
    int32_t revolutions = 0;
    uint32_t indexPeriodUs = 0;
    uint8_t velocityQuality = 0;
    uint16_t filterFlags = 0;
    uint8_t measWindowMs = 0;
    uint16_t sampleRateHz = 0;
};

class IEncoderStatusSink {
public:
    virtual ~IEncoderStatusSink() = default;

    /** @brief Report encoder hardware status change */
    virtual void setEncoderStatus(Harness::EncoderStatus status) = 0;
};

} // namespace Scheduler
