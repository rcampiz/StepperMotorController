/**
 * @file telemetry.cpp
 * @brief Telemetry manager implementation
 */

#include "F_platform/interfaces/telemetry.hpp"
#include <string.h>

namespace Comms {

// Global instance
TelemetryManager g_telemetry;

bool TelemetryManager::init(ILock& lock, IClock& clock) {
  m_lock = &lock;
  m_clock = &clock;

  // Initialize data to zero
  memset(&m_data, 0, sizeof(m_data));
  return true;
}

void TelemetryManager::updateMotor(const MotorTelemetry &data) {
  m_lock->acquire();
  m_data.motor = data;
  m_data.timestamp = m_clock->getTickMs();
  m_lock->release();
}

void TelemetryManager::updateEncoder(const EncoderTelemetry &data) {
  m_lock->acquire();
  m_data.encoder = data;
  m_data.timestamp = m_clock->getTickMs();
  m_lock->release();
}

void TelemetryManager::updateSystem(const SystemTelemetry &data) {
  m_lock->acquire();
  m_data.system = data;
  m_data.timestamp = m_clock->getTickMs();
  m_lock->release();
}

void TelemetryManager::updateControl(const ControlTelemetry &data) {
  m_lock->acquire();
  m_data.control = data;
  m_data.timestamp = m_clock->getTickMs();
  m_lock->release();
}

TelemetrySnapshot TelemetryManager::getSnapshot() {
  TelemetrySnapshot snapshot = {};

  m_lock->acquire();
  snapshot = m_data;
  m_lock->release();

  return snapshot;
}

} // namespace Comms
