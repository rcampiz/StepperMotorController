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
#include <stdint.h>

namespace Platform { class IClock; }
namespace Services { class IMotorService; }

namespace Scheduler {

constexpr uint32_t MOTOR_TASK_STACK_SIZE = 1024;  // words
constexpr uint32_t MOTOR_TASK_PRIORITY = 4;
constexpr uint32_t MOTOR_STATUS_PERIOD_MS = 50;   // periodicUpdate cadence / queue timeout

using MotorCmdQueue = Platform::IQueue<MotorCommand, Harness::MOTOR_CMD_QUEUE_DEPTH>;

bool MotorTask_Init(Services::IMotorService& service,
                    MotorCmdQueue& cmdQueue,
                    Platform::IClock& clock);
void vMotorTask(void* pvParameters);

extern Platform::TaskHandle g_motorTaskHandle;

} // namespace Scheduler

#endif // MOTOR_TASK_HPP
