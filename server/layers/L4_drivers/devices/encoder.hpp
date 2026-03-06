/**
 * @file encoder.hpp
 * @brief Quadrature encoder hardware driver
 *
 * Timer counter is injected via constructor — no CMSIS dependency.
 * Supports optional index pulse detection via EXTI (configured externally).
 *
 * Note: Timer is 16-bit (wraps at 65535). Position is sign-extended to int32_t.
 */

#pragma once

#include "harness/pins/itimer_counter.hpp"
#include "harness/pins/iencoder_hardware.hpp"
#include <stdint.h>

namespace Drivers {

/**
 * @brief Hardware quadrature encoder driver
 *
 * Wraps a timer counter in encoder mode for 16-bit position counting.
 * Index pulse detection available via external EXTI interrupt.
 * Implements IEncoderHardware for task-level decoupling.
 */
class Encoder : public Harness::IEncoderHardware {
public:
  enum class Status : uint8_t {
    NOT_INITIALIZED = 0,
    INITIALIZING = 1,
    READY = 2,
    FAULT = 3
  };

  /**
   * @brief Construct with a pre-initialized timer counter
   *
   * Timer must be configured in encoder mode before construction.
   */
  explicit Encoder(Harness::ITimerCounter& timer)
      : m_timer(timer), m_status(Status::NOT_INITIALIZED),
        m_indexSeen(false), m_indexTick(0),
        m_revolutions(0), m_lastIndexUs(0), m_indexPeriodUs(0) {}

  /**
   * @brief Initialize encoder
   *
   * GPIO and timer initialization is handled externally (L5 layer).
   * This just marks the encoder as ready.
   *
   * @return true if initialization succeeded
   */
  bool init();

  Status getStatus() const { return m_status; }

  bool isReady() const override { return m_status == Status::READY; }

  /**
   * @brief Get current position count
   *
   * TIM4 is 16-bit, so we sign-extend to int32_t via int16_t cast.
   * Thread-safe (single 16-bit read).
   */
  int32_t getCount() const override {
    return static_cast<int32_t>(static_cast<int16_t>(m_timer.getCount()));
  }

  void reset() override { m_timer.setCount(0); }

  bool isIndexSeen() const override { return m_indexSeen; }
  uint32_t getIndexTick() const override { return m_indexTick; }
  void clearIndexFlag() override { m_indexSeen = false; }

  /**
   * @brief Index pulse ISR callback
   *
   * Called from EXTI9_5_IRQHandler when index pulse detected.
   * Sets indexSeen flag and records tick time.
   */
  void indexISR(uint32_t tick, uint32_t tickUs) override {
    m_indexSeen = true;
    m_indexTick = tick;
    // Track revolution count (direction from timer DIR bit: 0=up/CW, 1=down/CCW)
    if (m_timer.isCountingDown()) {
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

  int32_t getRevolutions() const override { return m_revolutions; }
  uint32_t getIndexPeriodUs() const override { return m_indexPeriodUs; }

private:
  Harness::ITimerCounter& m_timer;
  Status m_status;
  volatile bool m_indexSeen;
  volatile uint32_t m_indexTick;
  volatile int32_t m_revolutions;
  volatile uint32_t m_lastIndexUs;
  volatile uint32_t m_indexPeriodUs;
};

} // namespace Drivers
