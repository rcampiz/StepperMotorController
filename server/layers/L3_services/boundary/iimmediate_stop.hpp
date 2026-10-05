/**
 * @file iimmediate_stop.hpp
 * @brief L3-internal: ESTOP latch — force Hi-Z now, refuse motion until an explicit clear
 *
 * Used by the ESTOP path (CommandQueue::emergencyStop) so a full motor queue can never
 * delay or drop the stop, and by CLEAR (CommandQueue::clearFault) to release the latch.
 * immediateHardHiZ() executes in the caller's (Comms) task context: it latches, empties the
 * motor command queue and drives the driver to Hi-Z under the SPI mutex. While latched the
 * Motor task refuses Move/GoTo/Run/GoHome/GoMark and inhibits its control loops.
 */
#pragma once

namespace Services {

class IImmediateStop {
public:
    virtual ~IImmediateStop() = default;
    /// Latch + empty the motor queue + Hi-Z now (Comms task context).
    virtual void immediateHardHiZ() = 0;
    /// Release the latch (CLEAR). Motion commands are accepted again from the next Motor-task pass.
    virtual void clearImmediateStop() = 0;
    /// True between immediateHardHiZ() and clearImmediateStop().
    virtual bool isStopLatched() const = 0;
};

} // namespace Services
