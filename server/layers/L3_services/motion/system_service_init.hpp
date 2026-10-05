/**
 * @file system_service_init.hpp
 * @brief Services::initSystemServices() — L3 shared state (control mode, events, telemetry, UI mode)
 */
#pragma once

#include "harness/pins/async_event.hpp"
#include "F_platform/boundary/iqueue.hpp"

namespace Platform { class ILock; class IClock; }

namespace Services {

bool initSystemServices(Platform::ILock& controlModeLock,
                        Platform::IQueue<AsyncEvent, 8>& eventQueue,
                        Platform::ILock& telemetryLock,
                        Platform::ILock& uiModeLock,
                        Platform::IClock& clock);

} // namespace Services
