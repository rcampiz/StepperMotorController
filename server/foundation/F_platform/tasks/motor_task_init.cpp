/**
 * @file motor_task_init.cpp
 * @brief Implements Harness::initMotorTask()
 *
 * Thin wrapper around Scheduler::MotorTask_Init() + scheduler->createTask().
 */

#include "F_platform/tasks/motor_task_init.hpp"
#include "F_platform/tasks/motor_task.hpp"
#include "harness/harness_wiring_map.hpp"

namespace Harness {

bool initMotorTask(Services::IMotorDriver& driver,
                   Platform::IQueue<Scheduler::MotorCommand, MOTOR_CMD_QUEUE_DEPTH>& cmdQueue,
                   Platform::IClock& clock,
                   Services::IMotorControlService& controller,
                   Platform::IScheduler& scheduler,
                   MotorTaskResult& result)
{
    auto& controllerPort = Harness::adaptMotorControlService(controller);
    if (!Scheduler::MotorTask_Init(driver, cmdQueue, clock, controllerPort, scheduler)) {
        return false;
    }

    Scheduler::g_motorTaskHandle = scheduler.createTask(
        {"Motor", Scheduler::MOTOR_TASK_STACK_SIZE,
         Scheduler::MOTOR_TASK_PRIORITY,
         Scheduler::vMotorTask, nullptr});

    if (Scheduler::g_motorTaskHandle == nullptr) {
        return false;
    }

    result.motorCtrl = Scheduler::MotorTask_GetControlInterface();
    return true;
}

} // namespace Harness
