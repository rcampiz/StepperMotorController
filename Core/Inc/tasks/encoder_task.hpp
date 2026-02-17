/**
 * @file encoder_task.hpp
 * @brief Encoder reading task using TIM4 hardware encoder mode
 *
 * Configures TIM4 for quadrature encoder counting on PB6/PB7.
 * TIM4 is 16-bit (wraps at 65535); accumulated to int64_t in software.
 * Monitors PC4 (EZ) for index pulse via EXTI.
 * Priority: Medium (tskIDLE_PRIORITY + 2)
 */

#ifndef ENCODER_TASK_HPP
#define ENCODER_TASK_HPP

#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>

namespace Tasks {

// Task configuration
constexpr uint32_t ENCODER_TASK_STACK_SIZE = 128;
constexpr UBaseType_t ENCODER_TASK_PRIORITY = tskIDLE_PRIORITY + 2;
constexpr TickType_t ENCODER_SAMPLE_PERIOD_MS = 10; // 100 Hz sampling

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
};

/**
 * @brief Initialize encoder task resources
 *
 * Configures TIM2 in encoder mode, sets up EXTI for index.
 * Call before vTaskStartScheduler().
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
 * @return Current TIM2->CNT value
 */
int32_t EncoderTask_GetCount();

/**
 * @brief Clear index seen flag
 *
 * Call after homing routine has used the index position.
 */
void EncoderTask_ClearIndexFlag();

/**
 * @brief Reset encoder count to zero
 *
 * Sets TIM2->CNT = 0.
 */
void EncoderTask_ResetCount();

/**
 * @brief Index pulse ISR handler (call from EXTI9_5_IRQHandler)
 *
 * Sets indexSeen flag and records tick time.
 */
void EncoderTask_IndexISR();

} // namespace Tasks

#endif // ENCODER_TASK_HPP
