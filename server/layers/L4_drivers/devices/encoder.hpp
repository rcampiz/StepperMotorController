/**
 * @file encoder.hpp
 * @brief Quadrature encoder driver — L4 wrapper over the L5 counter, sampler and index EXTI
 *
 * Owns: the TIM4 count (via Board::ITimerCounter), the TIM3+DMA sample ring
 * (via Board::IEncoderSampler) and the index-pulse ISR registration (H_L4_L5 registerExti).
 * Timestamps come from the Platform clocks (power net, ISR-safe calls only).
 * Index fields live in an ISR-private working copy and are published through a
 * sequence-counter snapshot, so readIndex() in task context is never torn.
 */
#pragma once

#include "harness/defs/harness_l4_l5.hpp"
#include "harness/pins/seq_snapshot.hpp"
#include "L4_drivers/boundary/iencoder_hardware.hpp"
#include "F_platform/boundary/iclock.hpp"
#include "F_platform/boundary/imicrosecond_clock.hpp"
#include <stdint.h>

namespace Drivers {

class Encoder : public Drivers::IEncoderHardware {
public:
    enum class Status : uint8_t { NOT_INITIALIZED = 0, INITIALIZING = 1, READY = 2, FAULT = 3 };

    Encoder(const Harness::EncoderPins& pins,
            Platform::IClock& clock,
            Platform::IMicrosecondClock& usClock,
            void (*registerExti)(uint8_t line, Harness::ExtiCallback cb));

    /// Register the index ISR, prefill the DMA ring and start sampling at sampleRateHz.
    bool init(uint16_t sampleRateHz);

    Status getStatus() const { return m_status; }

    // IEncoderHardware
    bool isReady() const override { return m_status == Status::READY; }
    int32_t getCount() const override {
        return static_cast<int32_t>(static_cast<int16_t>(m_timer.getCount()));
    }
    void reset() override { m_timer.setCount(0); }

    IndexSnapshot readIndex() const override { return m_index.read(); }
    void indexISR(uint32_t tickMs, uint32_t tickUs) override;

    const uint16_t* sampleBuffer() const override { return m_sampler.getBuffer(); }
    uint32_t sampleBufferSize() const override { return m_sampler.getBufferSize(); }
    uint32_t sampleWriteIndex() const override { return m_sampler.getWriteIndex(); }
    uint16_t sampleRateHz() const override { return m_sampler.getSampleRateHz(); }
    void restartSampling(uint16_t hz) override;

private:
    static void indexIsrTrampoline();   ///< L5 EXTI callback (no context argument)
    static Encoder* s_isrInstance;

    Board::ITimerCounter&        m_timer;
    Board::IEncoderSampler&      m_sampler;
    Platform::IClock&            m_clock;
    Platform::IMicrosecondClock& m_usClock;
    void (*m_registerExti)(uint8_t, Harness::ExtiCallback);
    uint8_t                      m_extiLine;

    Status m_status = Status::NOT_INITIALIZED;

    // ISR-private working state (only indexISR and init touch these)
    IndexSnapshot m_isrIndex{};
    uint32_t      m_lastIndexUs = 0;
    // Published copy: ISR writes, task reads (seqlock)
    Harness::SeqSnapshot<IndexSnapshot> m_index;
};

} // namespace Drivers
