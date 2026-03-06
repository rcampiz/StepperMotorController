/**
 * @file comms.hpp
 * @brief Umbrella header — CommsGroup (telemetry + traces)
 */

#ifndef L3_COMMS_HPP
#define L3_COMMS_HPP

#include "L3_services/comms/trace_dispatch/trace_dispatch.hpp"
#include "L3_services/comms/system_dispatch/system_dispatch.hpp"

namespace Services {

struct CommsGroup {
    TraceDispatch&  trace;
    SystemDispatch& system;
};

extern CommsGroup comms;

} // namespace Services

#endif // L3_COMMS_HPP
