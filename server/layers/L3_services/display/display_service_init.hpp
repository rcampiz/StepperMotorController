/**
 * @file display_service_init.hpp
 * @brief Harness contract for L3 display subsystem init
 *
 * L3 orchestrates: L4 driver init (through harness) →
 * task init (through harness, handles L3 services + RTOS).
 * All Platform deps injected.
 */

#pragma once

namespace Harness {
class IClock;
class IRemoteDisplay;
class IScheduler;
} // namespace Harness

namespace Services {

/**
 * @brief Result of display subsystem initialization
 */
struct DisplaySubsystemResult {
    Harness::IRemoteDisplay* remoteDisplay = nullptr;  ///< Remote display interface
    bool flashAvailable = false;                        ///< NOR flash detected
};

/**
 * @brief Initialize display subsystem — L3 drives the cascade
 *
 * @param clock     System clock (injected from Platform)
 * @param scheduler Scheduler for RTOS task creation (injected from Platform)
 * @param result    Output: remote display interface + flash availability
 * @return true on success
 */
bool initDisplaySubsystem(Harness::IClock& clock,
                          Harness::IScheduler& scheduler,
                          DisplaySubsystemResult& result);

} // namespace Services
