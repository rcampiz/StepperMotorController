/**
 * @file display_task_init.cpp
 * @brief Implements Harness::initDisplayTask() — task + RTOS creation
 *
 * Thin wrapper: calls Scheduler::DisplayTask_Init + createTask.
 * All dependencies arrive as harness interfaces — no L3/L4 includes.
 */

#include "F_platform/tasks/display_task_init.hpp"
#include "harness/pins/ischeduler.hpp"
#include "F_platform/tasks/display_task.hpp"

namespace Harness {

bool initDisplayTask(ICanvas& lcd,
                     IJoystick* joystick,
                     IClock& clock,
                     IFlashImageAccess& flashImages,
                     IIndicatorRenderer& indicator,
                     IScheduler& scheduler,
                     DisplayTaskResult& result)
{
    // Task init
    if (!Scheduler::DisplayTask_Init(lcd, joystick, clock,
                                      flashImages, indicator, scheduler)) {
        return false;
    }

    // RTOS task creation
    Scheduler::g_displayTaskHandle = scheduler.createTask(
        {"Display", Scheduler::DISPLAY_TASK_STACK_SIZE,
         Scheduler::DISPLAY_TASK_PRIORITY,
         Scheduler::vDisplayTask, nullptr});

    // Return harness interfaces for dispatch wiring
    result.remoteDisplay = Scheduler::DisplayTask_GetRemoteInterface();
    return true;
}

} // namespace Harness
