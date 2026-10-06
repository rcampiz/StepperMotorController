/**
 * @file boot_id_stub.cpp
 * @brief Host stand-in for F_platform/hw/boot_id.cpp (fixed, predictable values)
 */
#include "F_platform/boundary/boot_id.hpp"

namespace Platform {

uint32_t bootId() { return 0xB007CAFEu; }
const char* deviceSerial() { return "HOST0001"; }

} // namespace Platform
