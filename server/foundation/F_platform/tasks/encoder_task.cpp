/**
 * @file encoder_task.cpp
 * @brief Encoder reading task with DMA sampling + composable filter chain
 *
 * DMA Architecture:
 *   TIM3 fires at configurable rate (default 1kHz) → triggers DMA1_Stream2
 *   → reads TIM4->CNT (16-bit) into circular RAM buffer (256 entries)
 *   → encoder task wakes every 10ms, processes buffer, computes velocity
 *
 * Filter Pipeline:
 *   1. Velocity from measurement window (configurable time span)
 *   2. [Optional] EMA on velocity
 *   3. [Optional] SMA on velocity
 *   4. [Optional] Padé [1/1] sharpener (lag compensation)
 *   5. Deadband (±2 tps → 0)
 *   6. Velocity quality assessment
 *
 * Encoder is OPTIONAL — system operates in OPEN_LOOP if init fails.
 */

#include "F_platform/tasks/encoder_task.hpp"
#include "L4_drivers/devices/encoder.hpp"
#include "L3_services/motion/control_mode.hpp"
#include "L3_services/infra/tick_timer.hpp"
#include "L3_services/infra/trace.hpp"
#include "L3_services/motion/motor_config.hpp"
#include "F_platform/types/telemetry.hpp"
#include "X_vendor/CMSIS/stm32f401xe.h"
#include <math.h>
#include "X_vendor/CMSIS/core_cm4.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/task.h"

