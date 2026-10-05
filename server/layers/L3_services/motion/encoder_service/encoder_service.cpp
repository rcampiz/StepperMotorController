/**
 * @file encoder_service.cpp
 * @brief L3 EncoderService implementation (moved from F_platform/tasks/encoder_task.cpp)
 */

#include "L3_services/motion/encoder_service/encoder_service.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include "harness/pins/encoder_status.hpp"

namespace Services {

EncoderService g_encoderService;

bool EncoderService::init(Drivers::IEncoderHardware& hw,
                          Platform::ICriticalSection& critical,
                          Platform::IMicrosecondClock& usClock,
                          IEncoderProcessing& processor,
                          IEncoderStatusSink& statusSink,
                          uint8_t measWindowMs,
                          uint16_t sampleRateHz)
{
    m_hw = &hw;
    m_critical = &critical;
    m_usClock = &usClock;
    m_processor = &processor;
    m_statusSink = &statusSink;

    m_statusSink->setEncoderStatus(Harness::EncoderStatus::INITIALIZING);
    if (!m_hw->isReady()) {
        m_statusSink->setEncoderStatus(Harness::EncoderStatus::NOT_PRESENT);
        return false;
    }

    m_state.count = 0;
    m_state.velocity = 0;
    m_state.indexSeen = false;
    m_state.indexTick = 0;
    m_state.revolutions = 0;
    m_state.indexPeriodUs = 0;
    m_lastDmaIdx = 0;
    m_measWindowMs = (measWindowMs != 0) ? measWindowMs : DEFAULT_MEAS_WINDOW_MS;
    m_sampleRateHz = (sampleRateHz != 0) ? sampleRateHz : DEFAULT_SAMPLE_RATE_HZ;
    if (m_hw->sampleRateHz() != m_sampleRateHz) {
        m_hw->restartSampling(m_sampleRateHz);
    }

    m_statusSink->setEncoderStatus(Harness::EncoderStatus::READY);
    return true;
}

void EncoderService::sample()
{
    if (m_hw == nullptr || !m_hw->isReady()) {
        return;
    }

    const uint32_t bufSize = m_hw->sampleBufferSize();
    const uint32_t tickUs = m_usClock->getTickUs();

    // --- Feed every new DMA sample to the processor ---
    const uint32_t writeIdx = m_hw->sampleWriteIndex();
    const uint16_t* buf = m_hw->sampleBuffer();
    while (m_lastDmaIdx != writeIdx) {
        m_processor->feedSample(buf[m_lastDmaIdx], tickUs);
        m_lastDmaIdx = (m_lastDmaIdx + 1) % bufSize;
    }

    // --- Raw velocity over the measurement window ---
    uint32_t windowSamples = static_cast<uint32_t>(m_measWindowMs) * m_sampleRateHz / 1000;
    if (windowSamples < 1) windowSamples = 1;
    if (windowSamples > bufSize - 1) windowSamples = bufSize - 1;

    int32_t rawVelocity = 0;
    {
        uint32_t latestIdx = (writeIdx - 1 + bufSize) % bufSize;
        uint32_t oldIdx = (writeIdx - 1 - windowSamples + bufSize) % bufSize;
        auto delta = static_cast<int16_t>(buf[latestIdx] - buf[oldIdx]);
        rawVelocity = static_cast<int32_t>(delta) / static_cast<int32_t>(windowSamples);
    }
    m_lastRawVelocity = rawVelocity;

    int32_t filtered = m_processor->filter(rawVelocity, tickUs);

    bool indexSeen = m_hw->isIndexSeen();
    uint32_t indexTick = m_hw->getIndexTick();
    int32_t revolutions = m_hw->getRevolutions();
    uint32_t indexPeriodUs = m_hw->getIndexPeriodUs();

    // --- Shared snapshot (read by the Comms task through getState) ---
    m_critical->enter();
    m_state.count = m_processor->getPosition();
    m_state.velocity = filtered;
    m_state.indexSeen = indexSeen;
    m_state.indexTick = indexTick;
    m_state.revolutions = revolutions;
    m_state.indexPeriodUs = indexPeriodUs;
    m_state.velocityQuality = m_processor->getQuality();
    m_critical->exit();

    // --- Telemetry ---
    EncoderTelemetryData telem = {};
    telem.count = m_processor->getPosition();
    telem.velocity = filtered;
    telem.indexSeen = indexSeen;
    telem.indexTick = indexTick;
    telem.revolutions = revolutions;
    telem.indexPeriodUs = indexPeriodUs;
    telem.velocityQuality = static_cast<uint8_t>(m_processor->getQuality());
    telem.filterFlags = m_processor->getFilterFlags();
    telem.measWindowMs = m_measWindowMs;
    telem.sampleRateHz = m_sampleRateHz;
    Services::g_telemetryStore.writeEncoder(telem);
}

EncoderSnapshot EncoderService::getState()
{
    EncoderSnapshot state;
    m_critical->enter();
    state.count = m_state.count;
    state.velocity = m_state.velocity;
    state.indexSeen = m_state.indexSeen;
    state.indexTick = m_state.indexTick;
    state.revolutions = m_state.revolutions;
    state.indexPeriodUs = m_state.indexPeriodUs;
    state.velocityQuality = m_state.velocityQuality;
    m_critical->exit();
    return state;
}

int32_t EncoderService::getCount()
{
    return (m_hw != nullptr && m_hw->isReady()) ? m_hw->getCount() : 0;
}

void EncoderService::resetCount()
{
    if (m_hw != nullptr) {
        m_hw->reset();
    }
    m_critical->enter();
    m_state.count = 0;
    m_processor->reset();
    m_critical->exit();
}

void EncoderService::clearIndexFlag()
{
    if (m_hw != nullptr) {
        m_hw->clearIndexFlag();
    }
    m_critical->enter();
    m_state.indexSeen = false;
    m_critical->exit();
}

bool EncoderService::isAvailable()
{
    return m_hw != nullptr && m_hw->isReady();
}

void EncoderService::getConfig(IEncoderDispatcher::EncFilterParams& out)
{
    m_processor->getFilterConfig(out);
    out.measWindowMs = m_measWindowMs;
    out.sampleRateHz = m_sampleRateHz;
}

void EncoderService::setConfig(const IEncoderDispatcher::EncFilterParams& cfg)
{
    m_processor->setFilterConfig(
        cfg.filterFlags, cfg.emaAlpha, cfg.smaWindow,
        cfg.padeGainPct, cfg.padeMaxCorr,
        cfg.biquadCutoffHz, cfg.notchCenterHz, cfg.notchQ10,
        cfg.holtAlpha, cfg.holtBeta);
    m_measWindowMs = (cfg.measWindowMs != 0) ? cfg.measWindowMs : DEFAULT_MEAS_WINDOW_MS;
    uint16_t rate = (cfg.sampleRateHz != 0) ? cfg.sampleRateHz : DEFAULT_SAMPLE_RATE_HZ;
    if (rate != m_sampleRateHz) {
        setSampleRate(rate);
    }
}

void EncoderService::setLegacy(uint8_t type, uint8_t param)
{
    m_processor->setFilter(type, param);
}

void EncoderService::setSampleRate(uint16_t hz)
{
    if (hz < 100) hz = 100;
    if (hz > 10000) hz = 10000;
    m_hw->restartSampling(hz);
    m_lastDmaIdx = 0;
    m_sampleRateHz = hz;
}

} // namespace Services
