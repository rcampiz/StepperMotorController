/**
 * @file debug_access_init.cpp
 * @brief Implements Drivers::initDebugAccess() over the L4<->L5 harness
 */

#include "harness/pins/debug_access_init.hpp"
#include "harness/defs/harness_l4_l5.hpp"
#include "L4_drivers/devices/debug_access.hpp"

namespace Drivers {

bool initDebugAccess(Drivers::IDebugAccess*& access)
{
    const Harness::L4L5Harness& h = Harness::connectL4L5();
    if (h.boardDebug == nullptr) {
        return false;
    }
    static DebugAccess s_access(*h.boardDebug);
    access = &s_access;
    return true;
}

} // namespace Drivers
