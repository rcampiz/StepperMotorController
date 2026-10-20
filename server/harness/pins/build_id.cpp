/**
 * @file build_id.cpp
 * @brief The only translation unit that sees the build id (see protocol_version.hpp)
 *
 * Firmware and virtual Nucleo builds generate smc_build_id.h on every build
 * (cmake/smc_build_id.cmake); the host tests define SMC_BUILD_ID directly.
 */
#include "harness/pins/protocol_version.hpp"

#ifdef SMC_HAVE_BUILD_ID_H
#include "smc_build_id.h"
#endif
#ifndef SMC_BUILD_ID
#define SMC_BUILD_ID "unknown"
#endif
#ifndef SMC_VERSION_FULL
#define SMC_VERSION_FULL SMC_FW_VERSION     // no generated header (host tests): the release
#endif

namespace Harness {

const char* buildId() { return SMC_BUILD_ID; }
const char* fwVersion() { return SMC_VERSION_FULL; }

} // namespace Harness
