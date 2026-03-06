/**
 * @file itelemetry.cpp
 * @brief Telemetry global accessor wiring
 */

#include "harness/pins/itelemetry.hpp"

namespace Harness {

static ITelemetry* s_telemetryProvider = nullptr;

void setTelemetryProvider(ITelemetry* provider) {
    s_telemetryProvider = provider;
}

ITelemetry& telemetry() {
    return *s_telemetryProvider;
}

} // namespace Harness
