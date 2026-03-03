/**
 * @file dispatch_result.hpp
 * @brief Typed status enum for L2↔L3 service call results
 */

#pragma once

#include <stdint.h>

namespace Comms {

/**
 * @brief Typed result status for service dispatch calls.
 * L2 owns the mapping from ServiceStatus → response text.
 */
enum class ServiceStatus : uint8_t {
    Ok,
    InvalidParam,
    OutOfRange,
    Fault,
    FaultActive,     // clearFault: hardware faults still present
    QueueFull,
    QueueEmpty,
    InvalidState,
    InvalidCommand,
    TimeoutExpired,
    FlashError,
    EncoderRequired,
    NotEnabled,
};

/**
 * @brief Convert ServiceStatus to human-readable string (L2 response formatting).
 */
inline const char* statusToString(ServiceStatus s) {
    switch (s) {
        case ServiceStatus::Ok:               return "OK";
        case ServiceStatus::InvalidParam:     return "Invalid parameter";
        case ServiceStatus::OutOfRange:       return "Value out of range";
        case ServiceStatus::Fault:            return "Motor fault active";
        case ServiceStatus::FaultActive:      return "Hardware faults still present";
        case ServiceStatus::QueueFull:        return "Queue full";
        case ServiceStatus::QueueEmpty:       return "Queue empty";
        case ServiceStatus::InvalidState:     return "Invalid state for operation";
        case ServiceStatus::InvalidCommand:   return "Invalid command";
        case ServiceStatus::TimeoutExpired:   return "Timeout expired";
        case ServiceStatus::FlashError:       return "Flash write failed";
        case ServiceStatus::EncoderRequired:  return "Encoder required";
        case ServiceStatus::NotEnabled:       return "Motor not enabled";
        default:                              return "Unknown error";
    }
}

} // namespace Comms
