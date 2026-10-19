/**
 * @file motor_command.hpp
 * @brief Harness pin: motor command POD — the element type of the Platform motor command queue
 *
 * Shared by L3 (producer: motion/config services, CommandQueue) and the task sheet
 * (consumer: Motor task receives and hands it to Services::IMotorService::onCommand).
 * Owned by no layer; copied by value through the queue.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Services {

enum class MotorCmdType : uint8_t {
    Move,           // Relative move: param1 = microsteps (signed), param2 = speed override (full steps/s, 0 = none)
    GoTo,           // Absolute move: param1 = position (microsteps), param2 = speed override (full steps/s, 0 = none)
    Run,            // Continuous: param1 = speed (full steps/s), param2 = direction (0=rev, 1=fwd)
    SoftStop,       // Decelerate to stop
    HardStop,       // Immediate stop
    SoftHiZ,        // Decelerate then Hi-Z
    HardHiZ,        // Immediate Hi-Z
    GoHome,         // Return to home position: param2 = speed override (full steps/s, 0 = none)
    GoMark,         // Go to mark position
    ResetPos,       // Set current position as zero
    SetAccel,       // param1 = acceleration value
    SetDecel,       // param1 = deceleration value
    SetMaxSpeed,    // param1 = MAX_SPEED config register value (driver boundary step 3: physical)
    SetMark,        // param1 = mark position
    GetStatus,      // Trigger status read and telemetry update
    SetStepMode,    // param1 = STEP_MODE (0..7); handled by MotorService on the Motor task, readback published
    StartAt,        // param1 = TickTimer tick (us, bit-cast); the Motor task waits for it, then flushes the
                    // armed CommandQueue itself (SYNC:START:AT), so the Comms task never blocks
    // Motor-task jobs (Phase 4 step 5): the Comms task used to run these on the controller
    // directly, interleaving with the Motor task's own driver sequences. Now they are
    // messages answered through MotorService's job slot (bounded wait on the Comms side).
    Reinit,         // IMotorDriver::init() + applyConfig()
    ApplyConfig,    // write the saved config (STEP_MODE via the Hi-Z-safe path when the bridges are on)
    GetDebugInfo    // read every register into the job's MotorDebugParams
};

struct MotorCommand {
    MotorCmdType type;
    int32_t param1;
    int32_t param2;
};

} // namespace Services

namespace Harness {
constexpr size_t MOTOR_CMD_QUEUE_DEPTH = 8;
}

// The task sheet refers to the same POD under its own namespace.
namespace Scheduler {
using Services::MotorCommand;
using Services::MotorCmdType;
}
