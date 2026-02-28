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

namespace Trace {

static Entry s_ring[RING_SIZE];
static size_t s_head = 0;      // Next write position
static size_t s_total = 0;     // Total entries ever written

void record(Dir d, const char* tag, uint32_t arg0) {
    InterruptGuard guard;

    Entry &e = s_ring[s_head];
    e.tick = Services::TickTimer_GetTick();
    e.tag = tag;
    e.arg0 = arg0;
    e.dir = d;

    s_head = (s_head + 1) % RING_SIZE;
    s_total++;
}

size_t getCount() {
    return (s_total < RING_SIZE) ? s_total : RING_SIZE;
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

} // namespace Trace
