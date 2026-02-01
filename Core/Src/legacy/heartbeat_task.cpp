/**
 * @file heartbeat_task.cpp
 * @brief Heartbeat task implementation
 */

#include "heartbeat_task.hpp"

// External global objects
extern LED *userLED;

void vHeartbeatTask(void *pvParameters) {
  (void)pvParameters;

  TickType_t lastWakeTime = xTaskGetTickCount();

  while (true) {
    userLED->toggle();
    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(500));
  }
}