namespace Tasks {

// Encoder driver instance
static Encoder s_encoder;

// Encoder state (protected by critical section)
static volatile EncoderState s_state = {};

// ============================================================================
// DMA circular buffer — filled by hardware, processed by task
// ============================================================================

static uint16_t s_dmaBuf[ENC_DMA_BUF_SIZE] __attribute__((aligned(4)));
static uint32_t s_lastDmaIdx = 0;  // Last processed index in DMA buffer

// 64-bit accumulator for wrap-free position tracking
static int64_t s_accumulatedCount = 0;
static uint16_t s_lastRawCount = 0;

// Current DMA sample rate (for velocity calculation)
static uint16_t s_sampleRateHz = 1000;

// ============================================================================
// Filter configuration and state
// ============================================================================

static uint8_t s_filterFlags = ENC_FILT_EMA | ENC_FILT_SMA | ENC_FILT_PADE | ENC_FILT_BIQUAD;
static uint8_t s_emaAlpha = 200;        // EMA smoothing factor
static uint8_t s_smaWindow = 8;         // SMA window (0=adaptive)
static uint8_t s_measWindowMs = 40;     // Measurement window in ms

// EMA state
static int32_t s_emaVelocity = 0;

// SMA state
static constexpr int SMA_MAX_WINDOW = 32;
static int32_t s_smaBuf[SMA_MAX_WINDOW] = {};
static int s_smaHead = 0;
static int s_smaCount = 0;

// Padé [1/1] sharpener state (3-sample history of post-EMA/SMA output)
static float s_padeHistory[3] = {0.0f, 0.0f, 0.0f};
static int s_padeCount = 0;     // Samples seen (need ≥3 before sharpening)
static uint8_t s_padeGainPct = 25;  // Correction strength 0-100%
static uint8_t s_padeMaxCorr = 50;  // Max absolute correction (tps)

// Butterworth 2nd-order IIR low-pass (biquad) state
static float s_bqB0 = 0.0f, s_bqB1 = 0.0f, s_bqB2 = 0.0f;
static float s_bqA1 = 0.0f, s_bqA2 = 0.0f;
static float s_bqW0 = 0.0f, s_bqW1 = 0.0f;
static uint8_t s_biquadCutoffHz = 5;

// Notch (band-reject) filter state — same biquad structure, separate coefficients
static float s_ntB0 = 0.0f, s_ntB1 = 0.0f, s_ntB2 = 0.0f;
static float s_ntA1 = 0.0f, s_ntA2 = 0.0f;
static float s_ntW0 = 0.0f, s_ntW1 = 0.0f;
static uint8_t s_notchCenterHz = 25;
static uint8_t s_notchQ10 = 50;

// Holt's double exponential smoothing state
static float s_holtLevel = 0.0f;
static float s_holtTrend = 0.0f;
static bool  s_holtInit = false;
static uint8_t s_holtAlpha = 51;   // ~0.20
static uint8_t s_holtBeta = 13;    // ~0.05

// Velocity quality tracking
static uint32_t s_lastEdgeUs = 0;       // Timestamp of last non-zero delta
static VelocityQuality s_velQuality = VelocityQuality::INVALID;

// Filtered velocity output
static int32_t s_filteredVelocity = 0;

// Task handle for self-suspension
static TaskHandle_t s_taskHandle = nullptr;

// ============================================================================
// DMA + TIM3 initialization
// ============================================================================

static void initDMA(uint16_t sampleRateHz)
{
    // Clamp sample rate
    if (sampleRateHz < 100) sampleRateHz = 100;
    if (sampleRateHz > 10000) sampleRateHz = 10000;
    s_sampleRateHz = sampleRateHz;

    // --- TIM3: periodic trigger for DMA ---
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    volatile uint32_t dummy = RCC->APB1ENR;
    (void)dummy;

    TIM3->CR1 = 0;                // Stop during config
    TIM3->PSC = 83;               // 84 MHz APB1 timer / 84 = 1 MHz tick
    // ARR = (1MHz / sampleRateHz) - 1
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

    // Configure: Channel 5, P→M, 16-bit, circular, memory increment
    DMA1_Stream2->PAR  = reinterpret_cast<uint32_t>(&TIM4->CNT);
    DMA1_Stream2->M0AR = reinterpret_cast<uint32_t>(s_dmaBuf);
    DMA1_Stream2->NDTR = ENC_DMA_BUF_SIZE;
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

static void stopDMA()
{
    TIM3->CR1 = 0;           // Stop TIM3
    TIM3->DIER = 0;          // Disable DMA requests
    DMA1_Stream2->CR = 0;    // Disable DMA stream
    while (DMA1_Stream2->CR & DMA_SxCR_EN) {}
}

// ============================================================================
// Encoder init
// ============================================================================

bool EncoderTask_Init()
{
    // Mark encoder as initializing
    Services::g_controlMode.setEncoderStatus(Services::EncoderStatus::INITIALIZING);

    // Try to initialize encoder hardware
    if (!s_encoder.init()) {
        // Encoder init failed - this is OK, system will run in OPEN_LOOP
        Services::g_controlMode.setEncoderStatus(Services::EncoderStatus::NOT_PRESENT);
        return false;
    }

    // Initialize state
    s_state.count = 0;
    s_state.velocity = 0;
    s_state.indexSeen = false;
    s_state.indexTick = 0;
    s_state.revolutions = 0;
    s_state.indexPeriodUs = 0;
    s_accumulatedCount = 0;
    s_lastRawCount = static_cast<uint16_t>(TIM4->CNT & 0xFFFF);
    s_lastDmaIdx = 0;
    s_filteredVelocity = 0;
    s_emaVelocity = 0;
    s_smaHead = 0;
    s_smaCount = 0;
    s_padeHistory[0] = s_padeHistory[1] = s_padeHistory[2] = 0.0f;
    s_padeCount = 0;
    s_bqW0 = s_bqW1 = 0.0f;
    s_ntW0 = s_ntW1 = 0.0f;
    s_holtLevel = 0.0f;
    s_holtTrend = 0.0f;
    s_holtInit = false;

    // Load filter config from flash
    const auto& cfg = Services::g_motorConfig;
    s_filterFlags = cfg.getEncFilterType();
    s_emaAlpha = cfg.getEncFilterParam();
    s_smaWindow = cfg.getEncSmaWindow();
    s_measWindowMs = cfg.getEncMeasWindowMs();
    uint16_t rate = cfg.getEncSampleRateHz();

    // Pre-fill DMA buffer with current count to avoid false deltas on first process
    auto currentCnt = static_cast<uint16_t>(TIM4->CNT & 0xFFFF);
    for (uint32_t i = 0; i < ENC_DMA_BUF_SIZE; i++) {
        s_dmaBuf[i] = currentCnt;
    }

    // Initialize DMA + TIM3
    initDMA(rate);

    // Encoder ready
    Services::g_controlMode.setEncoderStatus(Services::EncoderStatus::READY);
    return true;
}

// ============================================================================
// Filter application (composable chain)
// ============================================================================

static int32_t applyEMA(int32_t raw)
{
    if (s_emaAlpha == 0) {
        s_emaVelocity = raw;
        return raw;
    }
    // EMA: out += (raw - out) * (256 - alpha) / 256
    // Higher alpha = more weight on old value = smoother
    int32_t weight = 256 - static_cast<int32_t>(s_emaAlpha);
    s_emaVelocity += (raw - s_emaVelocity) * weight / 256;
    return s_emaVelocity;
}

static int32_t applySMA(int32_t raw)
{
    int window;
    if (s_smaWindow == 0) {
        // Adaptive window: longer at low speed, shorter at high speed
        int32_t absVel = (s_filteredVelocity < 0) ? -s_filteredVelocity : s_filteredVelocity;
        if (absVel < 100) {
            window = 8;
        } else if (absVel > 1000) {
            window = 3;
        } else {
            window = 8 - (((absVel - 100) * 5) / 900);
        }
    } else {
        window = static_cast<int>(s_smaWindow);
    }
    if (window < 2) window = 2;
    if (window > SMA_MAX_WINDOW) window = SMA_MAX_WINDOW;

    s_smaBuf[s_smaHead] = raw;
    s_smaHead = (s_smaHead + 1) % window;
    if (s_smaCount < window) s_smaCount++;
    if (s_smaCount > window) s_smaCount = window;

    int32_t sum = 0;
    for (int i = 0; i < s_smaCount; i++) sum += s_smaBuf[i];
    return sum / s_smaCount;
}

/**
 * @brief Padé [1/1] sharpener — lag compensation via Toeplitz matrix method
 *
 * Takes the 3 most recent post-filter samples and computes a correction
 * that removes the averaging blur introduced by EMA/SMA.  Constant-time
 * (no loops), uses only 3 floats of state.
 *
 * Math:  d1 = y[1]-y[0],  d2 = y[2]-y[1],  curvature = d1-d2
 *        correction = (d1*d2) / curvature
 *        result = y[1] + correction
 */
static int32_t applyPadeSharpen(int32_t input)
{
    // Shift history: [0]=oldest, [1]=previous, [2]=current
    s_padeHistory[0] = s_padeHistory[1];
    s_padeHistory[1] = s_padeHistory[2];
    s_padeHistory[2] = static_cast<float>(input);

    if (s_padeCount < 3) {
        s_padeCount++;
        return input;  // Not enough history yet
    }

    float d1 = s_padeHistory[1] - s_padeHistory[0];
    float d2 = s_padeHistory[2] - s_padeHistory[1];
    float curvature = d1 - d2;

    // Flat signal or no curvature — return current value unchanged
    if (curvature > -1e-6f && curvature < 1e-6f) {
        return input;
    }

    float correction = (d1 * d2) / curvature;

    // Apply gain scaling (0-100%)
    uint8_t gain = (s_padeGainPct > 0) ? s_padeGainPct : 50;
    correction = correction * static_cast<float>(gain) / 100.0f;

    // Clamp absolute correction to max (prevents spikes at step transitions)
    float maxC = static_cast<float>((s_padeMaxCorr > 0) ? s_padeMaxCorr : 50);
    if (correction > maxC)  correction = maxC;
    if (correction < -maxC) correction = -maxC;

    float result = s_padeHistory[1] + correction;

    return static_cast<int32_t>(result + (result >= 0.0f ? 0.5f : -0.5f));
}

// ============================================================================
// Butterworth 2nd-order IIR low-pass — coefficient computation
// Called from SetFilterConfig (runs on comms task, 8KB stack)
// ============================================================================

static void computeBiquadCoeffs(uint8_t cutoffHz)
{
    float fc = (cutoffHz > 0 && cutoffHz <= 50) ? static_cast<float>(cutoffHz) : 10.0f;
    float fs = 1000.0f / static_cast<float>(ENCODER_SAMPLE_PERIOD_MS); // 100 Hz
    float K = tanf(3.14159265f * fc / fs);
    float K2 = K * K;
    float sqrt2 = 1.41421356f;
    float norm = 1.0f / (1.0f + sqrt2 * K + K2);

    s_bqB0 = K2 * norm;
    s_bqB1 = 2.0f * s_bqB0;
    s_bqB2 = s_bqB0;
    s_bqA1 = 2.0f * (K2 - 1.0f) * norm;
    s_bqA2 = (1.0f - sqrt2 * K + K2) * norm;

    // Reset filter state
    s_bqW0 = 0.0f;
    s_bqW1 = 0.0f;
}

static int32_t applyBiquad(int32_t input)
{
    float x = static_cast<float>(input);
    float y = s_bqB0 * x + s_bqW0;
    s_bqW0 = s_bqB1 * x - s_bqA1 * y + s_bqW1;
    s_bqW1 = s_bqB2 * x - s_bqA2 * y;
    return static_cast<int32_t>(y + (y >= 0.0f ? 0.5f : -0.5f));
}

// ============================================================================
// Notch (band-reject) filter — coefficient computation
// ============================================================================

static void computeNotchCoeffs(uint8_t centerHz, uint8_t q10)
{
    float fc = (centerHz > 0 && centerHz <= 50) ? static_cast<float>(centerHz) : 25.0f;
    float Q = (q10 > 0 && q10 <= 100) ? static_cast<float>(q10) / 10.0f : 5.0f;
    float fs = 1000.0f / static_cast<float>(ENCODER_SAMPLE_PERIOD_MS); // 100 Hz
    float w0 = 2.0f * 3.14159265f * fc / fs;
    float sinW = sinf(w0);
    float cosW = cosf(w0);
    float alpha = sinW / (2.0f * Q);
    float norm = 1.0f / (1.0f + alpha);

    s_ntB0 = norm;
    s_ntB1 = -2.0f * cosW * norm;
    s_ntB2 = norm;
    s_ntA1 = -2.0f * cosW * norm;
    s_ntA2 = (1.0f - alpha) * norm;

    // Reset filter state
    s_ntW0 = 0.0f;
    s_ntW1 = 0.0f;
}

static int32_t applyNotch(int32_t input)
{
    float x = static_cast<float>(input);
    float y = s_ntB0 * x + s_ntW0;
    s_ntW0 = s_ntB1 * x - s_ntA1 * y + s_ntW1;
    s_ntW1 = s_ntB2 * x - s_ntA2 * y;
    return static_cast<int32_t>(y + (y >= 0.0f ? 0.5f : -0.5f));
}

// ============================================================================
// Holt's double exponential smoothing
// ============================================================================

static int32_t applyHolt(int32_t input)
{
    float x = static_cast<float>(input);
    float a = static_cast<float>((s_holtAlpha > 0) ? s_holtAlpha : 51) / 255.0f;
    float b = static_cast<float>((s_holtBeta > 0) ? s_holtBeta : 13) / 255.0f;

    if (!s_holtInit) {
        s_holtLevel = x;
        s_holtTrend = 0.0f;
        s_holtInit = true;
        return input;
    }

    float prevLevel = s_holtLevel;
    s_holtLevel = a * x + (1.0f - a) * (s_holtLevel + s_holtTrend);
    s_holtTrend = b * (s_holtLevel - prevLevel) + (1.0f - b) * s_holtTrend;

    // Output: current level + one-step trend prediction
    float result = s_holtLevel + s_holtTrend;
    return static_cast<int32_t>(result + (result >= 0.0f ? 0.5f : -0.5f));
}

// ============================================================================
// Task loop
// ============================================================================

void vEncoderTask(void* pvParameters)
{
    (void)pvParameters;

    // Store task handle for potential suspension
    s_taskHandle = xTaskGetCurrentTaskHandle();

    // Check if encoder is available
    if (!s_encoder.isReady()) {
        vTaskSuspend(nullptr);
        return;
    }

    TickType_t lastWakeTime = xTaskGetTickCount();

    while (true) {
        Trace::setCurrentTaskId(Trace::TASK_ENCODER);
        Trace::setCurrentServiceId(Trace::SVC_ENCODER);

        // Get hardware timestamp for quality tracking
        uint32_t tickUs = Services::TickTimer_GetTick();

        // --- Process all new DMA samples ---
        // DMA writes to s_dmaBuf[0..N-1] in circular mode.
        // NDTR counts down from ENC_DMA_BUF_SIZE; current write index =
        // ENC_DMA_BUF_SIZE - NDTR.
        uint32_t writeIdx = ENC_DMA_BUF_SIZE - DMA1_Stream2->NDTR;

        while (s_lastDmaIdx != writeIdx) {
            uint16_t rawCount = s_dmaBuf[s_lastDmaIdx];
            auto delta = static_cast<int16_t>(rawCount - s_lastRawCount);
            s_accumulatedCount += delta;
            s_lastRawCount = rawCount;

            // Track edges for velocity quality
            if (delta != 0) {
                s_lastEdgeUs = tickUs;
            }

            s_lastDmaIdx = (s_lastDmaIdx + 1) % ENC_DMA_BUF_SIZE;
        }

        // --- Compute velocity over measurement window ---
        // windowSamples = measWindowMs * sampleRateHz / 1000
        auto windowSamples = static_cast<uint32_t>(s_measWindowMs)
                               * s_sampleRateHz / 1000;
        if (windowSamples < 1) windowSamples = 1;
        if (windowSamples > ENC_DMA_BUF_SIZE - 1) {
            windowSamples = ENC_DMA_BUF_SIZE - 1;
        }

        int32_t velocity = 0;
        // Need at least windowSamples entries in the buffer to compute velocity.
        // After init, buffer is pre-filled so this is always valid after
        // the first few DMA cycles.
        {
            // Latest sample is one before writeIdx
            uint32_t latestIdx = (writeIdx - 1 + ENC_DMA_BUF_SIZE) % ENC_DMA_BUF_SIZE;
            uint32_t oldIdx = (writeIdx - 1 - windowSamples + ENC_DMA_BUF_SIZE)
                            % ENC_DMA_BUF_SIZE;
            auto delta = static_cast<int16_t>(
                s_dmaBuf[latestIdx] - s_dmaBuf[oldIdx]);
            // velocity = delta * sampleRateHz / windowSamples  (counts/sec)
            velocity = static_cast<int32_t>(delta)
                     * static_cast<int32_t>(s_sampleRateHz)
                     / static_cast<int32_t>(windowSamples);
        }

        // --- Apply composable filter chain ---
        int32_t filtered = velocity;

        if (s_filterFlags & ENC_FILT_EMA) {
            filtered = applyEMA(filtered);
        } else {
            s_emaVelocity = filtered;  // Keep EMA state tracking raw
        }

        if (s_filterFlags & ENC_FILT_SMA) {
            filtered = applySMA(filtered);
        }

        if (s_filterFlags & ENC_FILT_PADE) {
            filtered = applyPadeSharpen(filtered);
        } else {
            // Keep history tracking raw so Padé starts clean when enabled
            s_padeHistory[0] = s_padeHistory[1] = s_padeHistory[2]
                             = static_cast<float>(filtered);
            s_padeCount = 0;
        }

        if (s_filterFlags & ENC_FILT_BIQUAD) {
            filtered = applyBiquad(filtered);
        } else {
            s_bqW0 = 0.0f;
            s_bqW1 = 0.0f;
        }

        if (s_filterFlags & ENC_FILT_NOTCH) {
            filtered = applyNotch(filtered);
        } else {
            s_ntW0 = 0.0f;
            s_ntW1 = 0.0f;
        }

        if (s_filterFlags & ENC_FILT_HOLT) {
            filtered = applyHolt(filtered);
        } else {
            s_holtLevel = static_cast<float>(filtered);
            s_holtTrend = 0.0f;
            s_holtInit = false;
        }

        // Deadband: suppress ±2 count/s noise at rest
        if (filtered > -3 && filtered < 3) {
            filtered = 0;
        }

        s_filteredVelocity = filtered;

        // --- Velocity quality assessment ---
        {
            uint32_t edgeAgeUs = tickUs - s_lastEdgeUs;
            if (edgeAgeUs > 200000 && s_lastEdgeUs != 0) {
                s_velQuality = VelocityQuality::STALE;
            } else if (s_lastEdgeUs == 0) {
                s_velQuality = VelocityQuality::LOW_CONFIDENCE;
            } else {
                s_velQuality = VelocityQuality::GOOD;
            }
        }

        // --- Index pulse status ---
        bool indexSeen = s_encoder.isIndexSeen();
        uint32_t indexTick = s_encoder.getIndexTick();
        int32_t revolutions = s_encoder.getRevolutions();
        uint32_t indexPeriodUs = s_encoder.getIndexPeriodUs();

        // --- Update state (critical section for thread safety) ---
        taskENTER_CRITICAL();
        s_state.count = s_accumulatedCount;
        s_state.velocity = s_filteredVelocity;
        s_state.indexSeen = indexSeen;
        s_state.indexTick = indexTick;
        s_state.revolutions = revolutions;
        s_state.indexPeriodUs = indexPeriodUs;
        s_state.velocityQuality = s_velQuality;
        taskEXIT_CRITICAL();

        // --- Update telemetry ---
        Comms::EncoderTelemetry telem = {};
        telem.count = s_accumulatedCount;
        telem.velocity = s_filteredVelocity;
        telem.indexSeen = indexSeen;
        telem.indexTick = indexTick;
        telem.revolutions = revolutions;
        telem.indexPeriodUs = indexPeriodUs;
        telem.velocityQuality = static_cast<uint8_t>(s_velQuality);
        telem.filterFlags = s_filterFlags;
        telem.measWindowMs = s_measWindowMs;
        telem.sampleRateHz = s_sampleRateHz;
        Comms::g_telemetry.updateEncoder(telem);

        // Wait for next processing period
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(ENCODER_SAMPLE_PERIOD_MS));
    }
}

// ============================================================================
// Public API
// ============================================================================

EncoderState EncoderTask_GetState()
{
    EncoderState state;
    taskENTER_CRITICAL();
    state.count = s_state.count;
    state.velocity = s_state.velocity;
    state.indexSeen = s_state.indexSeen;
    state.indexTick = s_state.indexTick;
    state.revolutions = s_state.revolutions;
    state.indexPeriodUs = s_state.indexPeriodUs;
    state.velocityQuality = s_state.velocityQuality;
    taskEXIT_CRITICAL();
    return state;
}

int32_t EncoderTask_GetCount()
{
    if (!s_encoder.isReady()) {
        return 0;
    }
    return s_encoder.getCount();
}

void EncoderTask_ClearIndexFlag()
{
    s_encoder.clearIndexFlag();
    taskENTER_CRITICAL();
    s_state.indexSeen = false;
    taskEXIT_CRITICAL();
}

void EncoderTask_ResetCount()
{
    s_encoder.reset();
    taskENTER_CRITICAL();
    s_state.count = 0;
    s_accumulatedCount = 0;
    s_lastRawCount = 0;
    s_filteredVelocity = 0;
    s_emaVelocity = 0;
    s_smaHead = 0;
    s_smaCount = 0;
    s_padeHistory[0] = s_padeHistory[1] = s_padeHistory[2] = 0.0f;
    s_padeCount = 0;
    s_bqW0 = s_bqW1 = 0.0f;
    s_ntW0 = s_ntW1 = 0.0f;
    s_holtLevel = 0.0f;
    s_holtTrend = 0.0f;
    s_holtInit = false;
    taskEXIT_CRITICAL();
}

void EncoderTask_SetFilterConfig(const EncoderFilterConfig& cfg)
{
    s_filterFlags = cfg.filterFlags;
    s_emaAlpha = cfg.emaAlpha;
    s_smaWindow = cfg.smaWindow;
    s_measWindowMs = (cfg.measWindowMs != 0) ? cfg.measWindowMs : 40;
    s_padeGainPct = cfg.padeGainPct;
    s_padeMaxCorr = cfg.padeMaxCorr;
    s_biquadCutoffHz = cfg.biquadCutoffHz;
    s_notchCenterHz = cfg.notchCenterHz;
    s_notchQ10 = cfg.notchQ10;
    s_holtAlpha = cfg.holtAlpha;
    s_holtBeta = cfg.holtBeta;

    // Reset filter state on config change
    s_emaVelocity = 0;
    s_smaHead = 0;
    s_smaCount = 0;
    s_padeHistory[0] = s_padeHistory[1] = s_padeHistory[2] = 0.0f;
    s_padeCount = 0;
    s_holtLevel = 0.0f;
    s_holtTrend = 0.0f;
    s_holtInit = false;

    // Precompute biquad coefficients (uses sinf/cosf/tanf, runs on caller's stack)
    computeBiquadCoeffs(s_biquadCutoffHz);
    computeNotchCoeffs(s_notchCenterHz, s_notchQ10);

    // Handle sample rate change
    uint16_t rate = (cfg.sampleRateHz != 0) ? cfg.sampleRateHz : 1000;
    if (rate != s_sampleRateHz) {
        EncoderTask_SetSampleRate(rate);
    }
}

void EncoderTask_GetFilterConfig(EncoderFilterConfig& cfg)
{
    cfg.filterFlags = s_filterFlags;
    cfg.emaAlpha = s_emaAlpha;
    cfg.smaWindow = s_smaWindow;
    cfg.measWindowMs = s_measWindowMs;
    cfg.sampleRateHz = s_sampleRateHz;
    cfg.padeGainPct = s_padeGainPct;
    cfg.padeMaxCorr = s_padeMaxCorr;
    cfg.biquadCutoffHz = s_biquadCutoffHz;
    cfg.notchCenterHz = s_notchCenterHz;
    cfg.notchQ10 = s_notchQ10;
    cfg.holtAlpha = s_holtAlpha;
    cfg.holtBeta = s_holtBeta;
}

// Legacy API: backward compatible with old type/param interface
void EncoderTask_SetFilter(uint8_t type, uint8_t param)
{
    // Map old type to new flags:
    //   0=NONE → flags=0
    //   1=EMA  → flags=ENC_FILT_EMA, emaAlpha=param
    //   2=SMA  → flags=ENC_FILT_SMA, smaWindow=param
    s_filterFlags = 0;
    s_emaAlpha = 0;

    if (type == 1) {
        s_filterFlags = ENC_FILT_EMA;
        s_emaAlpha = param;
    } else if (type == 2) {
        s_filterFlags = ENC_FILT_SMA;
        s_smaWindow = param;
    }

    // Reset filter state
    s_emaVelocity = 0;
    s_smaHead = 0;
    s_smaCount = 0;
}

void EncoderTask_GetFilter(uint8_t &type, uint8_t &param)
{
    // Map new flags back to old type/param for backward compat
    if (s_filterFlags & ENC_FILT_EMA) {
        type = 1;
        param = s_emaAlpha;
    } else if (s_filterFlags & ENC_FILT_SMA) {
        type = 2;
        param = s_smaWindow;
    } else {
        type = 0;
        param = 0;
    }
}

void EncoderTask_SetSampleRate(uint16_t hz)
{
    if (hz < 100) hz = 100;
    if (hz > 10000) hz = 10000;

    stopDMA();

    // Pre-fill buffer with current count
    auto currentCnt = static_cast<uint16_t>(TIM4->CNT & 0xFFFF);
    for (uint32_t i = 0; i < ENC_DMA_BUF_SIZE; i++) {
        s_dmaBuf[i] = currentCnt;
    }
    s_lastDmaIdx = 0;

    initDMA(hz);
}

void EncoderTask_IndexISR()
{
    // Called from EXTI9_5_IRQHandler
    uint32_t tickUs = Services::TickTimer_GetTick();
    s_encoder.indexISR(xTaskGetTickCountFromISR(), tickUs);
}

bool EncoderTask_IsAvailable()
{
    return s_encoder.isReady();
}

} // namespace Tasks

