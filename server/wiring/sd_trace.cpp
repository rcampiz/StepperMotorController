/**
 * @file sd_trace.cpp
 * @brief ServiceDispatcher — ITraceDispatcher implementation
 */

#include "service_dispatcher.hpp"
#include "L3_services/infra/trace.hpp"

uint32_t ServiceDispatcher::traceGetCount() {
    return static_cast<uint32_t>(Trace::getCount());
}

bool ServiceDispatcher::traceGetEntry(uint32_t index, TraceEntryData& out) {
    Trace::Entry e;
    if (!Trace::getEntry(static_cast<size_t>(index), e)) return false;
    out.tick = e.tick;
    out.tag = e.tag;
    out.arg0 = e.arg0;
    out.dir = static_cast<uint8_t>(e.dir);
    out.method = e.method;
    return true;
}

void ServiceDispatcher::traceReset() {
    Trace::reset();
}

void ServiceDispatcher::traceRecordEntry(const char* tag, uint32_t arg0) {
    Trace::record(Trace::ENTRY, tag, arg0);
}

void ServiceDispatcher::traceRecordExit(const char* tag, uint32_t arg0) {
    Trace::record(Trace::EXIT, tag, arg0);
}
