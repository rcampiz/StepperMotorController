/**
 * @file dispatch_result.hpp
 * @brief Shared return type for dispatcher command results
 */

#pragma once

namespace Comms {

/**
 * @brief Result of a dispatched command
 */
struct DispatchResult {
    bool ok;
    const char* message;  // error string on failure, nullptr on success
};

} // namespace Comms
