/**
 * @file encoder.hpp
 * @brief Quadrature encoder hardware driver
 *
 * Provides hardware abstraction for quadrature encoder using TIM4 in encoder
 * mode. Supports optional index pulse detection via EXTI4.
 *
 * Note: TIM4 is 16-bit (wraps at 65535). Position is sign-extended to int32_t.
 *
 * Pin assignments:
 *   EA  - PB6 (TIM4_CH1) - Quadrature A  (CN10 P-17)
 *   EB  - PB7 (TIM4_CH2) - Quadrature B  (CN7 P-21)
 *   EZ  - PC4 (EXTI4)    - Index pulse (active low, optional)
 */

#pragma once

#include "L5_board/board_pins.hpp"
#include "X_vendor/CMSIS/stm32f401xe.h"
#include <stdint.h>

/**
 * @brief Hardware quadrature encoder driver
 *
 * Wraps TIM4 in encoder mode for 16-bit position counting.
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
      : m_status(Status::NOT_INITIALIZED), m_indexSeen(false), m_indexTick(0),
        m_revolutions(0), m_lastIndexUs(0), m_indexPeriodUs(0) {}

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
   * TIM4 is 16-bit, so we sign-extend to int32_t via int16_t cast.
   * Thread-safe (single 16-bit read).
   *
   * @return Signed 32-bit encoder count (sign-extended from 16-bit)
   */
  int32_t getCount() const {
    return static_cast<int32_t>(static_cast<int16_t>(TIM4->CNT & 0xFFFF));
  }

  /**
   * @brief Reset position count to zero
   */
  void reset() { TIM4->CNT = 0; }

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
   * Called from EXTI9_5_IRQHandler when index pulse detected on PC9.
   * Sets indexSeen flag and records tick time.
   *
   * @param tick Current FreeRTOS tick count
   */
  void indexISR(uint32_t tick, uint32_t tickUs) {
    m_indexSeen = true;
    m_indexTick = tick;
    // Track revolution count (direction from TIM4 DIR bit: 0=up/CW, 1=down/CCW)
    if (TIM4->CR1 & (1U << 4)) {  // DIR bit: 0=upcounting, 1=downcounting
      m_revolutions--;
    } else {
      m_revolutions++;
    }
    // Compute period between successive index pulses
    if (m_lastIndexUs != 0) {
      m_indexPeriodUs = tickUs - m_lastIndexUs;
    }
    m_lastIndexUs = tickUs;
  }

  int32_t getRevolutions() const { return m_revolutions; }
  uint32_t getIndexPeriodUs() const { return m_indexPeriodUs; }

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
  volatile int32_t m_revolutions;
  volatile uint32_t m_lastIndexUs;
  volatile uint32_t m_indexPeriodUs;

  /**
   * @brief Initialize GPIO pins for encoder
   */
  bool initGPIO();

  /**
   * @brief Initialize TIM2 in encoder mode
   */
  bool initTimer();
};
