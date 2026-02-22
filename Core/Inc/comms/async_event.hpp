/**
 * @file async_event.hpp
 * @brief Async event types and structures for unsolicited firmware-to-host messages
 *
 * See docs/PROTOCOL_EVENTS_V1.md for wire format and behaviour contract.
 */

#pragma once
#include <stdint.h>

enum class EventType : uint8_t {
    FAULT        = 0,
    FAULT_CLEAR  = 1,
    STALL        = 2,
    STALL_CLEAR  = 3,
    MOTION_DONE  = 4,
};

struct AsyncEvent {
    EventType type;
    uint16_t  statusReg;   // raw STATUS register at detection time
};

// Enable mask bits (one per event type)
static constexpr uint8_t EVT_MASK_FAULT       = (1 << 0);
static constexpr uint8_t EVT_MASK_FAULT_CLR   = (1 << 1);
static constexpr uint8_t EVT_MASK_STALL       = (1 << 2);
static constexpr uint8_t EVT_MASK_STALL_CLR   = (1 << 3);
static constexpr uint8_t EVT_MASK_MOTION_DONE = (1 << 4);
static constexpr uint8_t EVT_MASK_ALL         = 0x1F;

// Reserved-slot threshold: informational events only enqueued when depth < this
static constexpr uint8_t EVT_RESERVED_SLOT_THRESHOLD = 4;

const char* eventTypeToString(EventType t);
uint8_t eventTypeToMaskBit(EventType t);
bool eventTypeIsCritical(EventType t);
