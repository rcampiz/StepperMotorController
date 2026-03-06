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
#include "harness/pins/itrace_context.hpp"

namespace Trace {

enum Dir : uint8_t { ENTRY, EXIT };

static constexpr size_t DETAIL_SIZE = 16;

// Re-export harness enums for backward compatibility
using Harness::SVC_NONE;
using Harness::SVC_MOTION;
using Harness::SVC_CONFIG;
using Harness::SVC_SAFETY;
using Harness::SVC_ENCODER;
using Harness::SVC_SYSID;
using Harness::SVC_COMMS;
using Harness::SVC_UI;
using Harness::SVC_DISPATCH;
using Harness::SVC_COUNT;

using Harness::TASK_UNKNOWN;
using Harness::TASK_MOTOR;
using Harness::TASK_ENCODER;
using Harness::TASK_COMMS;
using Harness::TASK_DISPLAY;
using Harness::TASK_TIMER;
using Harness::TASK_COUNT;

struct Entry {
    uint32_t tick;       // Microsecond timestamp (TickTimer) at record time
    const char* tag;     // String literal tag (e.g. "MOT:RUN") or label ("[L3>L4]")
    uint32_t arg0;       // Context-dependent argument
    Dir dir;             // ENTRY or EXIT
    uint16_t boundary;   // 0 = legacy, Harness::ITrace::Boundary for interface traces
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

/**
 * @brief Initialize the trace sink (wires TraceSinkAdapter to ITrace)
 */
void initTraceSink();

} // namespace Trace

#include "harness/pins/itrace_sink.hpp"

/**
 * @brief Adapter: forwards ITraceSink calls to Trace::recordIface()
 *
 * Single static instance created in system_init and passed to Harness::ITrace::setSink().
 */
class TraceSinkAdapter : public Harness::ITraceSink {
public:
    void recordIface(uint16_t boundary, const char* label,
                     const char* method, uint32_t result,
                     const char* detail = nullptr) override {
        Trace::recordIface(boundary, label, method, result, detail);
    }
};

#define TRACE_ENTRY(tag, ...) Trace::record(Trace::ENTRY, tag, ##__VA_ARGS__)
#define TRACE_EXIT(tag, ...)  Trace::record(Trace::EXIT,  tag, ##__VA_ARGS__)
