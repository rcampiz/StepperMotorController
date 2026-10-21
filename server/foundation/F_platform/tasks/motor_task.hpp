/**
 * @file motor_task.hpp
 * @brief Motor task — thin sheet: receive from the Platform queue, call Services::IMotorService
 *
 * All motor logic lives in L3 MotorService (H_TASKS_L3). The task keeps its handle so L3
 * can suspend/resume it through the Platform scheduler.
 */
#ifndef MOTOR_TASK_HPP
#define MOTOR_TASK_HPP

#include "harness/pins/motor_command.hpp"
#include "F_platform/boundary/iqueue.hpp"
#include "F_platform/boundary/ischeduler.hpp"
#include "F_platform/tasks/task_stacks.hpp"
#include <stdint.h>

namespace Platform { class IClock; }
namespace Services { class IMotorService; }

namespace Scheduler {

// MOTOR_TASK_STACK_SIZE: tasks/task_stacks.hpp (all the stack sizes, in one place)
constexpr uint32_t MOTOR_TASK_PRIORITY = 4;
// periodicUpdate cadence / queue timeout: the driver's state and the encoder count read with it
// (telemetry, fault / stall / motion-done edges). The control laws inside run on their own 50 ms
// clock (MotorController::CONTROL_PERIOD_MS). Was 50 until 2026-10-21; bench at 5 ms with the
// LCD streaming: 199 polls/s, the LCD at 17.7 frames/s (14.1 before, at 50 ms).
constexpr uint32_t MOTOR_STATUS_PERIOD_MS = 5;

using MotorCmdQueue = Platform::IQueue<MotorCommand, Harness::MOTOR_CMD_QUEUE_DEPTH>;

bool MotorTask_Init(Services::IMotorService& service,
                    MotorCmdQueue& cmdQueue,
                    Platform::IClock& clock);
void vMotorTask(void* pvParameters);

extern Platform::TaskHandle g_motorTaskHandle;

} // namespace Scheduler

#endif // MOTOR_TASK_HPP
