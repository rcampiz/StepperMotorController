/**
 * @file encoder_task.cpp
 * @brief Encoder task — thin sheet over Services::IEncoderService (H_TASKS_L3)
 */

#include "F_platform/tasks/encoder_task.hpp"
#include "harness/defs/harness_tasks_l3.hpp"
#include "F_platform/boundary/iclock.hpp"
#include "harness/pins/itrace_context.hpp"

namespace Scheduler {

static Services::IEncoderService* s_service = nullptr;
static Platform::IClock* s_clock = nullptr;

bool EncoderTask_Init(Services::IEncoderService& service, Platform::IClock& clock)
{
    s_service = &service;
    s_clock = &clock;
    return true;
}

void vEncoderTask(void* pvParameters)
{
    (void)pvParameters;
    if (s_service == nullptr || s_clock == nullptr) {
        while (true) {}  // Defensive halt — composition root guarantees valid state
    }

    uint32_t lastWakeMs = s_clock->getTickMs();
    while (true) {
        Harness::setTraceTaskId(Harness::TASK_ENCODER);
        Harness::setTraceServiceId(Harness::SVC_ENCODER);

        s_service->sample();

        s_clock->sleepUntilMs(lastWakeMs, ENCODER_SAMPLE_PERIOD_MS);
    }
}

} // namespace Scheduler
