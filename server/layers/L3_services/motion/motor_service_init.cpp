/**
 * @file motor_service_init.cpp
 * @brief Implements Services::initMotorSubsystem() — L3 drives init cascade
 *
 * Layer-clean: only includes L3 + harness.
 * No L4, L5, F_platform/tasks, or F_platform/rtos includes.
 * All Platform deps injected as parameters.
 */

#include "L3_services/motion/motor_service_init.hpp"

// L4 init through harness
#include "harness/pins/motor_driver_init.hpp"

// Task init through harness
#include "harness/pins/motor_task_init.hpp"

// L3 internal
#include "L3_services/motion/motion.hpp"
#include "L3_services/dispatch/command_queue/command_queue.hpp"

// Harness interfaces
#include "harness/pins/imotor_command_sink.hpp"

// Tracing (conditional)
#ifdef ENABLE_INTERFACE_TRACE
#include "harness/taps/traced_command_sink.hpp"
#endif

// --- MotorCommandSinkImpl adapter (only uses harness interfaces) ---

class MotorCommandSinkImpl : public Harness::IMotorCommandSink {
public:
    explicit MotorCommandSinkImpl(
        Harness::IQueue<Harness::MotorCommand, Harness::MOTOR_CMD_QUEUE_DEPTH>& queue)
        : m_queue(queue) {}

    bool sendCommand(const Harness::MotorCommand& cmd, uint32_t timeoutMs = 0) override {
        return m_queue.send(cmd, timeoutMs);
    }

private:
    Harness::IQueue<Harness::MotorCommand, Harness::MOTOR_CMD_QUEUE_DEPTH>& m_queue;
};

// --- Global pin (L3 init owns wiring) ---
Harness::IMotorCommandSink* Harness::g_motorCommandSink = nullptr;

namespace Services {

bool initMotorSubsystem(
    Harness::IQueue<Harness::MotorCommand, Harness::MOTOR_CMD_QUEUE_DEPTH>& commandQueue,
    Harness::IClock& clock,
    Harness::IScheduler& scheduler,
    Harness::ILock& commandQueueLock,
    MotorSubsystemResult& result)
{
    // L4 driver (through harness → L4 → L5)
    Harness::IMotorDriver* driver = nullptr;
    if (!Drivers::initMotorDriver(driver)) {
        return false;
    }

    // L3 config
    motion.configStore.init();

    // Task (through harness → F_platform)
    Harness::MotorTaskResult taskResult;
    if (!Harness::initMotorTask(*driver, commandQueue,
                                 clock, motion.stepper.controller,
                                 scheduler, taskResult)) {
        return false;
    }
    result.motorCtrl = taskResult.motorCtrl;

    // Adapter (harness interfaces only)
    static MotorCommandSinkImpl motorSink(commandQueue);
#ifdef ENABLE_INTERFACE_TRACE
    static Harness::TracedMotorCommandSink tracedSink(motorSink);
    Harness::g_motorCommandSink = &tracedSink;
#else
    Harness::g_motorCommandSink = &motorSink;
#endif

    // L3 command queue
    g_commandQueue.init(commandQueueLock, clock,
                        *Harness::g_motorCommandSink);

    return true;
}

} // namespace Services
