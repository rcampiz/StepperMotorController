/**
 * @file telemetry.cpp
 * @brief Telemetry manager implementation
 */

#include "L2_protocol/telemetry.hpp"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/task.h"
#include <string.h>

namespace Comms {

// Global instance
TelemetryManager g_telemetry;

bool TelemetryManager::init() {
  m_mutex = xSemaphoreCreateMutex();
  if (m_mutex == nullptr) {
    return false;
  }

  // Initialize data to zero
  memset(&m_data, 0, sizeof(m_data));
  return true;
}

void TelemetryManager::updateMotor(const MotorTelemetry &data) {
  if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    m_data.motor = data;
    m_data.timestamp = xTaskGetTickCount();
    xSemaphoreGive(m_mutex);
  }
}

void TelemetryManager::updateEncoder(const EncoderTelemetry &data) {
  if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    m_data.encoder = data;
    m_data.timestamp = xTaskGetTickCount();
    xSemaphoreGive(m_mutex);
  }
}

void TelemetryManager::updateSystem(const SystemTelemetry &data) {
  if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    m_data.system = data;
    m_data.timestamp = xTaskGetTickCount();
    xSemaphoreGive(m_mutex);
  }
}

void TelemetryManager::updateControl(const ControlTelemetry &data) {
  if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    m_data.control = data;
    m_data.timestamp = xTaskGetTickCount();
    xSemaphoreGive(m_mutex);
  }
}

TelemetrySnapshot TelemetryManager::getSnapshot() {
  TelemetrySnapshot snapshot = {};

  if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    snapshot = m_data;
    xSemaphoreGive(m_mutex);
  }

  return snapshot;
}

} // namespace Comms
