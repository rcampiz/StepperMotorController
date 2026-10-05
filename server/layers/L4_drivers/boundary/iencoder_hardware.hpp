/**
 * @file iencoder_hardware.hpp
 * @brief Drivers:: provider interface — quadrature encoder hardware (H_L3_L4)
 *
 * One L4 object wraps the L5 TIM4 counter, the TIM3+DMA sampler and the index-pulse
 * EXTI line. L3 (EncoderService) reads the DMA ring through the sample*() accessors and
 * keeps its own read index; L5 owns the ring. indexISR() is ISR context (NVIC 5), reached
 * from the L5 EXTI callback the driver registered — never from L3.
 *
 * Index fields are published as one IndexSnapshot through a sequence-counter snapshot
 * (readIndex): the ISR is the only writer, the task reader never sees torn values. "Index
 * seen" is derived by the consumer from the monotonic pulse count (ack = remember the count).
 */
#pragma once
#include <stdint.h>

namespace Drivers {

struct IndexSnapshot {
    uint32_t pulses = 0;        ///< index pulses since init (monotonic)
    uint32_t tickMs = 0;        ///< Platform tick (ms) at the last pulse
    int32_t  revolutions = 0;   ///< signed revolution count (+ counting up, - counting down)
    uint32_t periodUs = 0;      ///< microseconds between the last two pulses (0 until two seen)
};

class IEncoderHardware {
public:
    virtual ~IEncoderHardware() = default;

    // --- counter ---
    virtual bool isReady() const = 0;
    virtual int32_t getCount() const = 0;
    virtual void reset() = 0;

    // --- index pulse (written in ISR, read in task context) ---
    /// Consistent copy of the index fields (never torn by a concurrent indexISR).
    virtual IndexSnapshot readIndex() const = 0;
    /// ISR entry (L5 EXTI callback). tickMs from Platform::IClock::getTickMsFromISR.
    virtual void indexISR(uint32_t tickMs, uint32_t tickUs) = 0;

    // --- DMA sample ring (hardware-written; L5 owns the buffer) ---
    virtual const uint16_t* sampleBuffer() const = 0;
    virtual uint32_t sampleBufferSize() const = 0;
    virtual uint32_t sampleWriteIndex() const = 0;
    virtual uint16_t sampleRateHz() const = 0;
    /// Stop, prefill with the current count, restart at hz (clamped 100..10000). Task context.
    virtual void restartSampling(uint16_t hz) = 0;
};

} // namespace Drivers
