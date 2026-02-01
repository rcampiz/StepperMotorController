/**
 * @file heartbeat_task.hpp
 * @brief Heartbeat task for FreeRTOS
 *
 * Blinks onboard LED to indicate system is running.
 */

#ifndef HEARTBEAT_TASK_HPP
#define HEARTBEAT_TASK_HPP

#include "FreeRTOS.h"
#include "task.h"
#include "led.hpp"

/**
 * @brief Heartbeat task - blinks onboard LED
 *
 * Toggles LED every 500ms to indicate system is running.
 *
 * @param pvParameters Task parameters (expects pointer to LED object)
 */
void vHeartbeatTask(void *pvParameters);

#endif // HEARTBEAT_TASK_HPP