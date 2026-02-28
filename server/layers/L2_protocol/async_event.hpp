/**
 * @file async_event.hpp
 * @brief Async event protocol constants and codec declarations
 *
 * Core types (EventType, AsyncEvent) live in F_platform/interfaces/async_event_types.hpp.
 * This header adds protocol-level masks, thresholds, and codec functions.
 * See docs/PROTOCOL_EVENTS_V1.md for wire format and behaviour contract.
 */

#pragma once
#include "F_platform/interfaces/async_event_types.hpp"

// Enable mask bits (one per event type)
static constexpr uint8_t EVT_MASK_FAULT       = (1 << 0);
static constexpr uint8_t EVT_MASK_FAULT_CLR   = (1 << 1);
static constexpr uint8_t EVT_MASK_STALL       = (1 << 2);
static constexpr uint8_t EVT_MASK_STALL_CLR   = (1 << 3);
static constexpr uint8_t EVT_MASK_MOTION_DONE      = (1 << 4);
static constexpr uint8_t EVT_MASK_FOLLOW_FAULT     = (1 << 5);
static constexpr uint8_t EVT_MASK_FOLLOW_RECOVERY  = (1 << 6);
static constexpr uint8_t EVT_MASK_FOLLOW_RECOVERED = (1 << 7);
static constexpr uint8_t EVT_MASK_ALL              = 0xFF;

// Reserved-slot threshold: informational events only enqueued when depth < this
static constexpr uint8_t EVT_RESERVED_SLOT_THRESHOLD = 4;

const char* eventTypeToString(EventType t);
uint8_t eventTypeToMaskBit(EventType t);
bool eventTypeIsCritical(EventType t);
