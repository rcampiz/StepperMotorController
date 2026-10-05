/**
 * @file display_task_init.cpp
 * @brief Implements Harness::initDisplayTask() — task creation only
 */

#include "F_platform/tasks/display_task_init.hpp"
#include "F_platform/tasks/display_task.hpp"
#include "F_platform/boundary/ischeduler.hpp"

namespace Scheduler {

bool initDisplayTask(Services::IDisplayService& service,
                     Platform::IClock& clock,
                     Platform::IScheduler& scheduler)
{
    if (!Scheduler::DisplayTask_Init(service, clock)) {
        return false;
    }
    Scheduler::g_displayTaskHandle = scheduler.createTask(
        {"Display", Scheduler::DISPLAY_TASK_STACK_SIZE,
         Scheduler::DISPLAY_TASK_PRIORITY,
         Scheduler::vDisplayTask, nullptr});
    return Scheduler::g_displayTaskHandle != nullptr;
}

} // namespace Scheduler
