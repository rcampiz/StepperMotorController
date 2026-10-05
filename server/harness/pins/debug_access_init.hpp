/**
 * @file debug_access_init.hpp
 * @brief Init contract: L4 provides the raw debug access port (over H_L4_L5)
 */
#pragma once

namespace Drivers {
class IDebugAccess;
bool initDebugAccess(Drivers::IDebugAccess*& access);
} // namespace Drivers
