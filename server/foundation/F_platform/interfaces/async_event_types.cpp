/**
 * @file async_event_types.cpp
 * @brief Event type utility functions (layer-independent)
 */

#include "F_platform/interfaces/async_event_types.hpp"

const char* eventTypeToString(EventType t) {
    switch (t) {
        case EventType::FAULT:             return "FAULT";
        case EventType::FAULT_CLEAR:       return "FAULT_CLEAR";
        case EventType::STALL:             return "STALL";
        case EventType::STALL_CLEAR:       return "STALL_CLEAR";
        case EventType::MOTION_DONE:       return "MOTION_DONE";
        case EventType::FOLLOW_FAULT:      return "FOLLOW_FAULT";
        case EventType::FOLLOW_RECOVERY:   return "FOLLOW_RECOVERY";
        case EventType::FOLLOW_RECOVERED:  return "FOLLOW_RECOVERED";
        default:                           return "UNKNOWN";
    }
}

uint8_t eventTypeToMaskBit(EventType t) {
    return static_cast<uint8_t>(1 << static_cast<uint8_t>(t));
}

bool eventTypeIsCritical(EventType t) {
    switch (t) {
        case EventType::FAULT:
        case EventType::FAULT_CLEAR:
        case EventType::STALL:
        case EventType::STALL_CLEAR:
        case EventType::FOLLOW_FAULT:
            return true;
        case EventType::MOTION_DONE:
        case EventType::FOLLOW_RECOVERY:
        case EventType::FOLLOW_RECOVERED:
        default:
            return false;
    }
}
