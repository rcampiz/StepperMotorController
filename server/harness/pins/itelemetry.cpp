/**
 * @file itelemetry.cpp
 * @brief Telemetry global accessor wiring
 */

#include "F_platform/tasks/boundary/itelemetry.hpp"

namespace Scheduler {

static ITelemetry* s_telemetryProvider = nullptr;

void setTelemetryProvider(ITelemetry* provider) {
    s_telemetryProvider = provider;
}

ITelemetry& telemetry() {
    return *s_telemetryProvider;
}

} // namespace Scheduler
