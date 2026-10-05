/**
 * @file iencoder_service.hpp
 * @brief Services:: provider — Encoder task entry point (H_TASKS_L3)
 *
 * The Encoder task only waits (10 ms period) and calls sample(). Everything else
 * (DMA ring scan, velocity window, filters, snapshot, telemetry) is inside L3.
 */
#pragma once

namespace Services {

class IEncoderService {
public:
    virtual ~IEncoderService() = default;

    /// One sampling step. Encoder task context, every ENCODER_SAMPLE_PERIOD_MS.
    virtual void sample() = 0;
};

} // namespace Services
