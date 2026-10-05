/**
 * @file icomms_service.hpp
 * @brief Services:: provider — Comms task entry point (H_TASKS_L3)
 *
 * The Comms task only waits (10 ms period) and calls tick(). Driving the L2 parser,
 * draining async events, publishing telemetry and running the heartbeat watchdog all
 * live inside Services::CommsService.
 */
#pragma once

namespace Services {

class ICommsService {
public:
    virtual ~ICommsService() = default;

    /// Called once after the scheduler starts (banner, timers). Comms task context.
    virtual void onStart() = 0;

    /// One comms step. Comms task context, every COMMS_POLL_PERIOD_MS.
    virtual void tick() = 0;
};

} // namespace Services
