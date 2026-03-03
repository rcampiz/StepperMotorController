/**
 * @file trace.hpp
 * @brief Selective constant-time trace ring buffer
 *
 * Static ring buffer for lightweight tracing at Transport, Motion,
 * and Safety boundaries. No heap, constant time per record.
 *
 * Usage:
 *   TRACE_ENTRY("MOT:RUN", speedArg);
 *   TRACE_EXIT("MOT:RUN", static_cast<uint32_t>(result));
 */

#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Trace {

enum Dir : uint8_t { ENTRY, EXIT };

static constexpr size_t DETAIL_SIZE = 16;

// Service context identifiers (set before traced calls)
enum ServiceId : uint8_t {
    SVC_NONE     = 0,
    SVC_MOTION   = 1,
    SVC_CONFIG   = 2,
    SVC_SAFETY   = 3,
    SVC_ENCODER  = 4,
    SVC_SYSID    = 5,
    SVC_COMMS    = 6,
    SVC_UI       = 7,
    SVC_DISPATCH = 8,
    SVC_COUNT    = 9,
};

// Task identifiers (set at top of each task's main loop)
enum TaskId : uint8_t {
    TASK_UNKNOWN  = 0,
    TASK_MOTOR    = 1,
    TASK_ENCODER  = 2,
    TASK_COMMS    = 3,
    TASK_DISPLAY  = 4,
    TASK_TIMER    = 5,
    TASK_COUNT    = 6,
};

struct Entry {
    uint32_t tick;       // Microsecond timestamp (TickTimer) at record time
    const char* tag;     // String literal tag (e.g. "MOT:RUN") or label ("[L3>L4]")
    uint32_t arg0;       // Context-dependent argument
    Dir dir;             // ENTRY or EXIT
    uint16_t boundary;   // 0 = legacy, ITrace::Boundary for interface traces
    uint8_t serviceId;   // ServiceId of the initiating service
    uint8_t taskId;      // TaskId of the executing RTOS task
    const char* method;  // nullptr for legacy, method name for interface traces
    char detail[DETAIL_SIZE];  // Short copy of detail string (e.g. SCPI cmd name)
};

static constexpr size_t RING_SIZE = 128;

/**
 * @brief Record a trace entry (ISR-safe, constant time)
 */
void record(Dir d, const char* tag, uint32_t arg0 = 0);

/**
 * @brief Record an interface trace entry with boundary + method info
 */
void recordIface(uint16_t boundary, const char* label, const char* method,
                 uint32_t arg0 = 0, const char* detail = nullptr);

/**
 * @brief Number of valid entries in ring (max RING_SIZE)
 */
size_t getCount();

/**
 * @brief Total entries ever written (monotonically increasing)
 *
 * Use to detect new entries even after ring is full (getCount() saturates).
 */
size_t getTotal();

/**
 * @brief Get entry by index (0 = oldest valid entry)
 * @return false if index >= getCount()
 */
bool getEntry(size_t index, Entry &out);

/**
 * @brief Clear the ring buffer
 */
void reset();

/**
 * @brief Set the current task context for subsequent trace entries
 *
 * Call at the top of each RTOS task's main loop. The value persists
 * until the next call (thread-local-like, single-core safe).
 */
void setCurrentTaskId(uint8_t id);

/**
 * @brief Set the current service context for subsequent trace entries
 */
void setCurrentServiceId(uint8_t id);

/**
 * @brief RAII guard that sets service context on construction and resets on destruction
 */
struct ServiceScope {
    explicit ServiceScope(uint8_t id);
    ~ServiceScope();
    ServiceScope(const ServiceScope&) = delete;
    ServiceScope& operator=(const ServiceScope&) = delete;
};

} // namespace Trace

#include "F_platform/hal/itrace_sink.hpp"

/**
 * @brief Adapter: forwards ITraceSink calls to Trace::recordIface()
 *
 * Single static instance created in system_init and passed to ITrace::setSink().
 */
class TraceSinkAdapter : public ITraceSink {
public:
    void recordIface(uint16_t boundary, const char* label,
                     const char* method, uint32_t result,
                     const char* detail = nullptr) override {
        Trace::recordIface(boundary, label, method, result, detail);
    }
};

#define TRACE_ENTRY(tag, ...) Trace::record(Trace::ENTRY, tag, ##__VA_ARGS__)
#define TRACE_EXIT(tag, ...)  Trace::record(Trace::EXIT,  tag, ##__VA_ARGS__)
