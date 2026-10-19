/**
 * @file comms_task.cpp
 * @brief Comms task — thin sheet over Services::ICommsService (H_TASKS_L3)
 */

#include "F_platform/tasks/comms_task.hpp"
#include "harness/defs/harness_tasks_l3.hpp"
#include "F_platform/boundary/iclock.hpp"
#include "F_platform/boundary/itask_notifier.hpp"
#include "harness/pins/itrace_context.hpp"
#include "harness/trace/harness_meter.hpp"

namespace Scheduler {

static Services::ICommsService* s_service = nullptr;
static Platform::IClock* s_clock = nullptr;
static Platform::ITaskNotifier* s_rxWake = nullptr;

Platform::TaskHandle g_commsTaskHandle = nullptr;

bool CommsTask_Init(Services::ICommsService& service, Platform::IClock& clock,
                    Platform::ITaskNotifier* rxWake)
{
    s_service = &service;
    s_clock = &clock;
    s_rxWake = rxWake;
    return true;
}

void vCommsTask(void* pvParameters)
{
    (void)pvParameters;
    if (s_service == nullptr || s_clock == nullptr) {
        while (true) {}  // Defensive halt — composition root guarantees valid state
    }

    s_clock->delayMs(200);   // wait for system startup
    s_service->onStart();

    uint32_t lastWakeMs = s_clock->getTickMs();
    while (true) {
        Harness::setTraceTaskId(Harness::TASK_COMMS);
        Harness::setTraceServiceId(Harness::SVC_COMMS);

        {
            Harness::Meter::Scope crossing(Harness::Meter::TASKS_COMMS);     // H_TASKS_L3
            s_service->tick();
        }

        // Wake early on USART2 RX (ITaskNotifier), otherwise poll every COMMS_POLL_PERIOD_MS
        if (s_rxWake != nullptr) {
            s_rxWake->waitForNotification(COMMS_POLL_PERIOD_MS);
        } else {
            s_clock->sleepUntilMs(lastWakeMs, COMMS_POLL_PERIOD_MS);
        }
    }
}

} // namespace Scheduler
