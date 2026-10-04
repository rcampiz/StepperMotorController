/**
 * @file boundary/imotoreventreceiver.hpp
 * @brief Consumer port — Scheduler side of the Services::IMotorEventReceiver harness connection
 *
 * Structurally identical to Services::IMotorEventReceiver.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

// Forward declaration -- async_event_types.hpp is a foundation type
struct AsyncEvent;

namespace Scheduler {

class IMotorEventReceiver {
public:
    virtual ~IMotorEventReceiver() = default;

    /**
     * @brief Try to receive a queued motor event
     * @param evt Output event (valid only when returning true)
     * @return true if an event was received, false if queue empty
     */
    virtual bool receive(AsyncEvent& evt) = 0;
};

} // namespace Scheduler
