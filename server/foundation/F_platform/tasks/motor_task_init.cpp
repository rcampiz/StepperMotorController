/**
 * @file motor_task_init.cpp
 * @brief Implements Harness::initMotorTask() — task creation only
 */

#include "F_platform/tasks/motor_task_init.hpp"
#include "F_platform/tasks/motor_task.hpp"
#include "F_platform/boundary/ischeduler.hpp"

namespace Harness {

bool initMotorTask(Services::IMotorService& service,
                   Platform::IQueue<Services::MotorCommand, MOTOR_CMD_QUEUE_DEPTH>& cmdQueue,
                   Platform::IClock& clock,
                   Platform::IScheduler& scheduler)
{
    if (!Scheduler::MotorTask_Init(service, cmdQueue, clock)) {
        return false;
    }
    Scheduler::g_motorTaskHandle = scheduler.createTask(
        {"Motor", Scheduler::MOTOR_TASK_STACK_SIZE,
         Scheduler::MOTOR_TASK_PRIORITY,
         Scheduler::vMotorTask, nullptr});
    return Scheduler::g_motorTaskHandle != nullptr;
}

} // namespace Harness
