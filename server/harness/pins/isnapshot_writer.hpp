/**
 * @file isnapshot_writer.hpp
 * @brief System snapshot write interface — boundary contract
 *
 * Taps and services write raw/processed data through this.
 * SystemSnapshot implements it. Readers use ITelemetry::getSnapshot().
 */

#pragma once

#include "harness/pins/telemetry_types.hpp"
#include "harness/pins/iencoder_status_sink.hpp"
#include <stdint.h>

namespace Harness {

class ISnapshotWriter {
public:
    virtual ~ISnapshotWriter() = default;

    /** @brief Write raw motor STATUS register (from TracedMotorDriver::getStatus) */
    virtual void writeMotorStatus(uint16_t statusReg) = 0;

    /** @brief Write raw motor register value (from TracedMotorDriver::getParam) */
    virtual void writeMotorParam(uint8_t reg, uint32_t value) = 0;

    /** @brief Write encoder telemetry (from encoder_task) */
    virtual void writeEncoder(const EncoderTelemetryData& data) = 0;

    /** @brief Write control telemetry (from motor_controller) */
    virtual void writeControl(const Protocol::ControlTelemetry& ctrl) = 0;

    /** @brief Write system telemetry (from platform) */
    virtual void writeSystem(const Protocol::SystemTelemetry& sys) = 0;
};

// Global accessor — wired during init, used by taps and tasks
void setSnapshotWriter(ISnapshotWriter* writer);
ISnapshotWriter& snapshotWriter();

} // namespace Harness
