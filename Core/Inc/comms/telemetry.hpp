/**
 * @file telemetry.hpp
 * @brief Shared telemetry data structures
 *
 * Thread-safe access via critical sections or mutex.
 */

#ifndef TELEMETRY_HPP
#define TELEMETRY_HPP

#include <cstdint>
#include "FreeRTOS.h"
#include "semphr.h"

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
    int32_t count;           // Raw encoder count
    int32_t velocity;        // Counts per second
    bool indexSeen;          // Index pulse detected since last clear
    uint32_t indexTick;      // Tick count when index last seen
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
 * @brief Combined telemetry snapshot
 */
struct TelemetrySnapshot {
    MotorTelemetry motor;
    EncoderTelemetry encoder;
    SystemTelemetry system;
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
    bool init();

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
     * @brief Get complete telemetry snapshot (thread-safe)
     * @return Current telemetry state
     */
    TelemetrySnapshot getSnapshot();

private:
    TelemetrySnapshot m_data;
    SemaphoreHandle_t m_mutex;
};

// Global telemetry manager instance
extern TelemetryManager g_telemetry;

} // namespace Comms

#endif // TELEMETRY_HPP
