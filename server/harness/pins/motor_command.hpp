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
    Move,           // Relative move: param1 = steps (signed)
    GoTo,           // Absolute move: param1 = position
    Run,            // Continuous: param1 = speed, param2 = direction (0=rev, 1=fwd)
    SoftStop,       // Decelerate to stop
    HardStop,       // Immediate stop
    SoftHiZ,        // Decelerate then Hi-Z
    HardHiZ,        // Immediate Hi-Z
    GoHome,         // Return to home position
    GoMark,         // Go to mark position
    ResetPos,       // Set current position as zero
    SetAccel,       // param1 = acceleration value
    SetDecel,       // param1 = deceleration value
    SetMaxSpeed,    // param1 = max speed value
    SetMark,        // param1 = mark position
    GetStatus       // Trigger status read and telemetry update
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
