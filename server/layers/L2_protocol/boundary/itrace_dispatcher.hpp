/**
 * @file boundary/itracedispatcher.hpp
 * @brief Consumer port — Protocol side of the Services::ITraceDispatcher harness connection
 *
 * Structurally identical to Services::ITraceDispatcher.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

#include <stdint.h>

namespace Protocol {

class ITraceDispatcher {
public:
    virtual ~ITraceDispatcher() = default;

    struct TraceEntryData {
        uint32_t tick;
        const char* tag;
        uint32_t arg0;
        uint8_t dir;       // 0=ENTRY, 1=EXIT
        const char* method;
    };

    virtual uint32_t traceGetCount() = 0;
    virtual bool traceGetEntry(uint32_t index, TraceEntryData& out) = 0;
    virtual void traceReset() = 0;
    virtual void traceRecordEntry(const char* tag, uint32_t arg0 = 0) = 0;
    virtual void traceRecordExit(const char* tag, uint32_t arg0 = 0) = 0;
};

} // namespace Protocol
