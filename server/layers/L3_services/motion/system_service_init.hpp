/**
 * @file system_service_init.hpp
 * @brief Harness contract for L3 system services init
 *
 * Initializes L3 singletons and cross-cutting services (control mode,
 * motor event) with injected Platform deps.
 */

#pragma once

#include "harness/pins/iqueue.hpp"
#include "harness/pins/async_event.hpp"

namespace Harness { class ILock; }

namespace Services {

/**
 * @brief Initialize system services (control mode, motor event)
 *
 * Telemetry and UI mode init handled by the composition root (system_init.cpp).
 *
 * @param controlModeLock Lock for control mode service
 * @param eventQueue      Async event queue
 * @return true on success
 */
bool initSystemServices(Harness::ILock& controlModeLock,
                        Harness::IQueue<AsyncEvent, 8>& eventQueue);

} // namespace Services