// ============================================================================
// Encoder driver implementation (init functions)
// ============================================================================

bool Encoder::init()
{
    m_status = Status::INITIALIZING;

    if (!initGPIO()) {
        m_status = Status::FAULT;
        return false;
    }

    if (!initTimer()) {
        m_status = Status::FAULT;
        return false;
    }

    // Enable index pulse interrupt (EXTI9 on PC9)
    enableIndexInterrupt();

    m_indexSeen = false;
    m_indexTick = 0;
    m_revolutions = 0;
    m_lastIndexUs = 0;
    m_indexPeriodUs = 0;
    m_status = Status::READY;
    return true;
}

bool Encoder::initGPIO()
{
    // Enable GPIO clocks
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;

    // Brief delay for clock to stabilize
    volatile uint32_t dummy = RCC->AHB1ENR;
    (void)dummy;

    // Configure EA (PB6) and EB (PB7) as alternate function for TIM4
    // PB6 = TIM4_CH1, PB7 = TIM4_CH2, both AF2
    GPIOB->MODER &= ~(GPIO_MODER_MODER6 | GPIO_MODER_MODER7);
    GPIOB->MODER |= (0x2 << GPIO_MODER_MODER6_Pos) | (0x2 << GPIO_MODER_MODER7_Pos);
    GPIOB->AFR[0] &= ~(GPIO_AFRL_AFRL6 | GPIO_AFRL_AFRL7);
    GPIOB->AFR[0] |= (Pins::Encoder::EA_AF << GPIO_AFRL_AFRL6_Pos) |
                     (Pins::Encoder::EB_AF << GPIO_AFRL_AFRL7_Pos);

    // Configure EZ (PC9) as input with pull-up for index pulse
    GPIOC->MODER &= ~(3U << (9 * 2));   // Input mode
    GPIOC->PUPDR &= ~(3U << (9 * 2));   // Clear pull
    GPIOC->PUPDR |=  (1U << (9 * 2));   // Pull-up

    // AM26LV32EIDR line receiver enable (G/nG) handled by on-board pulls.
    // nG pulled low, G pulled high on encoder board — no STM32 GPIO needed.

    return true;
}

