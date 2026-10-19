/**
 * @file harness_meter.hpp
 * @brief The harness meter: always-on counters for every harness crossing.
 *
 * The layers are sheets and the tasks are threads of execution that cross them (the two
 * are orthogonal). The meter shows the intersection: for each harness signal, how many
 * crossings, how many bytes, how long inside, and which task crossed. It is part of the
 * trace global net (any sheet may include it) and costs a few atomic adds a crossing, so
 * it is on in every build; the interface trace (ITrace) stays debug-only.
 *
 * Fed by: the taps (harness/taps, applied only in harness_connect.cpp), the task sheet
 * (H_TASKS_L3) and the L2 command router (H_L2_L3 commands). Read by DIAG:HARNESS? and
 * the LCD Architecture screen. Counters only grow (32-bit, wrap); readers take deltas.
 *
 * Header-only (C++14: function-local statics, zero-initialised, no guard). The counting
 * functions are kept out of line (one shared copy): inlined into every metered method they
 * cost ~36 KB of flash in the -O2 build.
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include "F_platform/boundary/imicrosecond_clock.hpp"

#if defined(__GNUC__)
#define SMC_METER_NOINLINE __attribute__((noinline))
#else
#define SMC_METER_NOINLINE
#endif

namespace Harness {
namespace Meter {

/// One entry per metered harness signal, grouped by harness
enum Net : uint8_t {
    // H_TASKS_L3: each task's one entry into L3
    TASKS_MOTOR = 0,
    TASKS_COMMS,
    TASKS_ENCODER,
    TASKS_DISPLAY,
    // H_L1_L2
    L1_L2_TRANSPORT,
    // H_L2_L3: host commands down, the L3 -> L2 processor up (process, telemetry, events)
    L2_L3_COMMANDS,
    L3_L2_PROCESSOR,
    // H_L3_L4
    L3_L4_MOTOR,
    L3_L4_CANVAS,
    L3_L4_ENCODER,
    L3_L4_FLASH,
    // H_L4_L5
    L4_L5_SPI1_MOTOR,
    L4_L5_SPI1_LCD,
    L4_L5_SPI2_FLASH,
    // X_L5_L1: host bytes, the one documented skip
    L5_L1_BYTES,
    NET_COUNT
};

/// Task slots follow Harness::TaskId (0 unknown, 1 motor, 2 encoder, 3 comms, 4 display, 5 timer)
constexpr uint8_t TASK_SLOTS = 6;

struct NetInfo {
    const char* harness;   ///< "H_L3_L4"
    const char* signal;    ///< "MOTOR"
};

inline const NetInfo& info(uint8_t net)
{
    static const NetInfo TABLE[NET_COUNT] = {
        {"H_TASKS_L3", "MOTOR"}, {"H_TASKS_L3", "COMMS"}, {"H_TASKS_L3", "ENCODER"}, {"H_TASKS_L3", "DISPLAY"},
        {"H_L1_L2", "TRANSPORT"},
        {"H_L2_L3", "COMMANDS"}, {"H_L2_L3", "PROCESSOR"},
        {"H_L3_L4", "MOTOR"}, {"H_L3_L4", "CANVAS"}, {"H_L3_L4", "ENCODER"}, {"H_L3_L4", "FLASH"},
        {"H_L4_L5", "SPI1_MOTOR"}, {"H_L4_L5", "SPI1_LCD"}, {"H_L4_L5", "SPI2_FLASH"},
        {"X_L5_L1", "BYTES"},
    };
    return TABLE[net < NET_COUNT ? net : 0];
}

/// A copy of one net's counters
struct Counters {
    uint32_t calls;
    uint32_t bytes;
    uint32_t busyUs;                 ///< time spent inside the crossing (or holding the bus)
    uint32_t byTask[TASK_SLOTS];     ///< calls, by the task that crossed
};

/// The counters (zero-initialised static storage; trivial atomics, no constructor runs)
struct Store {
    std::atomic<uint32_t> calls[NET_COUNT];
    std::atomic<uint32_t> bytes[NET_COUNT];
    std::atomic<uint32_t> busyUs[NET_COUNT];
    std::atomic<uint32_t> byTask[NET_COUNT][TASK_SLOTS];
    Platform::IMicrosecondClock* clock;
    uint8_t (*taskId)();
};

inline Store& store()
{
    static Store s;
    return s;
}

/// Wiring (composition root / harness): the microsecond clock for busy time, and where the
/// current task id comes from (the trace context the task sheet sets). Both optional.
inline void setClock(Platform::IMicrosecondClock* clock) { store().clock = clock; }
inline void setTaskSource(uint8_t (*taskId)()) { store().taskId = taskId; }

SMC_METER_NOINLINE inline uint32_t nowUs()
{
    Platform::IMicrosecondClock* c = store().clock;
    return (c != nullptr) ? c->getTickUs() : 0u;
}

SMC_METER_NOINLINE inline uint8_t currentTask()
{
    uint8_t (*fn)() = store().taskId;
    const uint8_t t = (fn != nullptr) ? fn() : 0u;
    return (t < TASK_SLOTS) ? t : 0u;
}

/// One crossing: a call, bytes carried, time inside
SMC_METER_NOINLINE inline void count(Net net, uint32_t bytes = 0, uint32_t busyUs = 0)
{
    if (net >= NET_COUNT) return;
    Store& s = store();
    s.calls[net].fetch_add(1u, std::memory_order_relaxed);
    if (bytes != 0u) s.bytes[net].fetch_add(bytes, std::memory_order_relaxed);
    if (busyUs != 0u) s.busyUs[net].fetch_add(busyUs, std::memory_order_relaxed);
    s.byTask[net][currentTask()].fetch_add(1u, std::memory_order_relaxed);
}

/// Bytes only (a stream already counted as one crossing)
SMC_METER_NOINLINE inline void addBytes(Net net, uint32_t bytes)
{
    if (net < NET_COUNT && bytes != 0u) store().bytes[net].fetch_add(bytes, std::memory_order_relaxed);
}

/// Busy time only (a bus held from lock to unlock)
SMC_METER_NOINLINE inline void addBusy(Net net, uint32_t us)
{
    if (net < NET_COUNT && us != 0u) store().busyUs[net].fetch_add(us, std::memory_order_relaxed);
}

/// The end of a timed crossing (out of line, shared by every Scope)
SMC_METER_NOINLINE inline void finish(Net net, uint32_t bytes, uint32_t t0)
{
    count(net, bytes, nowUs() - t0);
}

/// A timed crossing: counts on destruction, with the time spent inside
class Scope {
public:
    explicit Scope(Net net, uint32_t bytes = 0) : m_net(net), m_bytes(bytes), m_t0(nowUs()) {}
    ~Scope() { finish(m_net, m_bytes, m_t0); }
    void addBytes(uint32_t b) { m_bytes += b; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    Net m_net;
    uint32_t m_bytes;
    uint32_t m_t0;
};

inline void read(uint8_t net, Counters& out)
{
    if (net >= NET_COUNT) { out = Counters{}; return; }
    Store& s = store();
    out.calls = s.calls[net].load(std::memory_order_relaxed);
    out.bytes = s.bytes[net].load(std::memory_order_relaxed);
    out.busyUs = s.busyUs[net].load(std::memory_order_relaxed);
    for (uint8_t t = 0; t < TASK_SLOTS; t++) out.byTask[t] = s.byTask[net][t].load(std::memory_order_relaxed);
}

} // namespace Meter
} // namespace Harness
