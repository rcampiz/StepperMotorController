/**
 * @file encoder_service.hpp
 * @brief L3 EncoderService — owns encoder sampling, filtering and the shared snapshot
 *
 * Replaces the body of the Encoder task. The task sheet only calls sample() every
 * ENCODER_SAMPLE_PERIOD_MS (H_TASKS_L3). Hardware arrives through H_L3_L4
 * (Drivers::IEncoderHardware); Platform primitives are injected.
 *
 * Also provides the L3-internal IEncoder / IEncoderFilterControl ports used by the
 * encoder dispatcher (Comms task context): getState() copies under a critical section.
 */
#pragma once

#include "harness/defs/harness_l3_l4.hpp"
#include "L3_services/boundary/iencoder_service.hpp"
#include "L3_services/boundary/iencoder.hpp"
#include "L3_services/boundary/iencoder_filter_control.hpp"
#include "L3_services/boundary/iencoder_processing.hpp"
#include "L3_services/boundary/iencoder_status_sink.hpp"
#include "F_platform/boundary/icritical_section.hpp"
#include "F_platform/boundary/imicrosecond_clock.hpp"
#include "harness/pins/encoder_filter_config.hpp"
#include <stdint.h>

namespace Services {

class EncoderService : public IEncoderService,
                       public IEncoder,
                       public IEncoderFilterControl {
public:
    static constexpr uint8_t  DEFAULT_MEAS_WINDOW_MS = 40;
    static constexpr uint16_t DEFAULT_SAMPLE_RATE_HZ = 1000;

    /// Bind hardware and collaborators. Returns false when the hardware is not ready.
    bool init(Drivers::IEncoderHardware& hw,
              Platform::ICriticalSection& critical,
              Platform::IMicrosecondClock& usClock,
              IEncoderProcessing& processor,
              IEncoderStatusSink& statusSink,
              uint8_t measWindowMs,
              uint16_t sampleRateHz);

    // IEncoderService (Encoder task)
    void sample() override;

    // IEncoder (dispatchers, Comms task)
    EncoderSnapshot getState() override;
    int32_t getCount() override;
    void resetCount() override;
    void clearIndexFlag() override;
    bool isAvailable() override;

    // IEncoderFilterControl (dispatchers, Comms task)
    void getConfig(IEncoderDispatcher::EncFilterParams& out) override;
    void setConfig(const IEncoderDispatcher::EncFilterParams& cfg) override;
    void setLegacy(uint8_t type, uint8_t param) override;

    /// Last raw (unfiltered) velocity in counts/s — for tests and diagnostics.
    int32_t lastRawVelocity() const { return m_lastRawVelocity; }

private:
    void setSampleRate(uint16_t hz);

    Drivers::IEncoderHardware*   m_hw = nullptr;
    Platform::ICriticalSection*  m_critical = nullptr;
    Platform::IMicrosecondClock* m_usClock = nullptr;
    IEncoderProcessing*          m_processor = nullptr;
    IEncoderStatusSink*          m_statusSink = nullptr;

    volatile EncoderSnapshot m_state = {};
    uint32_t m_lastDmaIdx = 0;
    uint32_t m_ackedIndexPulses = 0;   ///< pulses acknowledged by clearIndexFlag (Comms task writes, Encoder task reads)
    uint16_t m_sampleRateHz = DEFAULT_SAMPLE_RATE_HZ;
    uint8_t  m_measWindowMs = DEFAULT_MEAS_WINDOW_MS;
    int32_t  m_lastRawVelocity = 0;
};

extern EncoderService g_encoderService;

} // namespace Services
