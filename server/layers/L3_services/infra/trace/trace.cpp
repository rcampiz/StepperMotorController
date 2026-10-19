/**
 * @file trace.cpp
 * @brief Trace ring buffer implementation
 *
 * A writer claims its slot with one atomic add on the running total, then fills it: tasks,
 * ISRs and, on a dual-core MCU, both cores record without masking interrupts or taking a
 * lock (it used PRIMASK, which a second core ignores). A reader may see a slot that is
 * being written: diagnostics only, as before. Ring buffer overwrites oldest entries when full.
 * No RTOS dependency.
 */

#include "L3_services/infra/trace/trace.hpp"
#include "L3_services/infra/tick_timer/tick_timer.hpp"
#include <atomic>
#include <string.h>

namespace Trace {

static_assert((RING_SIZE & (RING_SIZE - 1)) == 0,
              "RING_SIZE must be a power of two: slot = total % RING_SIZE across the 32-bit wrap");

static Entry s_ring[RING_SIZE];
static std::atomic<uint32_t> s_total{0};   // Total entries ever written; the next slot is total % RING_SIZE

/// The next slot, claimed (distinct for concurrent writers)
static Entry& claim()
{
    const uint32_t n = s_total.fetch_add(1u, std::memory_order_relaxed);
    return s_ring[n % RING_SIZE];
}

void record(Dir d, const char* tag, uint32_t arg0) {
    Entry &e = claim();
    e.tick = Services::TickTimer_GetTick();
    e.tag = tag;
    e.arg0 = arg0;
    e.dir = d;
    e.boundary = 0;
    e.serviceId = Harness::getTraceServiceId();
    e.taskId = Harness::getTraceTaskId();
    e.method = nullptr;
    e.detail[0] = '\0';
}

void recordIface(uint16_t boundary, const char* label, const char* method,
                 uint32_t arg0, const char* detail) {
    Entry &e = claim();
    e.tick = Services::TickTimer_GetTick();
    e.tag = label;
    e.arg0 = arg0;
    e.dir = ENTRY;
    e.boundary = boundary;
    e.serviceId = Harness::getTraceServiceId();
    e.taskId = Harness::getTraceTaskId();
    e.method = method;

    if (detail != nullptr) {
        strncpy(e.detail, detail, DETAIL_SIZE - 1);
        e.detail[DETAIL_SIZE - 1] = '\0';
    } else {
        e.detail[0] = '\0';
    }
}

size_t getCount() {
    const uint32_t total = s_total.load(std::memory_order_relaxed);
    return (total < RING_SIZE) ? total : RING_SIZE;
}

size_t getTotal() {
    return s_total.load(std::memory_order_relaxed);
}

bool getEntry(size_t index, Entry &out) {
    const uint32_t total = s_total.load(std::memory_order_relaxed);
    const size_t count = (total < RING_SIZE) ? total : RING_SIZE;
    if (index >= count) return false;

    const size_t start = (total <= RING_SIZE) ? 0 : (total % RING_SIZE);   // oldest entry
    out = s_ring[(start + index) % RING_SIZE];
    return true;
}

void reset() {
    s_total.store(0u, std::memory_order_relaxed);
}

void setCurrentTaskId(uint8_t id) {
    Harness::setTraceTaskId(id);
}

void setCurrentServiceId(uint8_t id) {
    Harness::setTraceServiceId(id);
}

ServiceScope::ServiceScope(uint8_t id) {
    Harness::setTraceServiceId(id);
}

ServiceScope::~ServiceScope() {
    Harness::setTraceServiceId(SVC_NONE);
}

} // namespace Trace

#include "harness/trace/interface_trace.hpp"

namespace {
TraceSinkAdapter s_traceSink;
} // namespace

void Trace::initTraceSink() {
    Harness::ITrace::setSink(&s_traceSink);
    Harness::ITrace::init();
}
