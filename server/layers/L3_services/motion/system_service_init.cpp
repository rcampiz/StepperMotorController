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

// Adapt Platform:: → Services:: at wiring boundary
#include "harness/adapters/queue_adapter.hpp"
namespace Services { class ILock; }
namespace Harness {
    Services::ILock& adaptLock(Platform::ILock& raw);
}

namespace Services {

bool initSystemServices(Platform::ILock& controlModeLock,
                        Platform::IQueue<AsyncEvent, 8>& eventQueue)
{
    // Adapt Platform:: → Services:: consumer ports
    static Harness::QueueAdapter<AsyncEvent, 8> adaptedQueue(eventQueue);

    // Control mode init
    if (!motion.control.mode.init(Harness::adaptLock(controlModeLock))) {
        return false;
    }
    motion.control.mode.setMode(ControlMode::OPEN_LOOP);

    // Motor event service
    motion.stepper.event.init(adaptedQueue);

    return true;
}

} // namespace Services
