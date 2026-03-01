/**
 * @file trace.cpp
 * @brief Trace ring buffer implementation
 *
 * Uses PRIMASK interrupt masking for ISR-safe constant-time writes.
 * Ring buffer overwrites oldest entries when full.
 * No RTOS dependency.
 */

#include "L3_services/infra/trace.hpp"
#include "F_platform/hw/interrupt_guard.hpp"
#include "L3_services/infra/tick_timer.hpp"
#include <string.h>

namespace Trace {

static Entry s_ring[RING_SIZE];
static size_t s_head = 0;      // Next write position
static size_t s_total = 0;     // Total entries ever written

// Thread-local-like context (single-core Cortex-M, set by each task at loop top)
static uint8_t s_currentTaskId = 0;
static uint8_t s_currentServiceId = 0;

void record(Dir d, const char* tag, uint32_t arg0) {
    InterruptGuard guard;

    Entry &e = s_ring[s_head];
    e.tick = Services::TickTimer_GetTick();
    e.tag = tag;
    e.arg0 = arg0;
    e.dir = d;
    e.boundary = 0;
    e.serviceId = s_currentServiceId;
    e.taskId = s_currentTaskId;
    e.method = nullptr;
    e.detail[0] = '\0';

    s_head = (s_head + 1) % RING_SIZE;
    s_total++;
}

void recordIface(uint8_t boundary, const char* label, const char* method,
                 uint32_t arg0, const char* detail) {
    InterruptGuard guard;

    Entry &e = s_ring[s_head];
    e.tick = Services::TickTimer_GetTick();
    e.tag = label;
    e.arg0 = arg0;
    e.dir = ENTRY;
    e.boundary = boundary;
    e.serviceId = s_currentServiceId;
    e.taskId = s_currentTaskId;
    e.method = method;

    if (detail != nullptr) {
        strncpy(e.detail, detail, DETAIL_SIZE - 1);
        e.detail[DETAIL_SIZE - 1] = '\0';
    } else {
        e.detail[0] = '\0';
    }

    s_head = (s_head + 1) % RING_SIZE;
    s_total++;
}

size_t getCount() {
    return (s_total < RING_SIZE) ? s_total : RING_SIZE;
}

size_t getTotal() {
    return s_total;
}

bool getEntry(size_t index, Entry &out) {
    size_t count = getCount();
    if (index >= count) return false;

    size_t start = (s_total <= RING_SIZE) ? 0 : s_head;
    size_t actual = (start + index) % RING_SIZE;
    out = s_ring[actual];
    return true;
}

void reset() {
    InterruptGuard guard;
    s_head = 0;
    s_total = 0;
}

void setCurrentTaskId(uint8_t id) {
    s_currentTaskId = id;
}

void setCurrentServiceId(uint8_t id) {
    s_currentServiceId = id;
}

ServiceScope::ServiceScope(uint8_t id) {
    s_currentServiceId = id;
}

ServiceScope::~ServiceScope() {
    s_currentServiceId = SVC_NONE;
}

} // namespace Trace
