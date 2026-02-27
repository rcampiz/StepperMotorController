/**
 * @file trace.cpp
 * @brief Trace ring buffer implementation
 *
 * Uses FreeRTOS BASEPRI masking for ISR-safe constant-time writes.
 * Ring buffer overwrites oldest entries when full.
 */

#include "L3_services/infra/trace.hpp"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/task.h"

namespace Trace {

static Entry s_ring[RING_SIZE];
static size_t s_head = 0;      // Next write position
static size_t s_total = 0;     // Total entries ever written

void record(Dir d, const char* tag, uint32_t arg0) {
    UBaseType_t saved = portSET_INTERRUPT_MASK_FROM_ISR();

    Entry &e = s_ring[s_head];
    e.tick = xTaskGetTickCount();
    e.tag = tag;
    e.arg0 = arg0;
    e.dir = d;

    s_head = (s_head + 1) % RING_SIZE;
    s_total++;

    portCLEAR_INTERRUPT_MASK_FROM_ISR(saved);
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
    UBaseType_t saved = portSET_INTERRUPT_MASK_FROM_ISR();
    s_head = 0;
    s_total = 0;
    portCLEAR_INTERRUPT_MASK_FROM_ISR(saved);
}

} // namespace Trace
