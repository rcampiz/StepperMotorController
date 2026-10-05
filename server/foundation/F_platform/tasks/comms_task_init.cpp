/**
 * @file comms_task_init.cpp
 * @brief Implements Harness::initCommsTask() — task creation only
 */

#include "F_platform/tasks/comms_task_init.hpp"
#include "F_platform/tasks/comms_task.hpp"
#include "F_platform/boundary/ischeduler.hpp"

namespace Scheduler {

bool initCommsTask(Services::ICommsService& service,
                   Platform::IClock& clock,
                   Platform::IScheduler& scheduler,
                   Platform::ITaskNotifier* rxWake)
{
    if (!Scheduler::CommsTask_Init(service, clock, rxWake)) {
        return false;
    }
    Scheduler::g_commsTaskHandle = scheduler.createTask(
        {"Comms", Scheduler::COMMS_TASK_STACK_SIZE,
         Scheduler::COMMS_TASK_PRIORITY,
         Scheduler::vCommsTask, nullptr});
    return Scheduler::g_commsTaskHandle != nullptr;
}

} // namespace Scheduler
