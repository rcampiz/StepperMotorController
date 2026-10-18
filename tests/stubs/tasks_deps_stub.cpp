/**
 * @file tasks_deps_stub.cpp
 * @brief Host stand-in for the trace ring read side (Trace::getTotal/getCount/getEntry), with
 *        a test hook to fill it: the task monitor's timeline reads it.
 *
 * The real ring (L3_services/infra/trace) needs the interrupt guard on target; this one is a
 * plain array. trace_stub.cpp supplies the write-side context setters.
 */
#include "L3_services/infra/trace/trace.hpp"
#include <string.h>

namespace TraceStub {

static Trace::Entry s_ring[Trace::RING_SIZE];
static size_t s_count = 0;   // valid entries (saturates at RING_SIZE)
static size_t s_total = 0;   // entries ever pushed
static size_t s_head = 0;    // next write slot

void clear()
{
    memset(s_ring, 0, sizeof s_ring);
    s_count = s_total = s_head = 0;
}

void push(const Trace::Entry& e)
{
    s_ring[s_head] = e;
    s_head = (s_head + 1) % Trace::RING_SIZE;
    if (s_count < Trace::RING_SIZE) s_count++;
    s_total++;
}

} // namespace TraceStub

namespace Trace {

size_t getTotal() { return TraceStub::s_total; }
size_t getCount() { return TraceStub::s_count; }

bool getEntry(size_t index, Entry& out)
{
    if (index >= TraceStub::s_count) return false;
    size_t oldest = (TraceStub::s_head + RING_SIZE - TraceStub::s_count) % RING_SIZE;
    out = TraceStub::s_ring[(oldest + index) % RING_SIZE];
    return true;
}

} // namespace Trace
