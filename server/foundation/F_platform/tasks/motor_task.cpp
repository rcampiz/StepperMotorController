/**
 * @file motor_task.cpp
 * @brief Motor control task — scheduler-neutral
 *
 * All dependencies injected via MotorTask_Init().
 * Hardware construction lives in wiring/wire_motor.cpp.
 * All business logic lives in Services::MotorController.
 */

#include "F_platform/tasks/motor_task.hpp"
#include "harness/pins/imotor_task_control.hpp"
#include "harness/pins/imotor_control_service.hpp"
#include "harness/pins/isnapshot_writer.hpp"
#include "harness/pins/itask_stats.hpp"
#include <stdint.h>
#include "harness/pins/itrace_context.hpp"

namespace Scheduler {

// Injected dependencies (set by MotorTask_Init)
static Harness::IMotorDriver* s_driver = nullptr;
static Harness::IQueue<Harness::MotorCommand, MOTOR_CMD_QUEUE_DEPTH>* s_cmdQueue = nullptr;
static Harness::IClock* s_clock = nullptr;
static Harness::IMotorControlService* s_controller = nullptr;
static Harness::IScheduler* s_scheduler = nullptr;

// Task handle for suspend/resume (opaque, managed by IScheduler)
Harness::TaskHandle g_motorTaskHandle = nullptr;

bool MotorTask_Init(Harness::IMotorDriver& driver,
                    Harness::IQueue<Harness::MotorCommand, MOTOR_CMD_QUEUE_DEPTH>& cmdQueue,
                    Harness::IClock& clock,
                    Harness::IMotorControlService& controller,
                    Harness::IScheduler& scheduler)
{
    s_driver = &driver;
    s_cmdQueue = &cmdQueue;
    s_clock = &clock;
    s_controller = &controller;
    s_scheduler = &scheduler;

    // Apply configuration to the driver
    MotorTask_ApplyConfig();

    return true;
}

void vMotorTask(void* pvParameters)
{
    (void)pvParameters;

    if (s_driver == nullptr) {
        while (true) {}  // Defensive halt — composition root guarantees non-null
    }

    s_clock->delayMs(100);

    MotorCommand cmd;
    constexpr uint32_t STATUS_UPDATE_MS = 50;
    uint32_t lastStatusUpdate = 0;

    while (true) {
        Harness::setTraceTaskId(Harness::TASK_MOTOR);
        Harness::setTraceServiceId(Harness::SVC_MOTION);

        if (s_cmdQueue->receive(cmd, STATUS_UPDATE_MS)) {
            s_controller->processCommand(cmd, *s_driver);

            // GetStatus forces immediate periodic update
            if (cmd.type == MotorCmdType::GetStatus) {
                lastStatusUpdate = 0;
            }
        }

        uint32_t now = s_clock->getTickMs();
        if ((now - lastStatusUpdate) >= STATUS_UPDATE_MS) {
            lastStatusUpdate = now;
            s_controller->periodicUpdate(*s_driver);

            // Populate system telemetry
            Protocol::SystemTelemetry sys = {};
            sys.uptimeTicks = now;
            Harness::ITaskStats* stats = Harness::taskStatsProvider();
            sys.freeHeap = (stats != nullptr) ? stats->getFreeHeapBytes() : 0;
            sys.cpuLoad = 0;
            Harness::snapshotWriter().writeSystem(sys);
        }
    }
}

bool MotorTask_SendCommand(const MotorCommand& cmd, uint32_t timeout)
{
    if (s_cmdQueue == nullptr) {
        return false;
    }
    return s_cmdQueue->send(cmd, timeout);
}

bool MotorTask_Move(int32_t steps)
{
    MotorCommand cmd = {};
    cmd.type = MotorCmdType::Move;
    cmd.param1 = steps;
    return MotorTask_SendCommand(cmd);
}

bool MotorTask_Stop(bool hard)
{
    MotorCommand cmd = {};
    cmd.type = hard ? MotorCmdType::HardStop : MotorCmdType::SoftStop;
    return MotorTask_SendCommand(cmd);
}

void MotorTask_Suspend()
{
    s_scheduler->suspendTask(g_motorTaskHandle);
}

void MotorTask_Resume()
{
    s_scheduler->resumeTask(g_motorTaskHandle);
}

bool MotorTask_GetDebugInfo(MotorDebugParams& info)
{
    if (s_driver == nullptr) {
        return false;
    }
    s_controller->getDebugInfo(*s_driver, info);
    return true;
}

bool MotorTask_ApplyConfig()
{
    if (s_driver == nullptr) {
        return false;
    }
    s_controller->applyConfig(*s_driver);
    return true;
}

bool MotorTask_SetStepModeSafe(uint8_t mode, uint8_t& readback)
{
    if (s_driver == nullptr || mode > 7) {
        readback = 0xFF;
        return false;
    }

    // Hi-Z + delay before register write
    s_driver->hardHiZ();
    s_clock->delayMs(5);

    return s_controller->setStepModeSafe(*s_driver, mode, readback);
}

void MotorTask_Reinit()
{
    if (s_driver != nullptr) {
        s_controller->reinit(*s_driver);
    }
}

// ============================================================================
// Harness interface implementation (eliminates wiring adapter)
// ============================================================================

class MotorTaskAccess : public Harness::IMotorTaskControl {
public:
    void reinit() override { MotorTask_Reinit(); }
    bool applyConfig() override { return MotorTask_ApplyConfig(); }
    bool getDebugInfo(Harness::MotorDebugParams& out) override {
        return MotorTask_GetDebugInfo(out);
    }
    bool setStepModeSafe(uint8_t mode, uint8_t& readback) override {
        MotorTask_Suspend();
        bool ok = MotorTask_SetStepModeSafe(mode, readback);
        MotorTask_Resume();
        return ok;
    }
};

static MotorTaskAccess s_motorAccess;

Harness::IMotorTaskControl* MotorTask_GetControlInterface() { return &s_motorAccess; }

} // namespace Scheduler
