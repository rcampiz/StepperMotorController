/**
 * @file telemetry_store.hpp
 * @brief L3 TelemetryStore — the shared telemetry snapshot (moved from F_platform/types/system_snapshot)
 *
 * Written by MotorService (motor status/params, system), EncoderService (encoder) and
 * MotorController (control loop); read by CommsService, the system dispatcher and the UI
 * screens. Protected by a Platform mutex (power net). Task context only.
 */
#pragma once

#include "L3_services/boundary/itelemetry.hpp"
#include "L3_services/boundary/isnapshot_writer.hpp"
#include "F_platform/boundary/ilock.hpp"
#include "F_platform/boundary/iclock.hpp"

namespace Services {

class TelemetryStore : public ITelemetry, public ISnapshotWriter {
public:
    bool init(Platform::ILock& lock, Platform::IClock& clock);

    // ITelemetry
    Protocol::TelemetrySnapshot getSnapshot() override;

    // ISnapshotWriter
    void writeMotorStatus(uint16_t statusReg) override;
    void writeMotorParam(uint8_t reg, uint32_t value) override;
    void writeEncoder(const EncoderTelemetryData& data) override;
    void writeControl(const Protocol::ControlTelemetry& ctrl) override;
    void writeSystem(const Protocol::SystemTelemetry& sys) override;

private:
    uint16_t m_motorStatusReg = 0;
    uint32_t m_motorRawAbsPos = 0;
    uint32_t m_motorRawSpeed = 0;
    Protocol::EncoderTelemetry m_encoder = {};
    Protocol::ControlTelemetry m_control = {};
    Protocol::SystemTelemetry m_system = {};
    uint32_t m_timestamp = 0;
    Platform::ILock* m_lock = nullptr;
    Platform::IClock* m_clock = nullptr;
};

extern TelemetryStore g_telemetryStore;

} // namespace Services
