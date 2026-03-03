/**
 * @file encoder_task.hpp
 * @brief Encoder reading task using TIM4 hardware encoder mode + DMA sampling
 *
 * TIM4 in quadrature encoder mode on PB6/PB7 (16-bit, wraps at 65535).
 * TIM3 triggers DMA1_Stream2 at configurable rate (default 1kHz) to sample
 * TIM4->CNT into a circular buffer. Encoder task wakes every 10ms to process
 * DMA buffer, compute velocity, and apply composable filter chain.
 * Monitors PC9 (EZ) for index pulse via EXTI9.
 * Priority: Medium (tskIDLE_PRIORITY + 2)
 */

#ifndef ENCODER_TASK_HPP
#define ENCODER_TASK_HPP

#include "F_platform/types/velocity_quality_t.hpp"
#include <stdint.h>

namespace Tasks {

// Task configuration
constexpr uint32_t ENCODER_TASK_STACK_SIZE = 128;
constexpr uint32_t ENCODER_TASK_PRIORITY = 2;
constexpr uint32_t ENCODER_SAMPLE_PERIOD_MS = 10; // 100 Hz task wake

// DMA buffer size (must be power of 2 for efficient modulo)
constexpr uint32_t ENC_DMA_BUF_SIZE = 256;

// Filter flag bits
constexpr uint8_t ENC_FILT_EMA    = 0x01;  // bit 0: EMA enabled
constexpr uint8_t ENC_FILT_SMA    = 0x02;  // bit 1: SMA enabled
constexpr uint8_t ENC_FILT_PADE   = 0x04;  // bit 2: Padé [1/1] sharpener enabled
constexpr uint8_t ENC_FILT_BIQUAD = 0x08;  // bit 3: Butterworth 2nd-order IIR low-pass
constexpr uint8_t ENC_FILT_NOTCH  = 0x10;  // bit 4: Notch (band-reject) filter
constexpr uint8_t ENC_FILT_HOLT   = 0x20;  // bit 5: Holt's double exponential smoothing

/**
 * @brief Composable encoder filter configuration
 *
 * Each filter stage is independently enabled. Pipeline:
 *   raw velocity → EMA → SMA → Padé → Butterworth → Notch → Holt → deadband → output
 */
struct EncoderFilterConfig {
    uint8_t  filterFlags;      // Bitfield: ENC_FILT_EMA | SMA | PADE | BIQUAD | NOTCH | HOLT
    uint8_t  emaAlpha;         // EMA smoothing (0-255, higher = smoother)
    uint8_t  smaWindow;        // SMA window (2-32, 0 = speed-adaptive)
    uint8_t  measWindowMs;     // Measurement window in ms (1-255, 0 = default 10)
    uint16_t sampleRateHz;     // DMA sample rate (100-10000, 0 = default 1000)
    uint8_t  padeGainPct;      // Padé correction strength (0-100%, 0 = default 50)
    uint8_t  padeMaxCorr;      // Max absolute correction (tps, 0 = default 50)
    uint8_t  biquadCutoffHz;   // Butterworth cutoff frequency (1-50 Hz, 0 = default 10)
    uint8_t  notchCenterHz;    // Notch center frequency (1-50 Hz, 0 = default 25)
    uint8_t  notchQ10;         // Notch Q factor × 10 (1-100 → Q 0.1-10.0, 0 = default 50)
    uint8_t  holtAlpha;        // Holt level smoothing (0-255, 0 = default 51 ≈ 0.20)
    uint8_t  holtBeta;         // Holt trend smoothing (0-255, 0 = default 13 ≈ 0.05)
};

/**
 * @brief Encoder state snapshot
 */
struct EncoderState {
  int64_t count;          // Accumulated encoder count (64-bit, no wrap)
  int32_t velocity;       // Calculated velocity (counts/sec)
  bool indexSeen;         // Index pulse seen since last clear
  uint32_t indexTick;     // Tick when index was last seen
  int32_t revolutions;    // Revolution count from index pulses (signed)
  uint32_t indexPeriodUs; // Microseconds between last two index pulses
  VelocityQuality velocityQuality; // Measurement confidence
};

/**
 * @brief Initialize encoder task resources
 *
 * Configures TIM4 in encoder mode, TIM3 + DMA for periodic sampling,
 * sets up EXTI for index pulse. Call before vTaskStartScheduler().
 *
 * @return true on success
 */
bool EncoderTask_Init();

/**
 * @brief Check if encoder hardware is available
 * @return true if encoder was initialized successfully
 */
bool EncoderTask_IsAvailable();

/**
 * @brief Encoder task entry point
 * @param pvParameters Unused
 */
void vEncoderTask(void *pvParameters);

/**
 * @brief Get current encoder state (thread-safe copy)
 * @return EncoderState snapshot
 */
EncoderState EncoderTask_GetState();

/**
 * @brief Get raw encoder count (fast, direct register read)
 * @return Current TIM4->CNT value
 */
int32_t EncoderTask_GetCount();

/**
 * @brief Clear index seen flag
 */
void EncoderTask_ClearIndexFlag();

/**
 * @brief Reset encoder count to zero
 */
void EncoderTask_ResetCount();

/**
 * @brief Set composable filter configuration
 * @param cfg Filter configuration (all fields)
 */
void EncoderTask_SetFilterConfig(const EncoderFilterConfig& cfg);

/**
 * @brief Get current filter configuration
 * @param[out] cfg Current filter settings
 */
void EncoderTask_GetFilterConfig(EncoderFilterConfig& cfg);

/**
 * @brief Legacy: set filter type and parameter (backward compat)
 * @param type 0=NONE, 1=EMA only, 2=SMA only
 * @param param EMA: alpha (0-255), SMA: window (2-32)
 */
void EncoderTask_SetFilter(uint8_t type, uint8_t param);

/**
 * @brief Legacy: get filter type and parameter (backward compat)
 * @param[out] type Current filter type (0=NONE, 1=EMA, 2=SMA)
 * @param[out] param Current filter parameter
 */
void EncoderTask_GetFilter(uint8_t &type, uint8_t &param);

/**
 * @brief Change DMA sample rate at runtime
 * @param hz Sample rate in Hz (100-10000)
 */
void EncoderTask_SetSampleRate(uint16_t hz);

/**
 * @brief Index pulse ISR handler (call from EXTI9_5_IRQHandler)
 */
void EncoderTask_IndexISR();

} // namespace Tasks

#endif // ENCODER_TASK_HPP
