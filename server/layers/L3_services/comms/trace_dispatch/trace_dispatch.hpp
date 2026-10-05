/**
 * @file trace_dispatch.hpp
 * @brief L3 implementation of ITraceDispatcher
 */

#pragma once

#include "L3_services/boundary/itrace_dispatcher.hpp"

namespace Services {

class TraceDispatch : public Services::ITraceDispatcher {
public:
    uint32_t traceGetCount() override;
    bool traceGetEntry(uint32_t index, TraceEntryData& out) override;
    void traceReset() override;
    void traceRecordEntry(const char* tag, uint32_t arg0 = 0) override;
    void traceRecordExit(const char* tag, uint32_t arg0 = 0) override;
};

} // namespace Services
