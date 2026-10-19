/**
 * @file motor_service_init.cpp
 * @brief Implements Services::initMotorSubsystem() — L3 drives the motor init cascade
 *
 * L3 -> H_L3_L4 -> L4 (-> H_L4_L5 -> L5) through connectL3L4(). No task creation here
 * (rule 1: the task sheet is above L3; the composition root creates the Motor task).
 */

#include "L3_services/motion/motor_service_init.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "L3_services/motion/motion.hpp"
#include "L3_services/motion/motor_service/motor_service.hpp"
#include "L3_services/dispatch/command_queue/command_queue.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"

namespace Services {

bool initMotorSubsystem(
    Platform::IQueue<MotorCommand, Harness::MOTOR_CMD_QUEUE_DEPTH>& commandQueue,
    Platform::IClock& clock,
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

    // L3 config (persistent blob via the L4 config store). The run-time journal erase
    // freezes the MCU for 1-2 s, so it is gated on the motor being stopped.
    motion.configStore.init(*h.configStore, h.motor);   // the driver: caps, V1-V4 chip-unit migration
    motion.configStore.setEraseGate([]() -> bool {
        const Protocol::TelemetrySnapshot s = g_telemetryStore.getSnapshot();
        return !s.motor.busy && s.motor.speed == 0;
    });

    // Motor service (task body, command sink, task control, immediate stop)
    g_motorService.init(motion.stepper.controller, motion.stepper.controller,
                        commandQueue, clock, taskStats, g_telemetryStore);

    // L3 command queue (ARM/START): bounded sends, immediate stop for ESTOP
    g_commandQueue.init(commandQueueLock, clock, g_motorService, &g_motorService);
    g_motorService.setScheduledStart(&g_commandQueue);   // SYNC:START:AT runs on the Motor task

    result.motorService = &g_motorService;
    result.motorCtrl = &g_motorService;
    return true;
}

} // namespace Services
