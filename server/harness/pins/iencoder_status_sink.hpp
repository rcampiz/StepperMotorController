/**
 * @file iencoder_status_sink.hpp
 * @brief Interface for reporting encoder hardware status and telemetry
 *
 * Outbound harness interface: encoder_task reports status and telemetry
 * through this to L3 services. Keeps task decoupled from L2 Protocol layer.
 */

#pragma once

#include "harness/pins/encoder_status.hpp"
#include <stdint.h>

namespace Harness {

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
    virtual void setEncoderStatus(EncoderStatus status) = 0;
};

} // namespace Harness
