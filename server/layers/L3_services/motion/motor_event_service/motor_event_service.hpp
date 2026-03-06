/**
 * @file motor_event_service.hpp
 * @brief Motor event service — policy, queue, and counters for async motor events
 *
 * Wire formatting is in Comms::EventCodec.
 * See docs/PROTOCOL_EVENTS_V1.md for contract.
 */

#pragma once
#include "harness/pins/iqueue.hpp"
#include "harness/pins/imotor_event_receiver.hpp"
#include "harness/pins/async_event.hpp"
#include <stdint.h>

namespace Services {

struct MotorEventStats {
    uint32_t sent;
    uint32_t lostCritical;  // dropped FAULT/STALL/CLEAR events
    uint32_t lostInfo;      // dropped MOTION_DONE events (expected under load)
    uint8_t  enableMask;
    uint8_t  queueDepth;
};

class MotorEventManager : public Harness::IMotorEventReceiver {
public:
    /**
     * @brief Initialize with an injected queue. Call once before scheduler start.
     */
    void init(Harness::IQueue<AsyncEvent, 8>& queue);

    /**
     * @brief Set the event enable mask. Also triggers snapshot-on-enable.
     * @param mask Bitmask of event types to enable (EVT_MASK_* constants)
     * @param currentStatusReg Current motor STATUS register for snapshot
     */
    void enable(uint8_t mask, uint16_t currentStatusReg);

    /**
     * @brief Disable all events (clear mask to 0).
     */
    void disable();

    /**
     * @brief Get current enable mask.
     */
    uint8_t getMask() const;

    /**
     * @brief Get event statistics.
     */
    MotorEventStats getStats() const;

    /**
     * @brief Post an event from motor_task. Non-blocking.
     *
     * Checks enable mask, applies reserved-slot policy for informational events.
     * Returns false if event was dropped (queue full or not enabled).
     */
    bool post(EventType type, uint16_t statusReg);

    /**
     * @brief Receive next event from queue. Non-blocking.
     * @param evt Output event
     * @return true if an event was received, false if queue empty
     */
    bool receive(AsyncEvent& evt) override;

private:
    static constexpr uint8_t QUEUE_DEPTH = 8;

    Harness::IQueue<AsyncEvent, 8>* m_queue       = nullptr;
    uint8_t  m_enableMask   = 0;
    uint32_t m_sentCount    = 0;
    uint32_t m_lostCritical = 0;
    uint32_t m_lostInfo     = 0;
};

extern MotorEventManager g_motorEvent;

} // namespace Services
