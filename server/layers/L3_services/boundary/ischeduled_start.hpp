/**
 * @file ischeduled_start.hpp
 * @brief The armed queue's scheduled start, executed by the Motor task (L3-internal interface)
 *
 * SYNC:START:AT hands the start instant to the Motor task as a StartAt command; when the
 * tick arrives the Motor task calls executeScheduledStart(), which flushes the staged
 * commands into its own queue. The Comms task only validates and replies.
 */
#pragma once

namespace Services {

class IScheduledStart {
public:
    virtual ~IScheduledStart() = default;
    /// True while a SYNC:START:AT is armed and not yet executed or cancelled.
    virtual bool scheduledStartPending() const = 0;
    /// Flush the armed queue now (Motor task context). False when nothing is armed any more
    /// (cleared, ESTOP) or the motor queue is full.
    virtual bool executeScheduledStart() = 0;
};

} // namespace Services
