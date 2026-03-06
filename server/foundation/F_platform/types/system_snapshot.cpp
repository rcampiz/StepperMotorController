/**
 * @file system_snapshot.cpp
 * @brief SystemSnapshot implementation — raw storage, lazy conversion
 */

#include "F_platform/types/system_snapshot.hpp"
#include "harness/pins/motor_conversions.hpp"
#include "harness/pins/imotor_driver.hpp"
#include <string.h>

bool SystemSnapshot::init(Harness::ILock& lock, Harness::IClock& clock) {
    m_lock = &lock;
    m_clock = &clock;

    m_motorStatusReg = 0;
    m_motorRawAbsPos = 0;
    m_motorRawSpeed = 0;
    memset(&m_encoder, 0, sizeof(m_encoder));
    memset(&m_control, 0, sizeof(m_control));
    memset(&m_system, 0, sizeof(m_system));
    m_timestamp = 0;

    return true;
}

Protocol::TelemetrySnapshot SystemSnapshot::getSnapshot() {
    Protocol::TelemetrySnapshot snap = {};

    m_lock->acquire();

    // Motor: convert raw → display
    Harness::MotorStatus mst{m_motorStatusReg};
    snap.motor.statusReg = m_motorStatusReg;
    snap.motor.position = Harness::signExtendAbsPos(m_motorRawAbsPos);
    snap.motor.speed = Harness::rawSpeedToStepsPerSec(m_motorRawSpeed);
    snap.motor.busy = mst.busy();
    snap.motor.hiZ = mst.hiZ();
    snap.motor.stalled = mst.stallA() || mst.stallB();
    snap.motor.targetPosition = 0;

    // Encoder, control, system: direct copy
    snap.encoder = m_encoder;
    snap.control = m_control;
    snap.system = m_system;
    snap.timestamp = m_timestamp;

    m_lock->release();

    return snap;
}

void SystemSnapshot::writeMotorStatus(uint16_t statusReg) {
    m_lock->acquire();
    m_motorStatusReg = statusReg;
    m_timestamp = m_clock->getTickMs();
    m_lock->release();
}

void SystemSnapshot::writeMotorParam(uint8_t reg, uint32_t value) {
    m_lock->acquire();
    if (reg == Harness::MotorReg::ABS_POS) {
        m_motorRawAbsPos = value;
    } else if (reg == Harness::MotorReg::SPEED) {
        m_motorRawSpeed = value;
    }
    m_timestamp = m_clock->getTickMs();
    m_lock->release();
}

void SystemSnapshot::writeEncoder(const Harness::EncoderTelemetryData& data) {
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

void SystemSnapshot::writeControl(const Protocol::ControlTelemetry& ctrl) {
    m_lock->acquire();
    m_control = ctrl;
    m_timestamp = m_clock->getTickMs();
    m_lock->release();
}

void SystemSnapshot::writeSystem(const Protocol::SystemTelemetry& sys) {
    m_lock->acquire();
    m_system = sys;
    m_timestamp = m_clock->getTickMs();
    m_lock->release();
}
