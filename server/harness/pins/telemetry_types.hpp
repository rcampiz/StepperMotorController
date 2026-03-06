/**
 * @file telemetry_types.hpp
 * @brief Telemetry data structures — boundary contract types
 *
 * These are pure data definitions shared across layers.
 * No logic, no dependencies beyond stdint.
 */

#pragma once

#include <stdint.h>

namespace Protocol {

struct MotorTelemetry {
    int32_t position;
    int32_t targetPosition;
    uint32_t speed;
    uint16_t statusReg;
    bool busy;
    bool hiZ;
    bool stalled;
};

struct EncoderTelemetry {
    int64_t count;
    int32_t velocity;
    bool indexSeen;
    uint32_t indexTick;
    int32_t revolutions;
    uint32_t indexPeriodUs;
    uint8_t velocityQuality;
    uint8_t filterFlags;
    uint8_t measWindowMs;
    uint16_t sampleRateHz;
};

struct SystemTelemetry {
    uint32_t uptimeTicks;
    uint32_t freeHeap;
    uint8_t cpuLoad;
};

struct ControlTelemetry {
    int32_t  followingError;
    int32_t  setpoint;
    uint8_t  mode;
    bool     tracking;
    int16_t  pidOutput;
    int16_t  pTerm;
    int16_t  iTerm;
    int16_t  dTerm;
    uint8_t  supervisorState;
    uint8_t  currentTier;
    int32_t  velError;
    uint8_t  retryCount;
    int32_t  baseSpeedRaw;
    int32_t  trimSpeedRaw;
    int32_t  finalSpeedRaw;
    uint8_t  trimFrozen;
    uint8_t  velQuality;
};

struct TelemetrySnapshot {
    MotorTelemetry motor;
    EncoderTelemetry encoder;
    SystemTelemetry system;
    ControlTelemetry control;
    uint32_t timestamp;
};

} // namespace Protocol
