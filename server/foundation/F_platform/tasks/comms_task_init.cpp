/**
 * @file comms_task_init.cpp
 * @brief Implements Harness::initCommsTask() — task + safety adapter + RTOS
 *
 * Composition-root-like: includes F_platform/tasks and harness interfaces.
 */

#include "F_platform/tasks/comms_task_init.hpp"
#include "harness/pins/ischeduler.hpp"
#include "harness/pins/isafety_actions.hpp"
#include "F_platform/tasks/comms_task.hpp"
#include "F_platform/tasks/motor_task.hpp"

#ifdef ENABLE_INTERFACE_TRACE
#include "harness/taps/traced_safety_actions.hpp"
#endif

// --- SafetyActionsImpl adapter (bridges harness ISafetyActions ↔ Scheduler:: task functions) ---

class SafetyActionsImpl : public Harness::ISafetyActions {
public:
    uint16_t getMotorStatusReg() override {
        Scheduler::MotorDebugParams info;
        if (Scheduler::MotorTask_GetDebugInfo(info)) {
            return info.status;
        }
        return 0;
    }

    void clearCommsTimeout() override {
        Scheduler::CommsTask_ClearCommsTimeout();
    }

    uint32_t setHeartbeatTimeout(uint32_t ms) override {
        return Scheduler::CommsTask_SetHeartbeatTimeout(ms);
    }

    void heartbeatReceived(uint32_t seq) override {
        Scheduler::CommsTask_HeartbeatReceived(seq);
    }

    void getHeartbeatStatus(bool& enabled, uint32_t& timeoutMs,
                             uint32_t& lastSeq, uint32_t& remainingMs,
                             bool& timedOut) override {
        Scheduler::CommsTask_GetHeartbeatStatus(
            enabled, timeoutMs, lastSeq, remainingMs, timedOut);
    }
};

// --- Global pin (composition root owns) ---
Harness::ISafetyActions* Harness::g_safetyActions = nullptr;

namespace Harness {

bool initCommsTask(ITransport& transport,
                   ICommandProcessor& parser,
                   IClock& clock,
                   IMotorEventReceiver& eventReceiver,
                   IEmergencyStop& emergencyStop,
                   IScheduler& scheduler,
                   CommsTaskResult& result)
{
    // Task init
    if (!Scheduler::CommsTask_Init(transport, parser, clock,
                                    eventReceiver, emergencyStop)) {
        return false;
    }

    // Safety actions adapter
    static SafetyActionsImpl safetyActions;
#ifdef ENABLE_INTERFACE_TRACE
    static Harness::TracedSafetyActions tracedSafety(safetyActions);
    Harness::g_safetyActions = &tracedSafety;
#else
    Harness::g_safetyActions = &safetyActions;
#endif

    // Joystick callback
    Scheduler::CommsTask_RegisterJoyCallback();

    // RTOS task creation
    auto* handle = scheduler.createTask(
        {"Comms", Scheduler::COMMS_TASK_STACK_SIZE,
         Scheduler::COMMS_TASK_PRIORITY,
         Scheduler::vCommsTask, nullptr});
    if (handle == nullptr) {
        return false;
    }

    // Return interfaces
    result.getEventSeqFn = &Scheduler::CommsTask_GetLastEventSeq;
    return true;
}

} // namespace Harness
