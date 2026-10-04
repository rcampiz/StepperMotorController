/**
 * @file telemetry.hpp
 * @brief TelemetryManager — thread-safe telemetry aggregator
 *
 * Type definitions are in harness/pins/telemetry_types.hpp.
 * This file provides the implementation (TelemetryManager).
 */

#ifndef TELEMETRY_HPP
#define TELEMETRY_HPP

#include "harness/pins/itelemetry.hpp"
#include "harness/pins/ilock.hpp"
#include "harness/pins/iclock.hpp"

namespace Protocol {

class TelemetryManager : public Scheduler::ITelemetry {
public:
    bool init(Platform::ILock& lock, Platform::IClock& clock);

    void updateMotor(const MotorTelemetry& data) override;
    void updateEncoder(const EncoderTelemetry& data) override;
    void updateSystem(const SystemTelemetry& data) override;
    void updateControl(const ControlTelemetry& data) override;
    TelemetrySnapshot getSnapshot() override;

private:
    TelemetrySnapshot m_data;
    Platform::ILock* m_lock;
    Platform::IClock* m_clock;
};

extern TelemetryManager g_telemetry;

} // namespace Protocol

#endif // TELEMETRY_HPP
