/**
 * @file encoder.cpp
 * @brief Quadrature encoder driver implementation
 */

#include "L4_drivers/devices/encoder.hpp"

namespace Drivers {

Encoder* Encoder::s_isrInstance = nullptr;

Encoder::Encoder(const Harness::EncoderPins& pins,
                 Platform::IClock& clock,
                 Platform::IMicrosecondClock& usClock,
                 void (*registerExti)(uint8_t line, Harness::ExtiCallback cb))
    : m_timer(*pins.timer),
      m_sampler(*pins.sampler),
      m_clock(clock),
      m_usClock(usClock),
      m_registerExti(registerExti),
      m_extiLine(pins.indexExtiLine)
{
}

bool Encoder::init(uint16_t sampleRateHz)
{
    m_status = Status::INITIALIZING;
    m_indexSeen = false;
    m_indexTick = 0;
    m_revolutions = 0;
    m_lastIndexUs = 0;
    m_indexPeriodUs = 0;

    // Index pulse: L5 EXTI -> trampoline -> indexISR (ISR context, NVIC 5)
    s_isrInstance = this;
    if (m_registerExti != nullptr && m_extiLine < 16) {
        m_registerExti(m_extiLine, &Encoder::indexIsrTrampoline);
    }

    // Sample ring: prefill with the current count so the first window is flat
    m_sampler.prefillBuffer(m_timer.getCount());
    m_sampler.start(sampleRateHz != 0 ? sampleRateHz : 1000);

    m_status = Status::READY;
    return true;
}

void Encoder::indexISR(uint32_t tickMs, uint32_t tickUs)
{
    m_indexSeen = true;
    m_indexTick = tickMs;
    if (m_timer.isCountingDown()) {
        m_revolutions--;
    } else {
        m_revolutions++;
    }
    if (m_lastIndexUs != 0) {
        m_indexPeriodUs = tickUs - m_lastIndexUs;
    }
    m_lastIndexUs = tickUs;
}

void Encoder::indexIsrTrampoline()
{
    Encoder* self = s_isrInstance;
    if (self != nullptr) {
        self->indexISR(self->m_clock.getTickMsFromISR(), self->m_usClock.getTickUs());
    }
}

void Encoder::restartSampling(uint16_t hz)
{
    if (hz < 100) hz = 100;
    if (hz > 10000) hz = 10000;
    m_sampler.stop();
    m_sampler.prefillBuffer(m_timer.getCount());
    m_sampler.start(hz);
}

} // namespace Drivers
