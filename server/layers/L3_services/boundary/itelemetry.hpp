/**
 * @file boundary/itelemetry.hpp
 * @brief Read side of the telemetry store (snapshot copy for the comms path) (L3-internal interface)
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
