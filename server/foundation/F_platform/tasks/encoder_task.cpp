/**
 * @file encoder_task.cpp
 * @brief Encoder reading task — DMA sampling + hardware management
 *
 * DMA Architecture:
 *   TIM3 fires at configurable rate (default 1kHz) → triggers DMA1_Stream2
 *   → reads TIM4->CNT (16-bit) into circular RAM buffer (256 entries)
 *   → encoder task wakes every 10ms, feeds samples to EncoderProcessor,
 *     computes raw velocity from DMA window, and delegates filtering.
 *
 * Business logic (position unwrapping, filter chain, quality assessment)
 * lives in Services::EncoderProcessor (L3 service).
 *
 * All hardware access goes through injected interfaces:
 *   IEncoderHardware  — encoder driver (index, count, reset)
 *   IEncoderSampler   — DMA sampling (TIM3 + DMA1_Stream2)
 *   ICriticalSection  — thread-safe state access
 *   IClock            — timing and delays
 *
 * Encoder is OPTIONAL — system operates in OPEN_LOOP if init fails.
 */

#include "F_platform/tasks/encoder_task.hpp"
#include "harness/pins/iencoder_hardware.hpp"
#include "harness/pins/iencoder_sampler.hpp"
#include "harness/pins/icritical_section.hpp"
#include "harness/pins/itimer_counter.hpp"
#include "harness/pins/iclock.hpp"
#include "harness/pins/iencoder.hpp"
#include "harness/pins/iencoder_filter_control.hpp"
#include "harness/pins/iencoder_processing.hpp"
#include "harness/pins/iencoder_status_sink.hpp"
#include "harness/pins/isnapshot_writer.hpp"
#include "harness/pins/imicrosecond_clock.hpp"
#include "harness/pins/itrace_context.hpp"
#include "harness/pins/exti_callbacks.hpp"

