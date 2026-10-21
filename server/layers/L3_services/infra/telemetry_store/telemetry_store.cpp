/**
 * @file telemetry_store.cpp
 * @brief L3 TelemetryStore implementation (moved from F_platform/types/system_snapshot.cpp)
 */

#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include <string.h>

namespace Services {

TelemetryStore g_telemetryStore;

bool TelemetryStore::init(Platform::ILock& lock, Platform::IClock& clock)
{
    m_lock = &lock;
    m_clock = &clock;
    m_motorStatus = Harness::MotorState::standby();
    m_encoderAtPosition = 0;
    memset(&m_encoder, 0, sizeof(m_encoder));
    memset(&m_control, 0, sizeof(m_control));
    memset(&m_system, 0, sizeof(m_system));
    m_timestamp = 0;
    return true;
}

Protocol::TelemetrySnapshot TelemetryStore::getSnapshot()
{
    Protocol::TelemetrySnapshot snap = {};

    m_lock->acquire();

    // Motor: the driver's state, physical (microsteps, full steps/s); no register decoding here
    const Harness::MotorState& mst = m_motorStatus;
    snap.motor.statusReg = mst.raw;
    snap.motor.faults = mst.faults;
    snap.motor.phase = static_cast<uint8_t>(mst.phase);
    snap.motor.forward = mst.forward;
    snap.motor.statusReadable = mst.readable;
    snap.motor.position = mst.positionUsteps;
    snap.motor.encoderAtPosition = m_encoderAtPosition;
    // Whole steps/s, truncated (the driver keeps the integer part exact)
    snap.motor.speed = (mst.speedFsps > 0.0f) ? static_cast<uint32_t>(mst.speedFsps) : 0u;
    snap.motor.busy = mst.busy();
    snap.motor.hiZ = mst.hiZ();
    snap.motor.stalled = mst.stalled();
    snap.motor.targetPosition = 0;

    // Encoder, control, system: direct copy
    snap.encoder = m_encoder;
    snap.control = m_control;
    snap.system = m_system;
    snap.timestamp = m_timestamp;

    m_lock->release();
    return snap;
}

void TelemetryStore::writeMotorStatus(const Harness::MotorState& status, int64_t encoderAtPosition)
{
    m_lock->acquire();
    m_motorStatus = status;
    m_encoderAtPosition = encoderAtPosition;
    m_timestamp = m_clock->getTickMs();
    m_lock->release();
}

void TelemetryStore::writeEncoder(const EncoderTelemetryData& data)
{
    m_lock->acquire();
    m_encoder.count = data.count;
    m_encoder.velocity = data.velocity;
    m_encoder.indexSeen = data.indexSeen;
    m_encoder.indexTick = data.indexTick;
    m_encoder.revolutions = data.revolutions;
    m_encoder.indexPeriodUs = data.indexPeriodUs;
    m_encoder.velocityQuality = data.velocityQuality;
    m_encoder.filterFlags = data.filterFlags;
    m_encoder.measWindowMs = data.measWindowMs;
    m_encoder.sampleRateHz = data.sampleRateHz;
    m_timestamp = m_clock->getTickMs();
    m_lock->release();
}

void TelemetryStore::writeControl(const Protocol::ControlTelemetry& ctrl)
{
    m_lock->acquire();
    m_control = ctrl;
    m_timestamp = m_clock->getTickMs();
    m_lock->release();
}

void TelemetryStore::writeSystem(const Protocol::SystemTelemetry& sys)
{
    m_lock->acquire();
    m_system = sys;
    m_timestamp = m_clock->getTickMs();
    m_lock->release();
}

} // namespace Services
