/**
 * @file itrace_sink.hpp
 * @brief Abstract trace record sink — decouples F_util from L3 Trace
 *
 * Implemented by the L3 Trace ring buffer. Injected into ITrace at init.
 */

#pragma once

#include <stdint.h>

class ITraceSink {
public:
    virtual ~ITraceSink() = default;

    virtual void recordIface(uint16_t boundary, const char* label,
                             const char* method, uint32_t result,
                             const char* detail = nullptr) = 0;
};
