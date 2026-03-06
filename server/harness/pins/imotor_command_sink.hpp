/**
 * @file imotor_command_sink.hpp
 * @brief Motor command types and command sink interface
 *
 * Defines the motor command vocabulary (MotorCmdType/MotorCommand) and an
 * abstract sink that L3 services use to send commands to the motor task.
 * Foundation provides MotorCommandSinkImpl wrapping a FreeRTOS queue.
 */

#pragma once

#include <stdint.h>

namespace Harness {

/**
 * @brief Motor command types
 */
enum class MotorCmdType : uint8_t {
  // Motion commands
  Move,     // Relative move: param1 = steps (signed)
  GoTo,     // Absolute move: param1 = position
  Run,      // Continuous: param1 = speed, param2 = direction (0=rev, 1=fwd)
  SoftStop, // Decelerate to stop
  HardStop, // Immediate stop
  SoftHiZ,  // Decelerate then Hi-Z
  HardHiZ,  // Immediate Hi-Z
  GoHome,   // Return to home position
  GoMark,   // Go to mark position
  ResetPos, // Set current position as zero

  // Configuration commands
  SetAccel,    // param1 = acceleration value
  SetDecel,    // param1 = deceleration value
  SetMaxSpeed, // param1 = max speed value
  SetMark,     // param1 = mark position

  // Query (response via telemetry)
  GetStatus // Trigger status read and telemetry update
};

/**
 * @brief Motor command structure
 */
struct MotorCommand {
  MotorCmdType type;
  int32_t param1;
  int32_t param2;
};

/**
 * @brief Abstract sink for sending motor commands
 *
 * L3 services call sendCommand() instead of directly touching
 * the Foundation motor task queue.
 */
class IMotorCommandSink {
public:
    virtual ~IMotorCommandSink() = default;

    /**
     * @brief Send a command to the motor task
     * @param cmd Command to send
     * @param timeoutMs Milliseconds to wait if full (0 = don't wait)
     * @return true if command accepted
     */
    virtual bool sendCommand(const MotorCommand& cmd, uint32_t timeoutMs = 0) = 0;
};

// Global sink pointer — set by main.cpp composition root
extern IMotorCommandSink* g_motorCommandSink;

} // namespace Harness
