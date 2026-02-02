/**
 * @file encoder.hpp
 * @brief Quadrature encoder hardware driver
 *
 * Provides hardware abstraction for quadrature encoder using TIM2 in encoder
 * mode. Supports optional index pulse detection via EXTI4.
 *
 * Pin assignments:
 *   EA  - PA0 (TIM2_CH1) - Quadrature A
 *   EB  - PA1 (TIM2_CH2) - Quadrature B
 *   EZ  - PC4 (EXTI4)    - Index pulse (active low, optional)
 */

#pragma once

#include "board/board_pins.hpp"
#include "stm32f401xe.h"
#include <stdint.h>

/**
 * @brief Hardware quadrature encoder driver
 *
 * Wraps TIM2 in encoder mode for 32-bit position counting.
 * Index pulse detection available via EXTI4 interrupt.
 */
class Encoder {
public:
  /**
   * @brief Encoder hardware status
   */
  enum class Status : uint8_t {
    NOT_INITIALIZED = 0,
    INITIALIZING = 1,
    READY = 2,
    FAULT = 3
  };

  Encoder()
      : m_status(Status::NOT_INITIALIZED), m_indexSeen(false), m_indexTick(0) {}

  /**
   * @brief Initialize encoder hardware
   *
   * Configures TIM2 in encoder mode and optionally enables index pulse
   * interrupt.
   *
   * @return true if initialization succeeded
   */
  bool init();

  /**
   * @brief Get current status
   */
  Status getStatus() const { return m_status; }

  /**
   * @brief Check if encoder is ready for use
   */
  bool isReady() const { return m_status == Status::READY; }

  /**
   * @brief Get current position count
   *
   * Direct register read, always returns current hardware value.
   * Thread-safe (single 32-bit read).
   *
   * @return Signed 32-bit encoder count
   */
  int32_t getCount() const { return static_cast<int32_t>(TIM2->CNT); }

  /**
   * @brief Reset position count to zero
   */
  void reset() { TIM2->CNT = 0; }

  /**
   * @brief Check if index pulse has been seen
   */
  bool isIndexSeen() const { return m_indexSeen; }

  /**
   * @brief Get tick when index pulse was last detected
   */
  uint32_t getIndexTick() const { return m_indexTick; }

  /**
   * @brief Clear index pulse flag
   */
  void clearIndexFlag() { m_indexSeen = false; }

  /**
   * @brief Index pulse ISR callback
   *
   * Called from EXTI4_IRQHandler when index pulse detected.
   * Sets indexSeen flag and records tick time.
   *
   * @param tick Current FreeRTOS tick count
   */
  void indexISR(uint32_t tick) {
    m_indexSeen = true;
    m_indexTick = tick;
  }

  /**
   * @brief Enable index pulse interrupt
   *
   * Configures EXTI4 for falling edge interrupt on PC4.
   */
  void enableIndexInterrupt();

  /**
   * @brief Disable index pulse interrupt
   */
  void disableIndexInterrupt();

private:
  Status m_status;
  volatile bool m_indexSeen;
  volatile uint32_t m_indexTick;

  /**
   * @brief Initialize GPIO pins for encoder
   */
  bool initGPIO();

  /**
   * @brief Initialize TIM2 in encoder mode
   */
  bool initTimer();
};
