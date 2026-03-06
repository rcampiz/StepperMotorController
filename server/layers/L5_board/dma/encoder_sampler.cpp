/**
 * @file encoder_sampler.cpp
 * @brief DMA-based encoder sampling — TIM3 trigger + DMA1_Stream2
 *
 * All CMSIS register access for DMA/TIM3 is encapsulated here in L5.
 */

#include "L5_board/dma/encoder_sampler.hpp"
#include "X_vendor/CMSIS/stm32f401xe.h"

namespace Board {

EncoderSampler::EncoderSampler(volatile uint32_t* timerCntReg)
    : m_timerCntReg(timerCntReg)
{
    for (uint32_t i = 0; i < BUF_SIZE; i++) {
        m_dmaBuf[i] = 0;
    }
}

void EncoderSampler::start(uint16_t sampleRateHz)
{
    if (sampleRateHz < 100) sampleRateHz = 100;
    if (sampleRateHz > 10000) sampleRateHz = 10000;
    m_sampleRateHz = sampleRateHz;

    // --- TIM3: periodic trigger for DMA ---
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    volatile uint32_t dummy = RCC->APB1ENR;
    (void)dummy;

    TIM3->CR1 = 0;                // Stop during config
    TIM3->PSC = 83;               // 84 MHz APB1 timer / 84 = 1 MHz tick
    TIM3->ARR = (1000000U / sampleRateHz) - 1;
    TIM3->DIER = TIM_DIER_UDE;   // DMA request on update event
    TIM3->EGR = TIM_EGR_UG;      // Load prescaler + ARR
    TIM3->SR = 0;                 // Clear any pending flags

    // --- DMA1 Stream 2, Channel 5 (TIM3_UP) ---
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    dummy = RCC->AHB1ENR;
    (void)dummy;

    // Disable stream before reconfiguring
    DMA1_Stream2->CR = 0;
    while (DMA1_Stream2->CR & DMA_SxCR_EN) {}

    // Clear all interrupt flags for stream 2 (LIFCR bits 16-21)
    DMA1->LIFCR = (0x3FU << 16);

    // Configure: Channel 5, P->M, 16-bit, circular, memory increment
    DMA1_Stream2->PAR  = reinterpret_cast<uint32_t>(m_timerCntReg);
    DMA1_Stream2->M0AR = reinterpret_cast<uint32_t>(m_dmaBuf);
    DMA1_Stream2->NDTR = BUF_SIZE;
    DMA1_Stream2->FCR  = 0;  // Direct mode (no FIFO)
    DMA1_Stream2->CR   = (5U << DMA_SxCR_CHSEL_Pos)   // Channel 5 (TIM3_UP)
                        | DMA_SxCR_MSIZE_0              // 16-bit memory
                        | DMA_SxCR_PSIZE_0              // 16-bit peripheral
                        | DMA_SxCR_MINC                 // Memory address increment
                        | DMA_SxCR_CIRC                 // Circular mode
                        | DMA_SxCR_PL_1;                // Priority: High

    // Enable DMA stream
    DMA1_Stream2->CR |= DMA_SxCR_EN;

    // Start TIM3
    TIM3->CR1 = TIM_CR1_CEN;
}

void EncoderSampler::stop()
{
    TIM3->CR1 = 0;           // Stop TIM3
    TIM3->DIER = 0;          // Disable DMA requests
    DMA1_Stream2->CR = 0;    // Disable DMA stream
    while (DMA1_Stream2->CR & DMA_SxCR_EN) {}
}

void EncoderSampler::setSampleRate(uint16_t hz)
{
    stop();
    start(hz);
}

uint32_t EncoderSampler::getWriteIndex() const
{
    return BUF_SIZE - DMA1_Stream2->NDTR;
}

void EncoderSampler::prefillBuffer(uint16_t value)
{
    for (uint32_t i = 0; i < BUF_SIZE; i++) {
        m_dmaBuf[i] = value;
    }
}

} // namespace Board
