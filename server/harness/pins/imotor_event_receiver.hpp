/**
 * @file imotor_event_receiver.hpp
 * @brief Interface for receiving async motor events
 *
 * Outbound harness interface: comms_task drains motor events through this
 * instead of calling MotorEventManager directly. Keeps task decoupled
 * from L3 service layer.
 */

#pragma once

// Forward declaration — async_event_types.hpp is a foundation type
struct AsyncEvent;

namespace Harness {

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

} // namespace Harness
