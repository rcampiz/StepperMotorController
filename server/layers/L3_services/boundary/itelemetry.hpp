/**
 * @file boundary/itelemetry.hpp
 * @brief Consumer port — Services side of the Scheduler::ITelemetry harness connection
 *
 * Structurally identical to Scheduler::ITelemetry.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

#include "harness/pins/telemetry_types.hpp"

namespace Services {

class ITelemetry {
public:
    virtual ~ITelemetry() = default;

    virtual Protocol::TelemetrySnapshot getSnapshot() = 0;
};

// Global accessor — wired during init, used by all layers

} // namespace Services
