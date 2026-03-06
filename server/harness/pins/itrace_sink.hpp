/**
 * @file itrace_sink.hpp
 * @brief Abstract trace record sink — decouples harness trace from L3 ring buffer
 *
 * Implemented by the L3 Trace ring buffer. Injected into ITrace at init.
 */

#pragma once

#include <stdint.h>

namespace Harness {

class ITraceSink {
public:
    virtual ~ITraceSink() = default;

    virtual void recordIface(uint16_t boundary, const char* label,
                             const char* method, uint32_t result,
                             const char* detail = nullptr) = 0;
};

} // namespace Harness