bool Encoder::initTimer()
{
    // Enable TIM4 clock
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;

    // Brief delay for clock to stabilize
    volatile uint32_t dummy = RCC->APB1ENR;
    (void)dummy;

    // Configure TIM4 in encoder mode (16-bit timer, ARR max 0xFFFF)
    TIM4->CR1 = 0;  // Stop timer during config
    TIM4->SMCR = TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1;  // Encoder mode 3 (count on both edges)
    TIM4->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0;  // IC1 = TI1, IC2 = TI2
    TIM4->CCER = 0;  // Rising edge, non-inverted
    TIM4->ARR = 0xFFFF;  // Full 16-bit range
    TIM4->CNT = 0;  // Reset counter
    TIM4->CR1 = TIM_CR1_CEN;  // Enable counter

    return true;
}

void Encoder::enableIndexInterrupt()
{
    // Enable SYSCFG clock for EXTI
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    // Clock stabilization delay (same pattern as initGPIO)
    volatile uint32_t dummy = RCC->APB2ENR;
    (void)dummy;

    // Map EXTI9 to PC9: EXTICR[2] bits 7:4 select source for EXTI9
    // 0x2 = Port C (RM0368 Table 40)
    SYSCFG->EXTICR[2] = (SYSCFG->EXTICR[2] & ~(0xFU << 4)) | (0x2U << 4);

    // Configure EXTI9 for falling edge (index pulse is active low)
    EXTI->IMR  |= (1U << 9);   // Unmask line 9
    EXTI->FTSR |= (1U << 9);   // Falling edge trigger

    // Enable EXTI9_5 interrupt in NVIC
    NVIC_SetPriority(EXTI9_5_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(EXTI9_5_IRQn);
}

void Encoder::disableIndexInterrupt()
{
    EXTI->IMR &= ~(1U << 9);  // Mask line 9
    NVIC_DisableIRQ(EXTI9_5_IRQn);
}

// ============================================================================
// EXTI9_5 interrupt handler (shared for EXTI lines 5-9)
// ============================================================================

extern "C" void EXTI9_5_IRQHandler(void)
{
    if (EXTI->PR & (1U << 9)) {
        EXTI->PR = (1U << 9);  // Clear pending (write-1-to-clear)
        Tasks::EncoderTask_IndexISR();
    }
}
