/**
 * @file boot_id.hpp
 * @brief Platform power net: per-boot identity and the MCU serial (F17)
 *
 * bootId() is a 32-bit value that differs from boot to boot with high probability. The
 * STM32F401 has no RNG, so the firmware mixes the 96-bit unique device ID with the DWT
 * cycle counter and SysTick at first use (pseudo-random; two boots collide only with the
 * same cycle-exact timing). It is computed once and then constant for the session.
 * deviceSerial() is the unique device ID folded to 8 hex characters (constant per MCU).
 * Host builds link tests/stubs/boot_id_stub.cpp instead.
 */
#pragma once
#include <stdint.h>

namespace Platform {

uint32_t bootId();
/// Why the MCU last reset, from RCC CSR at the first call (then cleared): "PIN", "POR",
/// "SFT", "IWDG", "WWDG", "LPWR", "BOR", joined with '+' when several flags are set.
const char* resetCause();
const char* deviceSerial();

} // namespace Platform
