/**
 * @file iencoder_sampler.hpp
 * @brief Abstract interface for DMA-based encoder sampling
 *
 * Decouples encoder task from CMSIS DMA/timer registers.
 * Implemented by Board::EncoderSampler (TIM2 trigger, DMA1_Stream1 + DMA1_Stream5).
 */

#pragma once

#include <stdint.h>

namespace Board {

class IEncoderSampler {
public:
    virtual ~IEncoderSampler() = default;

    /** @brief Start DMA sampling at given rate */
    virtual void start(uint16_t sampleRateHz) = 0;

    /** @brief Stop DMA sampling */
    virtual void stop() = 0;

    /** @brief Change sample rate (stops + restarts DMA) */
    virtual void setSampleRate(uint16_t hz) = 0;

    /** @brief Get current sample rate */
    virtual uint16_t getSampleRateHz() const = 0;

    /** @brief Get current DMA write index into the buffer */
    virtual uint32_t getWriteIndex() const = 0;

    /** @brief Get pointer to the circular sample buffer */
    virtual const uint16_t* getBuffer() const = 0;

    /** @brief Second ring sampled at the same instants: the driver's SYNC step-pulse count
     *         (powerSTEP01 BUSY/SYNC on PB5 counted by TIM3); nullptr when not fitted */
    virtual const uint16_t* getSyncBuffer() const { return nullptr; }

    /** @brief Get buffer size (number of entries) */
    virtual uint32_t getBufferSize() const = 0;

    /** @brief Pre-fill buffer with a value (avoids false deltas on startup) */
    virtual void prefillBuffer(uint16_t value) = 0;
};

} // namespace Board
