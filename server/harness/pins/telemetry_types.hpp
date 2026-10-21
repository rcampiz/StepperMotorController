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
    int64_t encoderAtPosition;   ///< the encoder count read at the same instant as position
    int32_t targetPosition;
    uint32_t speed;
    uint32_t statusReg;      ///< the driver chip's own status word: opaque, diagnostics only
    uint8_t faults;          ///< Harness::DriverFault bits, decoded by the driver
    uint8_t phase;           ///< Harness::MotionPhase
    bool busy;
    bool hiZ;
    bool stalled;
    bool forward;
    bool statusReadable;     ///< false while the chip is held in standby
};

struct EncoderTelemetry {
    int64_t count;
    int32_t velocity;          // counts per second (signed), filtered
    bool indexSeen;
    uint32_t indexTick;
    int32_t revolutions;
    uint32_t indexPeriodUs;
    uint8_t velocityQuality;
    uint8_t filterFlags;
    uint8_t measWindowMs;
    uint16_t sampleRateHz;
    uint16_t ripple10;         // position wobble about its trend, rms, 0.1 counts (<= 999.9)
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
    float    baseSpeedFsps;      ///< speed trim: commanded speed, full steps/s
    float    trimSpeedFsps;      ///< speed trim: final - base, full steps/s
    float    finalSpeedFsps;     ///< speed trim: speed sent to the driver, full steps/s
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
