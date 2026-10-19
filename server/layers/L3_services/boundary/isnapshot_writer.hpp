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

    /** @brief Write raw motor STATUS register (from TracedMotorDriver::getStatus) */
    virtual void writeMotorStatus(const Harness::MotorState& status) = 0;   ///< decoded by the driver

    /** @brief Write raw motor register value (from TracedMotorDriver::getParam) */
    virtual void writeMotorParam(uint8_t reg, uint32_t value) = 0;

    /** @brief Write encoder telemetry (from encoder_task) */
    virtual void writeEncoder(const Services::EncoderTelemetryData& data) = 0;

    /** @brief Write control telemetry (from motor_controller) */
    virtual void writeControl(const Protocol::ControlTelemetry& ctrl) = 0;

    /** @brief Write system telemetry (from platform) */
    virtual void writeSystem(const Protocol::SystemTelemetry& sys) = 0;
};

// Global accessor — wired during init, used by taps and tasks

} // namespace Services
