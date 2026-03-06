/**
 * @file encoder_sampler.hpp
 * @brief DMA-based encoder sampling hardware (TIM3 + DMA1_Stream2)
 *
 * Configures TIM3 as a periodic trigger and DMA1_Stream2 to sample
 * TIM4->CNT into a circular buffer. Implements IEncoderSampler.
 *
 * All CMSIS register access lives here in L5.
 */

#pragma once

#include "harness/pins/iencoder_sampler.hpp"

namespace Board {

class EncoderSampler : public Harness::IEncoderSampler {
public:
    static constexpr uint32_t BUF_SIZE = 256;

    /**
     * @brief Construct with pointer to the timer CNT register to sample
     * @param timerCntReg Address of TIMx->CNT (e.g., &TIM4->CNT)
     */
    explicit EncoderSampler(volatile uint32_t* timerCntReg);

    void start(uint16_t sampleRateHz) override;
    void stop() override;
    void setSampleRate(uint16_t hz) override;
    uint16_t getSampleRateHz() const override { return m_sampleRateHz; }
    uint32_t getWriteIndex() const override;
    const uint16_t* getBuffer() const override { return m_dmaBuf; }
    uint32_t getBufferSize() const override { return BUF_SIZE; }
    void prefillBuffer(uint16_t value) override;

private:
    volatile uint32_t* m_timerCntReg;
    uint16_t m_sampleRateHz = 1000;
    uint16_t m_dmaBuf[BUF_SIZE] __attribute__((aligned(4)));
};

} // namespace Board
