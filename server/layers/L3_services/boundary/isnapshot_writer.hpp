/**
 * @file boundary/isnapshot_writer.hpp
 * @brief Write side of the telemetry store (filled by the motor controller and encoder service) (L3-internal interface)
 */

#pragma once

#include "harness/pins/telemetry_types.hpp"
#include "harness/pins/motor_state.hpp"
#include "L3_services/boundary/iencoder_status_sink.hpp"
#include <stdint.h>

namespace Services {

class ISnapshotWriter {
public:
    virtual ~ISnapshotWriter() = default;

    /** @brief Write the driver's state (from MotorController::periodicUpdate): decoded flags,
     *         position in microsteps, speed in full steps/s; physical, no chip registers */
    virtual void writeMotorStatus(const Harness::MotorState& status) = 0;

    /** @brief Write encoder telemetry (from encoder_task) */
    virtual void writeEncoder(const Services::EncoderTelemetryData& data) = 0;

    /** @brief Write control telemetry (from motor_controller) */
    virtual void writeControl(const Protocol::ControlTelemetry& ctrl) = 0;

    /** @brief Write system telemetry (from platform) */
    virtual void writeSystem(const Protocol::SystemTelemetry& sys) = 0;
};

// Global accessor — wired during init, used by taps and tasks

} // namespace Services
