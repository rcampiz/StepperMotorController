/**
 * @file encoder_sampler.cpp
 * @brief DMA position sampling: TIM2 trigger, DMA1_Stream1 (TIM4->CNT, encoder) and
 *        DMA1_Stream5 (TIM3->CNT, powerSTEP01 SYNC pulse counter on PB5)
 *
 * TIM2 fires the DMA requests at the sample rate (both streams on TIM2_UP, channel 3), so
 * the encoder count and the commanded-step count land in their rings at the same instant.
 * TIM3 runs in external-clock mode 1 from TI2 (PB5, AF2): it counts the SYNC pulses the
 * chip emits per step (SYNC_SEL, 8 per full step as the Motor task configures STEP_MODE).
 * Until 2026-10-18 TIM3 was the trigger and there was no second ring.
 *
 * All CMSIS register access for DMA/TIM2/TIM3 is encapsulated here in L5.
 */

#include "L5_board/dma/encoder_sampler.hpp"
#include "X_vendor/CMSIS/stm32f401xe.h"

// The project's trimmed CMSIS header defines only the DMA streams it used so far; streams 1
// and 5 (TIM2_UP and TIM2_CH1, channel 3) follow the same 0x18-byte stride from DMA1_BASE + 0x10.
#ifndef DMA1_Stream1
#define DMA1_Stream1 ((DMA_Stream_TypeDef *) (DMA1_BASE + 0x28UL))
#endif
#ifndef DMA1_Stream5
#define DMA1_Stream5 ((DMA_Stream_TypeDef *) (DMA1_BASE + 0x88UL))
#endif

namespace Board {

EncoderSampler::EncoderSampler(volatile uint32_t* timerCntReg)
    : m_timerCntReg(timerCntReg)
{
    for (uint32_t i = 0; i < BUF_SIZE; i++) {
        m_dmaBuf[i] = 0;
        m_syncBuf[i] = 0;
    }
}

void EncoderSampler::initSyncCounter()
{
    // TIM3 as a 16-bit pulse counter clocked by TI2 (PB5, powerSTEP01 BUSY/SYNC)
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    volatile uint32_t dummy = RCC->APB1ENR;
    (void)dummy;
    TIM3->CR1 = 0;
    TIM3->DIER = 0;
    TIM3->CCMR1 = (TIM3->CCMR1 & ~TIM_CCMR1_CC2S) | TIM_CCMR1_CC2S_0;   // CC2 = TI2 input, no filter
    TIM3->CCER &= ~((1UL << 5) | (1UL << 7));                            // CC2P/CC2NP clear: rising edges
    TIM3->SMCR = (6U << 4) | (7U << TIM_SMCR_SMS_Pos);                   // TS = TI2FP2 (110), SMS = external clock mode 1
    TIM3->PSC = 0;
    TIM3->ARR = 0xFFFF;
    TIM3->CNT = 0;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0;
    TIM3->CR1 = TIM_CR1_CEN;
    m_syncReady = true;
}

void EncoderSampler::start(uint16_t sampleRateHz)
{
    if (sampleRateHz < 100) sampleRateHz = 100;
    if (sampleRateHz > 10000) sampleRateHz = 10000;
    m_sampleRateHz = sampleRateHz;
    if (!m_syncReady) {
        initSyncCounter();
    }

    // --- TIM2: periodic trigger for both DMA streams ---
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    volatile uint32_t dummy = RCC->APB1ENR;
    (void)dummy;

    TIM2->CR1 = 0;                // Stop during config
    TIM2->PSC = 83;               // 84 MHz APB1 timer / 84 = 1 MHz tick
    TIM2->ARR = (1000000U / sampleRateHz) - 1;
    TIM2->CCR1 = 1;              // compare 1 fires once per period too: stream 5's own request
    TIM2->DIER = TIM_DIER_UDE | (1UL << 9);        // UDE: update -> stream 1; CC1DE (bit 9): CC1 -> stream 5
    TIM2->EGR = TIM_EGR_UG;      // Load prescaler + ARR
    TIM2->SR = 0;

    // --- DMA1 Stream 1 (encoder, TIM2_UP) and Stream 5 (sync, TIM2_CH1), channel 3 ---
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    dummy = RCC->AHB1ENR;
    (void)dummy;

    DMA1_Stream1->CR = 0;
    DMA1_Stream5->CR = 0;
    for (int guard = 0; ((DMA1_Stream1->CR | DMA1_Stream5->CR) & DMA_SxCR_EN) && guard < 100000; ++guard) {}
    DMA1->LIFCR = (0x3FU << 6);    // stream 1 flags
    DMA1->HIFCR = (0x3FU << 6);    // stream 5 flags

    const uint32_t cr = (3U << DMA_SxCR_CHSEL_Pos)    // Channel 3 (TIM2_UP)
                      | DMA_SxCR_MSIZE_0              // 16-bit memory
                      | DMA_SxCR_PSIZE_0              // 16-bit peripheral
                      | DMA_SxCR_MINC                 // Memory address increment
                      | DMA_SxCR_CIRC                 // Circular mode
                      | DMA_SxCR_PL_1;                // Priority: High

    DMA1_Stream1->PAR  = reinterpret_cast<uint32_t>(m_timerCntReg);
    DMA1_Stream1->M0AR = reinterpret_cast<uint32_t>(m_dmaBuf);
    DMA1_Stream1->NDTR = BUF_SIZE;
    DMA1_Stream1->FCR  = 0;
    DMA1_Stream1->CR   = cr;

    DMA1_Stream5->PAR  = reinterpret_cast<uint32_t>(&TIM3->CNT);
    DMA1_Stream5->M0AR = reinterpret_cast<uint32_t>(m_syncBuf);
    DMA1_Stream5->NDTR = BUF_SIZE;
    DMA1_Stream5->FCR  = 0;
    DMA1_Stream5->CR   = cr;

    DMA1_Stream1->CR |= DMA_SxCR_EN;
    DMA1_Stream5->CR |= DMA_SxCR_EN;

    TIM2->CR1 = TIM_CR1_CEN;
}

void EncoderSampler::stop()
{
    TIM2->CR1 = 0;
    TIM2->DIER = 0;
    DMA1_Stream1->CR &= ~DMA_SxCR_EN;
    DMA1_Stream5->CR &= ~DMA_SxCR_EN;
    // The EN bit clears when the stream's current transfer ends; bound the wait so a
    // misbehaving stream can never hang the init ladder
    int guard = 0;
    while (((DMA1_Stream1->CR | DMA1_Stream5->CR) & DMA_SxCR_EN) && ++guard < 100000) {}
    DMA1_Stream1->CR = 0;
    DMA1_Stream5->CR = 0;
}

void EncoderSampler::setSampleRate(uint16_t hz)
{
    stop();
    start(hz);
}

uint32_t EncoderSampler::getWriteIndex() const
{
    // Both streams share the trigger and start together; stream 1's NDTR is the index
    return (BUF_SIZE - DMA1_Stream1->NDTR) % BUF_SIZE;
}

void EncoderSampler::prefillBuffer(uint16_t value)
{
    const uint16_t sync = m_syncReady ? static_cast<uint16_t>(TIM3->CNT) : 0;
    for (uint32_t i = 0; i < BUF_SIZE; i++) {
        m_dmaBuf[i] = value;
        m_syncBuf[i] = sync;
    }
}

} // namespace Board
