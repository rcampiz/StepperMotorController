/**
 * @file event_service.hpp
 * @brief Event service — policy, queue, and counters for async events
 *
 * Follows the Services::Motion / Services::Safety / Services::Config pattern
 * per CODE_PHILOSOPHY §10. Wire formatting is in Comms::EventCodec.
 *
 * See docs/PROTOCOL_EVENTS_V1.md for contract.
 */

#pragma once
#include "F_platform/interfaces/iqueue.hpp"
#include "L2_protocol/async_event.hpp"
#include <stdint.h>

namespace Services::Event {

struct Stats {
    uint32_t sent;
    uint32_t lostCritical;  // dropped FAULT/STALL/CLEAR events
    uint32_t lostInfo;      // dropped MOTION_DONE events (expected under load)
    uint8_t  enableMask;
    uint8_t  queueDepth;
};

/**
 * @brief Initialize with an injected queue. Call once before scheduler start.
 */
void init(IQueue<AsyncEvent, 8>& queue);

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
uint8_t getMask();

/**
 * @brief Get event statistics.
 */
Stats getStats();

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
bool receive(AsyncEvent& evt);

} // namespace Services::Event
