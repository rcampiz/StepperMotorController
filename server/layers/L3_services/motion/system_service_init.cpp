/**
 * @file system_service_init.cpp
 * @brief Implements Services::initSystemServices() — L3 system service init
 *
 * Initializes control mode and motor event service.
 * Telemetry and UI mode init handled by the composition root (system_init.cpp).
 */

#include "L3_services/motion/system_service_init.hpp"
#include "L3_services/motion/motion.hpp"
#include "L3_services/motion/control_mode/control_mode.hpp"

namespace Services {

bool initSystemServices(Harness::ILock& controlModeLock,
                        Harness::IQueue<AsyncEvent, 8>& eventQueue)
{
    // Control mode init
    if (!motion.control.mode.init(controlModeLock)) {
        return false;
    }
    motion.control.mode.setMode(ControlMode::OPEN_LOOP);

    // Motor event service
    motion.stepper.event.init(eventQueue);

    return true;
}

} // namespace Services
