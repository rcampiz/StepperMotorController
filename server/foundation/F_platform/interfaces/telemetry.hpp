/**
 * @file telemetry.hpp
 * @brief Shared telemetry data structures
 *
 * Thread-safe access via critical sections or mutex.
 */

#ifndef TELEMETRY_HPP
#define TELEMETRY_HPP

#include "F_platform/interfaces/ilock.hpp"
#include "F_platform/interfaces/iclock.hpp"
#include <stdint.h>

namespace Comms {

/**
 * @brief Motor telemetry data
 */
struct MotorTelemetry {
    int32_t position;        // Absolute position (steps)
    int32_t targetPosition;  // Target position for GoTo
    uint32_t speed;          // Current speed (steps/s)
    uint16_t statusReg;      // Raw STATUS register
    bool busy;               // Motor is moving
    bool hiZ;                // Outputs in Hi-Z
    bool stalled;            // Stall detected
};

/**
 * @brief Encoder telemetry data
 */
struct EncoderTelemetry {
    int64_t count;           // Accumulated encoder count (64-bit)
    int32_t velocity;        // Counts per second
    bool indexSeen;          // Index pulse detected since last clear
    uint32_t indexTick;      // Tick count when index last seen
    int32_t revolutions;     // Revolution count from index pulses (signed)
    uint32_t indexPeriodUs;  // Microseconds between last two index pulses
    uint8_t velocityQuality; // VelocityQuality enum (0=GOOD..3=INVALID)
    uint8_t filterFlags;     // Active filter flags (bit0=EMA, bit1=SMA)
    uint8_t measWindowMs;    // Current measurement window (ms)
    uint16_t sampleRateHz;   // DMA sample rate (Hz)
};

/**
 * @brief System telemetry data
 */
struct SystemTelemetry {
    uint32_t uptimeTicks;    // FreeRTOS tick count
    uint32_t freeHeap;       // Free heap bytes
    uint8_t cpuLoad;         // CPU load percentage (if measured)
};

/**
 * @brief Control loop telemetry data
 */
struct ControlTelemetry {
    int32_t  followingError;   // Position error in encoder ticks
    int32_t  setpoint;         // Commanded velocity (ticks/sec)
    uint8_t  mode;             // 0=OPEN_LOOP, 1=MONITOR, 2=SPEED_TRIM
    bool     tracking;         // Trim controller is actively correcting
    int16_t  pidOutput;        // Trim total output (tps adjustment)
    int16_t  pTerm;            // Proportional component
    int16_t  iTerm;            // Integral component
    int16_t  dTerm;            // Derivative component (always 0 for PI)
    // Following Error Supervisor fields
    uint8_t  supervisorState;  // SupervisorState enum (0=IDLE..4=FAULT)
    uint8_t  currentTier;      // Tier enum (0=OBSERVE..3=FAULT_LATCH)
    int32_t  velError;         // Velocity error (ticks/sec)
    uint8_t  retryCount;       // Recovery attempt counter
    // Speed-trim fields
    int32_t  baseSpeedRaw;     // Original commanded speed (raw register)
    int32_t  trimSpeedRaw;     // Trim adjustment (signed raw)
    int32_t  finalSpeedRaw;    // Applied speed = base + trim (raw)
    uint8_t  trimFrozen;       // 1 if integrator frozen this cycle
    uint8_t  velQuality;       // VelocityQuality from encoder (0=GOOD..3=INVALID)
};

/**
 * @brief Combined telemetry snapshot
 */
struct TelemetrySnapshot {
    MotorTelemetry motor;
    EncoderTelemetry encoder;
    SystemTelemetry system;
    ControlTelemetry control;
    uint32_t timestamp;      // Snapshot time (ticks)
};

/**
 * @brief Thread-safe telemetry manager
 */
class TelemetryManager {
public:
    /**
     * @brief Initialize telemetry manager
     * @return true on success
     */
    bool init(ILock& lock, IClock& clock);

    /**
     * @brief Update motor telemetry (call from MotorTask)
     * @param data New motor data
     */
    void updateMotor(const MotorTelemetry& data);

    /**
     * @brief Update encoder telemetry (call from EncoderTask)
     * @param data New encoder data
     */
    void updateEncoder(const EncoderTelemetry& data);

    /**
     * @brief Update system telemetry
     * @param data New system data
     */
    void updateSystem(const SystemTelemetry& data);

    /**
     * @brief Update control loop telemetry
     * @param data New control data
     */
    void updateControl(const ControlTelemetry& data);

    /**
     * @brief Get complete telemetry snapshot (thread-safe)
     * @return Current telemetry state
     */
    TelemetrySnapshot getSnapshot();

private:
    TelemetrySnapshot m_data;
    ILock* m_lock;
    IClock* m_clock;
};

// Global telemetry manager instance
extern TelemetryManager g_telemetry;

} // namespace Comms

#endif // TELEMETRY_HPP
