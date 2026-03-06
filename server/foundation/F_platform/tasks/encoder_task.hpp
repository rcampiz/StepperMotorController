/**
 * @file encoder_task.hpp
 * @brief Encoder reading task using TIM4 hardware encoder mode + DMA sampling
 *
 * TIM4 in quadrature encoder mode on PB6/PB7 (16-bit, wraps at 65535).
 * TIM3 triggers DMA1_Stream2 at configurable rate (default 1kHz) to sample
 * TIM4->CNT into a circular buffer. Encoder task wakes every 10ms to process
 * DMA buffer, compute velocity, and delegate filtering to EncoderProcessor.
 * Monitors PC9 (EZ) for index pulse via EXTI9.
 * Priority: Medium (tskIDLE_PRIORITY + 2)
 */

#ifndef ENCODER_TASK_HPP
#define ENCODER_TASK_HPP

#include "harness/pins/encoder_filter_config.hpp"
#include "harness/pins/velocity_quality.hpp"
#include <stdint.h>

namespace Harness { class IClock; class IEncoder; class IEncoderFilterControl;
                    class IEncoderProcessing; class IEncoderStatusSink;
                    class IEncoderHardware; class IEncoderSampler;
                    class ICriticalSection; class ITimerCounter; }

namespace Scheduler {

// Re-export filter types and constants from harness
using Harness::EncoderFilterConfig;
using Harness::ENC_FILT_EMA;
using Harness::ENC_FILT_SMA;
using Harness::ENC_FILT_PADE;
using Harness::ENC_FILT_BIQUAD;
using Harness::ENC_FILT_NOTCH;
using Harness::ENC_FILT_HOLT;

// Task configuration
constexpr uint32_t ENCODER_TASK_STACK_SIZE = 128;
constexpr uint32_t ENCODER_TASK_PRIORITY = 2;
constexpr uint32_t ENCODER_SAMPLE_PERIOD_MS = 10; // 100 Hz task wake

// DMA buffer size (must be power of 2 for efficient modulo)
constexpr uint32_t ENC_DMA_BUF_SIZE = 256;

/**
 * @brief Encoder state snapshot (includes hardware fields from encoder driver)
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
 * Stores injected dependencies from composition root, configures
 * TIM3 + DMA for periodic sampling. Call before vTaskStartScheduler().
 *
 * @param encoder       Encoder hardware interface (from L4 driver)
 * @param timer         Timer counter for TIM4 (wired by composition root)
 * @param sampler       DMA sampler (from L4 driver)
 * @param critSection   Critical section for thread-safe state access
 * @param clock         System clock (for delays and tick timing)
 * @param processor     Encoder signal processing service
 * @param statusSink    Encoder status reporting sink
 * @param measWindowMs  Measurement window in ms (0 = default 40)
 * @param sampleRateHz  DMA sample rate in Hz (0 = default 1000)
 * @return true on success, false if hardware not available
 */
bool EncoderTask_Init(Harness::IEncoderHardware& encoder,
                      Harness::ITimerCounter& timer,
                      Harness::IEncoderSampler& sampler,
                      Harness::ICriticalSection& critSection,
                      Harness::IClock& clock,
                      Harness::IEncoderProcessing& processor,
                      Harness::IEncoderStatusSink& statusSink,
                      uint8_t measWindowMs,
                      uint16_t sampleRateHz);

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

/**
 * @brief Get IEncoder interface implemented by this task
 * @return Pointer to static IEncoder instance (valid after init)
 */
Harness::IEncoder* EncoderTask_GetEncoderInterface();

/**
 * @brief Get IEncoderFilterControl interface implemented by this task
 * @return Pointer to static IEncoderFilterControl instance (valid after init)
 */
Harness::IEncoderFilterControl* EncoderTask_GetFilterInterface();

} // namespace Scheduler

#endif // ENCODER_TASK_HPP
