/**
 * @file trace_dispatch.cpp
 * @brief L3 ITraceDispatcher implementation
 */

#include "trace_dispatch.hpp"
#include "L3_services/infra/trace/trace.hpp"

namespace Services {

uint32_t TraceDispatch::traceGetCount() {
    return static_cast<uint32_t>(Trace::getCount());
}

bool TraceDispatch::traceGetEntry(uint32_t index, TraceEntryData& out) {
    Trace::Entry e;
    if (!Trace::getEntry(static_cast<size_t>(index), e)) return false;
    out.tick = e.tick;
    out.tag = e.tag;
    out.arg0 = e.arg0;
    out.dir = static_cast<uint8_t>(e.dir);
    out.method = e.method;
    return true;
}

void TraceDispatch::traceReset() {
    Trace::reset();
}

void TraceDispatch::traceRecordEntry(const char* tag, uint32_t arg0) {
    Trace::record(Trace::ENTRY, tag, arg0);
}

void TraceDispatch::traceRecordExit(const char* tag, uint32_t arg0) {
    Trace::record(Trace::EXIT, tag, arg0);
}

} // namespace Services
