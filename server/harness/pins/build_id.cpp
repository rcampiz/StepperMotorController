/**
 * @file build_id.cpp
 * @brief The only translation unit that sees GIT_HASH (see protocol_version.hpp)
 */
#include "harness/pins/protocol_version.hpp"

#ifndef GIT_HASH
#define GIT_HASH "unknown"
#endif

namespace Harness {

const char* buildId() { return GIT_HASH; }

} // namespace Harness
