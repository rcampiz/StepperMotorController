/**
 * @file motor_task.cpp
 * @brief Motor task — thin sheet over Services::IMotorService (H_TASKS_L3)
 */

#include "F_platform/tasks/motor_task.hpp"
#include "harness/defs/harness_tasks_l3.hpp"
#include "F_platform/boundary/iclock.hpp"
#include "harness/pins/itrace_context.hpp"
#include "harness/trace/harness_meter.hpp"

namespace Scheduler {

static Services::IMotorService* s_service = nullptr;
static MotorCmdQueue* s_cmdQueue = nullptr;
static Platform::IClock* s_clock = nullptr;

Platform::TaskHandle g_motorTaskHandle = nullptr;

bool MotorTask_Init(Services::IMotorService& service,
                    MotorCmdQueue& cmdQueue,
                    Platform::IClock& clock)
{
    s_service = &service;
    s_cmdQueue = &cmdQueue;
    s_clock = &clock;
    return true;
}

void vMotorTask(void* pvParameters)
{
    (void)pvParameters;
    if (s_service == nullptr || s_cmdQueue == nullptr || s_clock == nullptr) {
        while (true) {}  // Defensive halt — composition root guarantees non-null
    }

    s_clock->delayMs(100);   // let the rest of the system settle

    MotorCommand cmd;
    uint32_t lastStatusUpdate = 0;

    while (true) {
        Harness::setTraceTaskId(Harness::TASK_MOTOR);
        Harness::setTraceServiceId(Harness::SVC_MOTION);

        // Wait for a command at most until the next poll is due (a whole period after a pass
        // made the poll period the period plus the pass: 87 polls/s at 10 ms, bench 2026-10-21)
        const uint32_t sinceMs = s_clock->getTickMs() - lastStatusUpdate;
        const uint32_t waitMs = (sinceMs >= MOTOR_STATUS_PERIOD_MS) ? 0u : MOTOR_STATUS_PERIOD_MS - sinceMs;
        if (s_cmdQueue->receive(cmd, waitMs)) {
            {
                Harness::Meter::Scope crossing(Harness::Meter::TASKS_MOTOR);   // H_TASKS_L3
                s_service->onCommand(cmd);
            }
            if (cmd.type == MotorCmdType::GetStatus) {
                lastStatusUpdate = 0;   // force an immediate status poll
            }
        }

        uint32_t now = s_clock->getTickMs();
        if ((now - lastStatusUpdate) >= MOTOR_STATUS_PERIOD_MS) {
            lastStatusUpdate = now;
            Harness::Meter::Scope crossing(Harness::Meter::TASKS_MOTOR);       // H_TASKS_L3
            s_service->periodicUpdate();
        }
    }
}

} // namespace Scheduler
