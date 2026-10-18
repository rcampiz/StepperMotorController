/**
 * @file trace_screen_stub.cpp
 * @brief Host stand-in for the trace ring (L3_services/infra/trace), serving a realistic
 *        repeating mix of interface traces for the Trace screen snapshots.
 *
 * The pattern follows one SCPI command through the layers (transport, dispatch, command
 * sink, motor driver, SPI, mutex), plus encoder reads, safety checks, telemetry, LCD and
 * flash traffic and a legacy tag entry, with the service and task ids the firmware stamps.
 * TraceStub::push(n) appends n more entries; the ring keeps the newest RING_SIZE.
 */
#include "L3_services/infra/trace/trace.hpp"
#include <string.h>

namespace {

struct Pattern {
    uint16_t boundary;
    const char* tag;
    const char* method;
    uint32_t arg0;
    const char* detail;
    uint8_t serviceId;
    uint8_t taskId;
    Trace::Dir dir;
};

constexpr Pattern PATTERN[] = {
    { 0x001, "[L1>L2]", "rx",              0,      "MOT:RUN",  6, 3, Trace::ENTRY },
    { 0x002, "[L2>L3]", "dispatch",        0,      "MOT:RUN",  8, 3, Trace::ENTRY },
    { 0x020, "[L3>L3]", "enqueue",         1,      "run",      8, 3, Trace::ENTRY },
    { 0x004, "[L3>L4]", "motor.run",       1500,   "500,fwd",  1, 1, Trace::ENTRY },
    { 0x040, "[L4>L5]", "spi.xfer",        0,      "RUN",      1, 1, Trace::ENTRY },
    { 0x080, "[L5>F]",  "lock.acquire",    0,      nullptr,    1, 1, Trace::ENTRY },
    { 0x080, "[L5<F]",  "lock.release",    0,      nullptr,    1, 1, Trace::EXIT  },
    { 0x004, "[L3<L4]", "motor.getStatus", 0x7E03, nullptr,    1, 1, Trace::EXIT  },
    { 0x008, "[L3>L4]", "enc.read",        12017,  nullptr,    4, 2, Trace::ENTRY },
    { 0x010, "[L3~L3]", "safety.check",    0,      "ok",       3, 5, Trace::ENTRY },
    { 0x400, "[L2>L1]", "event",           0,      "TLM",      6, 3, Trace::ENTRY },
    { 0x100, "[F>L4]",  "fill",            0,      "240x12",   7, 4, Trace::ENTRY },
    { 0x002, "[L2<L3]", "dispatch",        0,      "OK",       8, 3, Trace::EXIT  },
    { 0x000, "MOT:RUN", nullptr,           150,    "",         1, 1, Trace::ENTRY },
    { 0x200, "[L3>L4]", "flash.save",      0,      "config",   2, 3, Trace::ENTRY },
    { 0x004, "[L3>L4]", "setParam",        0x16,   "STEP_MODE",1, 1, Trace::ENTRY },
    { 0x004, "[L3>L4]", "motor.move",      200,    "sysid",    5, 1, Trace::ENTRY },
    { 0x008, "[L3<L4]", "enc.velocity",    0,      nullptr,    4, 2, Trace::EXIT  },
};
constexpr size_t PATTERN_LEN = sizeof(PATTERN) / sizeof(PATTERN[0]);

Trace::Entry g_ring[Trace::RING_SIZE];
size_t g_total = 0;   // entries ever written
size_t g_count = 0;   // valid entries (saturates at RING_SIZE)

} // namespace

namespace TraceStub {

/// Append n entries of the pattern (continuing where the last call stopped)
void push(size_t n)
{
    for (size_t k = 0; k < n; k++) {
        const Pattern& p = PATTERN[g_total % PATTERN_LEN];
        Trace::Entry& e = g_ring[g_total % Trace::RING_SIZE];
        e.tick = static_cast<uint32_t>(g_total * 250u);
        e.tag = p.tag;
        e.arg0 = p.arg0;
        e.dir = p.dir;
        e.boundary = p.boundary;
        e.serviceId = p.serviceId;
        e.taskId = p.taskId;
        e.method = p.method;
        memset(e.detail, 0, sizeof e.detail);
        if (p.detail != nullptr) strncpy(e.detail, p.detail, sizeof e.detail - 1);
        g_total++;
        if (g_count < Trace::RING_SIZE) g_count++;
    }
}

void clear() { g_total = 0; g_count = 0; }

} // namespace TraceStub

namespace Trace {

size_t getTotal() { return g_total; }
size_t getCount() { return g_count; }

bool getEntry(size_t index, Entry& out)
{
    if (index >= g_count) return false;
    size_t oldest = g_total - g_count;
    out = g_ring[(oldest + index) % RING_SIZE];
    return true;
}

void setCurrentServiceId(uint8_t) {}
void setCurrentTaskId(uint8_t) {}

} // namespace Trace
