/**
 * @file system_snapshot.hpp
 * @brief SystemSnapshot — tap-populated telemetry aggregator
 *
 * Replaces TelemetryManager. Stores raw motor register values (written
 * by TracedMotorDriver tap), converts to display units lazily on read.
 * Encoder/control/system data stored as-is (no conversion needed).
 */

#pragma once

#include "harness/pins/itelemetry.hpp"
#include "harness/pins/isnapshot_writer.hpp"
#include "harness/pins/ilock.hpp"
#include "harness/pins/iclock.hpp"

class SystemSnapshot : public Harness::ITelemetry, public Harness::ISnapshotWriter {
public:
    bool init(Harness::ILock& lock, Harness::IClock& clock);

    // ITelemetry (read) — converts motor raw on the fly
    Protocol::TelemetrySnapshot getSnapshot() override;

    // ISnapshotWriter (write) — taps and services call these
    void writeMotorStatus(uint16_t statusReg) override;
    void writeMotorParam(uint8_t reg, uint32_t value) override;
    void writeEncoder(const Harness::EncoderTelemetryData& data) override;
    void writeControl(const Protocol::ControlTelemetry& ctrl) override;
    void writeSystem(const Protocol::SystemTelemetry& sys) override;

private:
    // Raw motor storage (written by TracedMotorDriver tap)
    uint16_t m_motorStatusReg = 0;
    uint32_t m_motorRawAbsPos = 0;
    uint32_t m_motorRawSpeed = 0;

    // Processed storage (no conversion needed on read)
    Protocol::EncoderTelemetry m_encoder = {};
    Protocol::ControlTelemetry m_control = {};
    Protocol::SystemTelemetry m_system = {};

    uint32_t m_timestamp = 0;
    Harness::ILock* m_lock = nullptr;
    Harness::IClock* m_clock = nullptr;
};
