/**
 * @file display_task.cpp
 * @brief Display task — thin sheet over Services::IDisplayService (H_TASKS_L3)
 */

#include "F_platform/tasks/display_task.hpp"
#include "harness/defs/harness_tasks_l3.hpp"
#include "F_platform/boundary/iclock.hpp"
#include "harness/pins/itrace_context.hpp"

namespace Scheduler {

static Services::IDisplayService* s_service = nullptr;
static Platform::IClock* s_clock = nullptr;
static Platform::IScheduler* s_scheduler = nullptr;

Platform::TaskHandle g_displayTaskHandle = nullptr;

bool DisplayTask_Init(Services::IDisplayService& service,
                      Platform::IClock& clock,
                      Platform::IScheduler& scheduler)
{
    s_service = &service;
    s_clock = &clock;
    s_scheduler = &scheduler;
    return true;
}

void vDisplayTask(void* pvParameters)
{
    (void)pvParameters;
    if (s_service == nullptr || s_clock == nullptr) {
        while (true) {}  // Defensive halt — composition root guarantees valid state
    }

    uint32_t lastWakeMs = s_clock->getTickMs();
    while (true) {
        Harness::setTraceTaskId(Harness::TASK_DISPLAY);
        Harness::setTraceServiceId(Harness::SVC_UI);

        s_service->tick();

        s_clock->sleepUntilMs(lastWakeMs, DISPLAY_REFRESH_PERIOD_MS);
    }
}

void DisplayTask_Suspend()
{
    if (s_scheduler != nullptr && g_displayTaskHandle != nullptr) {
        s_scheduler->suspendTask(g_displayTaskHandle);
    }
}

void DisplayTask_Resume()
{
    if (s_scheduler != nullptr && g_displayTaskHandle != nullptr) {
        s_scheduler->resumeTask(g_displayTaskHandle);
    }
}

} // namespace Scheduler
