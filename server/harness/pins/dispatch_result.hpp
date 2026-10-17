/**
 * @file dispatch_result.hpp
 * @brief Typed status enum for L2↔L3 service call results
 */

#pragma once

#include <stdint.h>

namespace Harness {

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
    StoreFull,       // config journal full and the motor is moving: the erase would stall the MCU
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
        case ServiceStatus::StoreFull:        return "Config store full: stop the motor and save again";
        default:                              return "Unknown error";
    }
}

/**
 * @brief Machine-readable code for a ServiceStatus (JSON "code" field, F6).
 */
inline const char* statusToCode(ServiceStatus s) {
    switch (s) {
        case ServiceStatus::Ok:               return "OK";
        case ServiceStatus::InvalidParam:     return "INVALID_PARAM";
        case ServiceStatus::OutOfRange:       return "OUT_OF_RANGE";
        case ServiceStatus::Fault:            return "FAULT";
        case ServiceStatus::FaultActive:      return "FAULT_ACTIVE";
        case ServiceStatus::QueueFull:        return "QUEUE_FULL";
        case ServiceStatus::QueueEmpty:       return "QUEUE_EMPTY";
        case ServiceStatus::InvalidState:     return "INVALID_STATE";
        case ServiceStatus::InvalidCommand:   return "INVALID_COMMAND";
        case ServiceStatus::TimeoutExpired:   return "TIMEOUT_EXPIRED";
        case ServiceStatus::FlashError:       return "FLASH_ERROR";
        case ServiceStatus::EncoderRequired:  return "ENCODER_REQUIRED";
        case ServiceStatus::NotEnabled:       return "NOT_ENABLED";
        case ServiceStatus::StoreFull:        return "STORE_FULL";
        default:                              return "ERROR";
    }
}

} // namespace Harness
