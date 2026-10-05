/**
 * @file motor_service_init.cpp
 * @brief Implements Services::initMotorSubsystem() — L3 drives the motor init cascade
 *
 * The L4 motor driver and config store arrive through H_L3_L4; MotorController owns the
 * driver; MotorService owns the Motor task's work and is created through the task init
 * contract (H_TASKS_L3). Platform primitives are injected.
 */

#include "L3_services/motion/motor_service_init.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "harness/pins/motor_task_init.hpp"
#include "L3_services/motion/motion.hpp"
#include "L3_services/motion/motor_service/motor_service.hpp"
#include "L3_services/dispatch/command_queue/command_queue.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"

namespace Services {

bool initMotorSubsystem(
    Platform::IQueue<MotorCommand, Harness::MOTOR_CMD_QUEUE_DEPTH>& commandQueue,
    Platform::IClock& clock,
    Platform::IScheduler& scheduler,
    Platform::ILock& commandQueueLock,
    Platform::ITaskStats* taskStats,
    MotorSubsystemResult& result)
{
    // L4 devices through H_L3_L4 (the harness runs the L4 -> L5 init cascade)
    const Harness::L3L4Harness& h = Harness::connectL3L4(clock);
    if (h.motor == nullptr || h.configStore == nullptr) {
        return false;
    }

    // L3 owns the driver from here on
    motion.stepper.controller.bind(*h.motor, clock);

    // L3 config (persistent blob via the L4 config store)
    motion.configStore.init(*h.configStore);

    // Motor service (task body, command sink, task control, immediate stop)
    g_motorService.init(motion.stepper.controller, motion.stepper.controller,
                        commandQueue, clock, scheduler, taskStats, g_telemetryStore);

    // Motor task (through harness -> F_platform)
    if (!Harness::initMotorTask(g_motorService, commandQueue, clock, scheduler)) {
        return false;
    }
    result.motorCtrl = &g_motorService;

    // L3 command queue (ARM/START): bounded sends, immediate stop for ESTOP
    g_commandQueue.init(commandQueueLock, clock, g_motorService, &g_motorService);

    return true;
}

} // namespace Services
