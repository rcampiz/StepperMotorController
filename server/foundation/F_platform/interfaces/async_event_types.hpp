/**
 * @file async_event_types.hpp
 * @brief Core event data types (layer-independent)
 *
 * EventType enum and AsyncEvent struct live here in Foundation
 * so they can be used by IQueue<AsyncEvent, N> without pulling
 * in L2 protocol headers. Protocol-specific constants (masks,
 * thresholds, codec functions) remain in L2_protocol/async_event.hpp.
 */

#ifndef ASYNC_EVENT_TYPES_HPP
#define ASYNC_EVENT_TYPES_HPP

#include <stdint.h>

enum class EventType : uint8_t {
    FAULT              = 0,
    FAULT_CLEAR        = 1,
    STALL              = 2,
    STALL_CLEAR        = 3,
    MOTION_DONE        = 4,
    FOLLOW_FAULT       = 5,   // Supervisor entered FAULT (Tier 3)
    FOLLOW_RECOVERY    = 6,   // Supervisor entered RECOVERY (Tier 2)
    FOLLOW_RECOVERED   = 7,   // Recovery succeeded
};

struct AsyncEvent {
    EventType type;
    uint16_t  statusReg;   // raw STATUS register at detection time
};

#endif // ASYNC_EVENT_TYPES_HPP
