/**
 * @file async_event.cpp
 * @brief Event type utility functions (layer-independent)
 */

#include "harness/pins/async_event.hpp"

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
        case EventType::ESTOP:             return "ESTOP";
        case EventType::ESTOP_CLEAR:       return "ESTOP_CLEAR";
        default:                           return "UNKNOWN";
    }
}

uint8_t eventTypeToMaskBit(EventType t) {
    const auto n = static_cast<uint8_t>(t);
    return n < 8 ? static_cast<uint8_t>(1u << n) : 0;     // ESTOP events have no mask bit
}

bool eventTypeAlwaysDelivered(EventType t) {
    return t == EventType::ESTOP || t == EventType::ESTOP_CLEAR;
}

bool eventTypeIsCritical(EventType t) {
    switch (t) {
        case EventType::FAULT:
        case EventType::FAULT_CLEAR:
        case EventType::STALL:
        case EventType::STALL_CLEAR:
        case EventType::FOLLOW_FAULT:
        case EventType::ESTOP:
        case EventType::ESTOP_CLEAR:
            return true;
        case EventType::MOTION_DONE:
        case EventType::FOLLOW_RECOVERY:
        case EventType::FOLLOW_RECOVERED:
        default:
            return false;
    }
}
