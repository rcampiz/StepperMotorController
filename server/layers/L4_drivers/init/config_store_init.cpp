/**
 * @file config_store_init.cpp
 * @brief Implements Drivers::initConfigStore() over the L4<->L5 harness
 */

#include "harness/pins/config_store_init.hpp"
#include "harness/defs/harness_l4_l5.hpp"
#include "L4_drivers/devices/internal_flash_config_store.hpp"

namespace Drivers {

bool initConfigStore(Drivers::IConfigStore*& store)
{
    const Harness::L4L5Harness& h = Harness::connectL4L5();
    if (h.internalFlash == nullptr) {
        return false;
    }
    static InternalFlashConfigStore s_store(*h.internalFlash);
    store = &s_store;
    return true;
}

} // namespace Drivers
