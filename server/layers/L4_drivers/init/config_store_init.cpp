/**
 * @file config_store_init.cpp
 * @brief Implements Drivers::initConfigStore() over the L4<->L5 harness
 */

#include "harness/pins/config_store_init.hpp"
#include "harness/defs/harness_l4_l5.hpp"
#include "L4_drivers/devices/internal_flash_journal_store.hpp"

namespace Drivers {

bool initConfigStore(Drivers::IConfigStore*& store)
{
    const Harness::L4L5Harness& h = Harness::connectL4L5();
    if (h.internalFlash == nullptr) {
        return false;
    }
    // Journal: saves append into blank flash; the 1-2 s sector erase happens only when the
    // journal is full (at boot, or at run time with the motor stopped).
    static InternalFlashJournalStore s_store(*h.internalFlash);
    store = &s_store;
    return true;
}

} // namespace Drivers
