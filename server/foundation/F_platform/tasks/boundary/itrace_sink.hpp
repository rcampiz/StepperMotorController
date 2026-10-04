/**
 * @file boundary/itracesink.hpp
 * @brief Consumer port — Scheduler side of the Services::ITraceSink harness connection
 *
 * Structurally identical to Services::ITraceSink.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

#include <stdint.h>

namespace Scheduler {

class ITraceSink {
public:
    virtual ~ITraceSink() = default;

    virtual void recordIface(uint16_t boundary, const char* label,
                             const char* method, uint32_t result,
                             const char* detail = nullptr) = 0;
};

} // namespace Scheduler
