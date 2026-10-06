/**
 * @file protocol_version.hpp
 * @brief The single source of truth for firmware and protocol identity (F17)
 *
 * Reported by `*IDN?` as `<vendor>,<model>,<serial>,<fw>,<proto>`, by the `!RESET` line
 * (`!RESET boot=<id> fw=<fw> proto=<n> build=<hash>`) and by `SYST:VER?`.
 * docs/PROTOCOL.md is generated against PROTOCOL_VERSION; clients refuse an unknown value.
 */
#pragma once
#include <stdint.h>

#ifndef GIT_HASH
#define GIT_HASH "unknown"
#endif

namespace Harness {

constexpr uint8_t     PROTOCOL_VERSION = 1;          // bumped to 2 at the end of the protocol pass
constexpr const char* FW_VERSION       = "0.4.0";
constexpr const char* IDN_VENDOR       = "SMC";
constexpr const char* IDN_MODEL        = "NUCLEO-F401RE-IHM03A1";
constexpr const char* BUILD_HASH       = GIT_HASH;

} // namespace Harness
