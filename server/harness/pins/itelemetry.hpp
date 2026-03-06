/**
 * @file itelemetry.hpp
 * @brief Telemetry read interface — boundary contract
 *
 * Provides thread-safe read access to the system telemetry snapshot.
 * Write access is through ISnapshotWriter (separate interface).
 * Implementation lives in F_platform (SystemSnapshot).
 */

#pragma once

#include "harness/pins/telemetry_types.hpp"

namespace Harness {

class ITelemetry {
public:
    virtual ~ITelemetry() = default;

    virtual Protocol::TelemetrySnapshot getSnapshot() = 0;
};

// Global accessor — wired during init, used by all layers
void setTelemetryProvider(ITelemetry* provider);
ITelemetry& telemetry();

} // namespace Harness