namespace Scheduler {

// Injected dependencies (set by EncoderTask_Init)
static Harness::IEncoderHardware* s_encoder = nullptr;
static Harness::ITimerCounter* s_timer = nullptr;
static Harness::IEncoderSampler* s_sampler = nullptr;
static Harness::ICriticalSection* s_critSection = nullptr;
static Harness::IClock* s_clock = nullptr;
static Harness::IEncoderProcessing* s_processor = nullptr;
static Harness::IEncoderStatusSink* s_statusSink = nullptr;

// Encoder state (protected by critical section)
static volatile EncoderState s_state = {};

// DMA tracking
static uint32_t s_lastDmaIdx = 0;

// Current DMA sample rate (for velocity calculation)
static uint16_t s_sampleRateHz = 1000;

// Measurement window (controls how much of DMA buffer is used for velocity)
static uint8_t s_measWindowMs = 40;

// ============================================================================
// Encoder init
// ============================================================================

bool EncoderTask_Init(Harness::IEncoderHardware& encoder,
                      Harness::ITimerCounter& timer,
                      Harness::IEncoderSampler& sampler,
                      Harness::ICriticalSection& critSection,
                      Harness::IClock& clock,
                      Harness::IEncoderProcessing& processor,
                      Harness::IEncoderStatusSink& statusSink,
                      uint8_t measWindowMs,
                      uint16_t sampleRateHz)
{
    s_encoder = &encoder;
    s_timer = &timer;
    s_sampler = &sampler;
    s_critSection = &critSection;
    s_clock = &clock;
    s_processor = &processor;
    s_statusSink = &statusSink;

    // Mark encoder as initializing
    s_statusSink->setEncoderStatus(Harness::EncoderStatus::INITIALIZING);

    // Check that hardware is ready
    if (!s_encoder->isReady()) {
        s_statusSink->setEncoderStatus(Harness::EncoderStatus::NOT_PRESENT);
        return false;
    }

    // Initialize state
    s_state.count = 0;
    s_state.velocity = 0;
    s_state.indexSeen = false;
    s_state.indexTick = 0;
    s_state.revolutions = 0;
    s_state.indexPeriodUs = 0;
    s_lastDmaIdx = 0;

    // Apply task-level params
    s_measWindowMs = (measWindowMs != 0) ? measWindowMs : 40;
    uint16_t rate = (sampleRateHz != 0) ? sampleRateHz : 1000;
    s_sampleRateHz = rate;

    // Pre-fill DMA buffer with current count to avoid false deltas on first process
    auto currentCnt = s_timer->getCount();
    s_sampler->prefillBuffer(currentCnt);

    // Start DMA sampling
    s_sampler->start(rate);

    // Register EXTI callback for encoder index ISR (line 9 = PC9)
    Harness::setExtiCallback(9, EncoderTask_IndexISR);

    // Encoder ready
    s_statusSink->setEncoderStatus(Harness::EncoderStatus::READY);
    return true;
}

// ============================================================================
// Task loop
// ============================================================================

void vEncoderTask(void* pvParameters)
{
    (void)pvParameters;

    // Check if encoder is available
    if (s_encoder == nullptr || !s_encoder->isReady()) {
        while (true) {}  // Defensive halt — composition root guarantees valid state
    }

    uint32_t lastWakeMs = s_clock->getTickMs();
    const uint32_t bufSize = s_sampler->getBufferSize();

    while (true) {
        Harness::setTraceTaskId(Harness::TASK_ENCODER);
        Harness::setTraceServiceId(Harness::SVC_ENCODER);

        // Get hardware timestamp for quality tracking
        uint32_t tickUs = Harness::microsecondClock()->getTickUs();

        // --- Process all new DMA samples via EncoderProcessor ---
        uint32_t writeIdx = s_sampler->getWriteIndex();
        const uint16_t* buf = s_sampler->getBuffer();

        while (s_lastDmaIdx != writeIdx) {
            s_processor->feedSample(buf[s_lastDmaIdx], tickUs);
            s_lastDmaIdx = (s_lastDmaIdx + 1) % bufSize;
        }

        // --- Compute raw velocity from DMA measurement window ---
        auto windowSamples = static_cast<uint32_t>(s_measWindowMs)
                               * s_sampleRateHz / 1000;
        if (windowSamples < 1) windowSamples = 1;
        if (windowSamples > bufSize - 1) {
            windowSamples = bufSize - 1;
        }

        int32_t rawVelocity = 0;
        {
            uint32_t latestIdx = (writeIdx - 1 + bufSize) % bufSize;
            uint32_t oldIdx = (writeIdx - 1 - windowSamples + bufSize)
                            % bufSize;
            auto delta = static_cast<int16_t>(
                buf[latestIdx] - buf[oldIdx]);
            rawVelocity = static_cast<int32_t>(delta)
                        * static_cast<int32_t>(s_sampleRateHz)
                        / static_cast<int32_t>(windowSamples);
        }

        // --- Filter + quality assessment via service ---
        int32_t filtered = s_processor->filter(rawVelocity, tickUs);

        // --- Index pulse status (from hardware encoder driver) ---
        bool indexSeen = s_encoder->isIndexSeen();
        uint32_t indexTick = s_encoder->getIndexTick();
        int32_t revolutions = s_encoder->getRevolutions();
        uint32_t indexPeriodUs = s_encoder->getIndexPeriodUs();

        // --- Update state (critical section for thread safety) ---
        s_critSection->enter();
        s_state.count = s_processor->getPosition();
        s_state.velocity = filtered;
        s_state.indexSeen = indexSeen;
        s_state.indexTick = indexTick;
        s_state.revolutions = revolutions;
        s_state.indexPeriodUs = indexPeriodUs;
        s_state.velocityQuality = s_processor->getQuality();
        s_critSection->exit();

        // --- Update telemetry via status sink ---
        Harness::EncoderTelemetryData telem = {};
        telem.count = s_processor->getPosition();
        telem.velocity = filtered;
        telem.indexSeen = indexSeen;
        telem.indexTick = indexTick;
        telem.revolutions = revolutions;
        telem.indexPeriodUs = indexPeriodUs;
        telem.velocityQuality = static_cast<uint8_t>(s_processor->getQuality());
        telem.filterFlags = s_processor->getFilterFlags();
        telem.measWindowMs = s_measWindowMs;
        telem.sampleRateHz = s_sampleRateHz;
        Harness::snapshotWriter().writeEncoder(telem);

        // Wait for next processing period
        s_clock->sleepUntilMs(lastWakeMs, ENCODER_SAMPLE_PERIOD_MS);
    }
}

// ============================================================================
// Public API — thin wrappers delegating to service or hardware
// ============================================================================

EncoderState EncoderTask_GetState()
{
    EncoderState state;
    s_critSection->enter();
    state.count = s_state.count;
    state.velocity = s_state.velocity;
    state.indexSeen = s_state.indexSeen;
    state.indexTick = s_state.indexTick;
    state.revolutions = s_state.revolutions;
    state.indexPeriodUs = s_state.indexPeriodUs;
    state.velocityQuality = s_state.velocityQuality;
    s_critSection->exit();
    return state;
}

int32_t EncoderTask_GetCount()
{
    if (s_encoder == nullptr || !s_encoder->isReady()) {
        return 0;
    }
    return s_encoder->getCount();
}

void EncoderTask_ClearIndexFlag()
{
    if (s_encoder != nullptr) {
        s_encoder->clearIndexFlag();
    }
    s_critSection->enter();
    s_state.indexSeen = false;
    s_critSection->exit();
}

void EncoderTask_ResetCount()
{
    if (s_encoder != nullptr) {
        s_encoder->reset();
    }
    s_critSection->enter();
    s_state.count = 0;
    s_processor->reset();
    s_critSection->exit();
}

void EncoderTask_SetFilterConfig(const EncoderFilterConfig& cfg)
{
    // Delegate filter params to service
    s_processor->setFilterConfig(
        cfg.filterFlags, cfg.emaAlpha, cfg.smaWindow,
        cfg.padeGainPct, cfg.padeMaxCorr,
        cfg.biquadCutoffHz, cfg.notchCenterHz, cfg.notchQ10,
        cfg.holtAlpha, cfg.holtBeta);

    // Handle task-level params
    s_measWindowMs = (cfg.measWindowMs != 0) ? cfg.measWindowMs : 40;

    // Handle sample rate change (DMA reconfiguration)
    uint16_t rate = (cfg.sampleRateHz != 0) ? cfg.sampleRateHz : 1000;
    if (rate != s_sampleRateHz) {
        EncoderTask_SetSampleRate(rate);
    }
}

void EncoderTask_GetFilterConfig(EncoderFilterConfig& cfg)
{
    // Get filter params from service
    s_processor->getFilterConfig(cfg);

    // Fill in task-level params
    cfg.measWindowMs = s_measWindowMs;
    cfg.sampleRateHz = s_sampleRateHz;
}

void EncoderTask_SetFilter(uint8_t type, uint8_t param)
{
    s_processor->setFilter(type, param);
}

void EncoderTask_GetFilter(uint8_t &type, uint8_t &param)
{
    s_processor->getFilter(type, param);
}

void EncoderTask_SetSampleRate(uint16_t hz)
{
    if (hz < 100) hz = 100;
    if (hz > 10000) hz = 10000;

    s_sampler->stop();

    // Pre-fill buffer with current count
    if (s_timer != nullptr) {
        auto currentCnt = s_timer->getCount();
        s_sampler->prefillBuffer(currentCnt);
    }
    s_lastDmaIdx = 0;

    s_sampleRateHz = hz;
    s_sampler->start(hz);
}

void EncoderTask_IndexISR()
{
    if (s_encoder != nullptr && s_clock != nullptr) {
        uint32_t tickUs = Harness::microsecondClock()->getTickUs();
        s_encoder->indexISR(s_clock->getTickMsFromISR(), tickUs);
    }
}

bool EncoderTask_IsAvailable()
{
    return (s_encoder != nullptr && s_encoder->isReady());
}

// ============================================================================
// Harness interface implementations (eliminate wiring adapters)
// ============================================================================

class EncoderAccess : public Harness::IEncoder {
public:
    Harness::EncoderSnapshot getState() override {
        EncoderState st = EncoderTask_GetState();
        Harness::EncoderSnapshot snap{};
        snap.count           = st.count;
        snap.velocity        = st.velocity;
        snap.indexSeen       = st.indexSeen;
        snap.indexTick       = st.indexTick;
        snap.revolutions     = st.revolutions;
        snap.indexPeriodUs   = st.indexPeriodUs;
        snap.velocityQuality = st.velocityQuality;
        return snap;
    }
    int32_t getCount() override { return EncoderTask_GetCount(); }
    void resetCount() override { EncoderTask_ResetCount(); }
    void clearIndexFlag() override { EncoderTask_ClearIndexFlag(); }
    bool isAvailable() override { return EncoderTask_IsAvailable(); }
};

class EncoderFilterAccess : public Harness::IEncoderFilterControl {
public:
    void getConfig(Harness::IEncoderDispatcher::EncFilterParams& out) override {
        EncoderTask_GetFilterConfig(out);
    }
    void setConfig(const Harness::IEncoderDispatcher::EncFilterParams& cfg) override {
        EncoderTask_SetFilterConfig(cfg);
    }
    void setLegacy(uint8_t type, uint8_t param) override {
        EncoderTask_SetFilter(type, param);
    }
};

static EncoderAccess s_encoderAccess;
static EncoderFilterAccess s_filterAccess;

Harness::IEncoder* EncoderTask_GetEncoderInterface() { return &s_encoderAccess; }
Harness::IEncoderFilterControl* EncoderTask_GetFilterInterface() { return &s_filterAccess; }

} // namespace Scheduler
