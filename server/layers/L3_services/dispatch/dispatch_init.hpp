/**
 * @file dispatch_init.hpp
 * @brief L3 dispatch init contract — creates all 12 dispatch implementations
 *
 * Takes harness interface dependencies (from tasks), returns 12
 * sub-dispatcher interface pointers in a DispatchInterfaces struct.
 * Follows the L4 driver init pattern: layer hides concrete types.
 */

#pragma once

#include "harness/pins/dispatch_interfaces.hpp"
#include <stdint.h>

namespace Harness {
class IEncoder;
class IEncoderFilterControl;
class IMotorTaskControl;
class IRemoteDisplay;
class IClock;
} // namespace Harness

namespace Services {

struct DispatchDeps {
    Harness::IEncoder*              encoder = nullptr;
    Harness::IEncoderFilterControl* encFilter = nullptr;
    Harness::IMotorTaskControl*     motorCtrl = nullptr;
    Harness::IRemoteDisplay*        remoteDisplay = nullptr;
    Harness::IClock*                clock = nullptr;
};

/**
 * @brief Initialize all 12 L3 dispatch implementations
 *
 * Creates static dispatch instances, injects dependencies, populates
 * service group externs. Only includes L3 + harness headers internally.
 *
 * @param out Output struct with 12 sub-dispatcher interface pointers
 * @param deps Dependencies from tasks and transport (harness interfaces only)
 * @return true on success
 */
bool initDispatches(Harness::DispatchInterfaces& out, const DispatchDeps& deps);

/**
 * @brief Set event sequence function on system dispatch (late binding)
 *
 * Called after comms task init provides the function pointer.
 */
void setDispatchEventSeqFn(uint32_t (*fn)());

} // namespace Services
