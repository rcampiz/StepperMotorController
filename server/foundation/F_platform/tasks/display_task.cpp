/**
 * @file display_task.cpp
 * @brief Display task — thin sheet over Services::IDisplayService (H_TASKS_L3)
 */

#include "F_platform/tasks/display_task.hpp"
#include "harness/defs/harness_tasks_l3.hpp"
#include "F_platform/boundary/iclock.hpp"
#include "harness/pins/itrace_context.hpp"
#include "harness/trace/harness_meter.hpp"

namespace Scheduler {

static Services::IDisplayService* s_service = nullptr;
static Platform::IClock* s_clock = nullptr;

Platform::TaskHandle g_displayTaskHandle = nullptr;

bool DisplayTask_Init(Services::IDisplayService& service, Platform::IClock& clock)
{
    s_service = &service;
    s_clock = &clock;
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

        {
            Harness::Meter::Scope crossing(Harness::Meter::TASKS_DISPLAY);   // H_TASKS_L3
            s_service->tick();
        }

        // A long tick (a flash save, a full repaint, a bounded wait on the Motor task) must
        // not be followed by the missed periods fired back to back: vTaskDelayUntil catches
        // up, so every frame-counted animation ran several times too fast (2026-10-19, the
        // selection bar rocking fast enough to make someone dizzy). Late: the next frame
        // starts now, after one tick for the lower-priority work, with the period counted
        // from here. (It used to sleep a whole period after a late frame: every frame over
        // 50 ms took 100, bench 2026-10-21: 48.5 ms frames ran at 14 fps, 55.8 ms at 9.9.)
        const uint32_t nowMs = s_clock->getTickMs();
        if (nowMs - lastWakeMs >= DISPLAY_REFRESH_PERIOD_MS) {
            lastWakeMs = nowMs;
            s_clock->delayMs(1);
            continue;
        }
        s_clock->sleepUntilMs(lastWakeMs, DISPLAY_REFRESH_PERIOD_MS);
    }
}

} // namespace Scheduler
