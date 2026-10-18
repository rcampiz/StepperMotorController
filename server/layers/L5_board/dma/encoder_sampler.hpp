/**
 * @file encoder_sampler.hpp
 * @brief DMA-based position sampling hardware (TIM2 trigger, DMA1_Stream1 + DMA1_Stream5)
 *
 * TIM2 triggers two DMA streams at the sample rate: TIM4->CNT (encoder) into one ring and
 * TIM3->CNT (powerSTEP01 SYNC pulse counter on PB5) into a second ring at the same instants.
 * Implements IEncoderSampler. All CMSIS register access lives here in L5.
 *
 * The rings are 4096 entries: 0.4 s at 10 kHz, 4 s at 1 kHz, enough for a capture of one
 * cruise segment (CTRL:CAP:*) while the Encoder service keeps reading the newest entries.
 */

#pragma once

#include "L5_board/boundary/iencoder_sampler.hpp"

namespace Board {

class EncoderSampler : public Board::IEncoderSampler {
public:
    static constexpr uint32_t BUF_SIZE = 4096;

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
    const uint16_t* getSyncBuffer() const override { return m_syncBuf; }
    uint32_t getBufferSize() const override { return BUF_SIZE; }
    void prefillBuffer(uint16_t value) override;

private:
    void initSyncCounter();

    volatile uint32_t* m_timerCntReg;
    uint16_t m_sampleRateHz = 1000;
    bool m_syncReady = false;
    uint16_t m_dmaBuf[BUF_SIZE] __attribute__((aligned(4)));
    uint16_t m_syncBuf[BUF_SIZE] __attribute__((aligned(4)));
};

} // namespace Board
