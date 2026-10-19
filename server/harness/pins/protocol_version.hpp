/**
 * @file protocol_version.hpp
 * @brief Firmware and protocol identity (F17)
 *
 * Reported by `*IDN?` as `<vendor>,<model>,<serial>,<fw>,<proto>`, by the `!RESET` line
 * (`!RESET boot=<id> fw=<fw> proto=<n> build=<id>`), by `SYST:VER?` and on the LCD saver.
 *
 *  - FW_VERSION: the release, from the VERSION file at the repository root (SemVer), which
 *    CMake passes in as SMC_FW_VERSION (cmake/smc_version.cmake). The client package carries
 *    the same number. Bumped when a release is cut (CHANGELOG.md).
 *  - PROTOCOL_VERSION: the wire protocol, independent of the release; it changes only when
 *    the protocol does. docs/PROTOCOL.md is generated against it; clients refuse an unknown value.
 *  - buildId(): which tree was compiled, regenerated on every build (see build_id.cpp).
 */
#pragma once
#include <stdint.h>

#ifndef SMC_FW_VERSION
#define SMC_FW_VERSION "0.0.0"       // a build that did not go through cmake/smc_version.cmake
#endif

namespace Harness {

constexpr uint8_t     PROTOCOL_VERSION = 2;          // protocol pass of 2026-10-06; docs/PROTOCOL.md section 7
constexpr const char* FW_VERSION       = SMC_FW_VERSION;
constexpr const char* IDN_VENDOR       = "SMC";
constexpr const char* IDN_MODEL        = "NUCLEO-F401RE-IHM03A1";
/// `git describe` of the tree the firmware was built from: "v0.5.0" on a release tag,
/// "v0.5.0-3-g<hash>" after it, a short hash before the first tag; "+dirty" when the
/// firmware's sources had uncommitted changes, "nogit" without git. Generated on every build
/// and seen only by build_id.cpp, so a new commit recompiles one file.
const char* buildId();

} // namespace Harness
