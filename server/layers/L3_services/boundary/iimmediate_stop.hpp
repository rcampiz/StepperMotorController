/**
 * @file iimmediate_stop.hpp
 * @brief L3-internal: force the motor outputs to Hi-Z right now, bypassing the command queue
 *
 * Used by the ESTOP path (CommandQueue::emergencyStop) so a full motor queue can never
 * delay or drop the stop. Executes in the caller's (Comms) task context under the SPI mutex.
 */
#pragma once

namespace Services {

class IImmediateStop {
public:
    virtual ~IImmediateStop() = default;
    virtual void immediateHardHiZ() = 0;
};

} // namespace Services
