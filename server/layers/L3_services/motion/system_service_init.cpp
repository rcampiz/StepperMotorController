/**
 * @file system_service_init.cpp
 * @brief Implements Services::initSystemServices() — control mode, events, telemetry store, UI mode
 */

#include "L3_services/motion/system_service_init.hpp"
#include "L3_services/motion/motion.hpp"
#include "L3_services/motion/control_mode/control_mode.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include "L3_services/ui/ui_mode/ui_mode.hpp"

namespace Services {

bool initSystemServices(Platform::ILock& controlModeLock,
                        Platform::IQueue<AsyncEvent, 8>& eventQueue,
                        Platform::ILock& telemetryLock,
                        Platform::ILock& uiModeLock,
                        Platform::IClock& clock)
{
    if (!motion.control.mode.init(controlModeLock)) {
        return false;
    }
    motion.control.mode.setMode(ControlMode::OPEN_LOOP);
    motion.stepper.event.init(eventQueue);

    g_telemetryStore.init(telemetryLock, clock);
    UI::g_uiMode.init(uiModeLock);
    return true;
}

} // namespace Services
