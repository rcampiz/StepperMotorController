/**
 * @file config_store_init.hpp
 * @brief Init contract: L4 provides the persistent configuration store
 *
 * Implemented in L4_drivers/init/config_store_init.cpp over the L4<->L5 harness
 * (Board::IInternalFlash). Consumed by L3 MotorConfigManager (migration step 4).
 */
#pragma once

namespace Drivers {
class IConfigStore;
bool initConfigStore(Drivers::IConfigStore*& store);
} // namespace Drivers
