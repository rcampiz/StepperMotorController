/**
 * @file imotor_command_sink.hpp
 * @brief L3-internal: where motion/config services and the CommandQueue send motor commands
 *
 * Implemented by Services::MotorService over the Platform motor command queue with a bounded
 * send timeout. The MotorCommand POD itself is a harness pin (shared with the Motor task).
 */
#pragma once

#include "harness/pins/motor_command.hpp"

namespace Services {

class IMotorCommandSink {
public:
    virtual ~IMotorCommandSink() = default;
    virtual bool sendCommand(const MotorCommand& cmd, uint32_t timeoutMs = 0) = 0;
};

} // namespace Services
