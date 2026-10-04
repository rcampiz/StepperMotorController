/**
 * @file display_task_init.cpp
 * @brief Implements Harness::initDisplayTask() — task + RTOS creation
 *
 * Thin wrapper: calls Scheduler::DisplayTask_Init + createTask.
 * All dependencies arrive as harness interfaces — no L3/L4 includes.
 */

#include "F_platform/tasks/display_task_init.hpp"
#include "harness/pins/ischeduler.hpp"
#include "harness/harness_wiring_map.hpp"
#include "F_platform/tasks/display_task.hpp"

namespace Harness {

bool initDisplayTask(Services::ICanvas& lcd,
                     Services::IJoystick* joystick,
                     Platform::IClock& clock,
                     Services::IFlashImageAccess& flashImages,
                     Services::IIndicatorRenderer& indicator,
                     Platform::IScheduler& scheduler,
                     DisplayTaskResult& result)
{
    // Adapt provider types to consumer ports
    auto& flashImagesPort = Harness::adaptFlashImageAccess(flashImages);
    auto& indicatorPort = Harness::adaptIndicatorRenderer(indicator);

    // Task init
    if (!Scheduler::DisplayTask_Init(lcd, joystick, clock,
                                      flashImagesPort, indicatorPort, scheduler)) {
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
