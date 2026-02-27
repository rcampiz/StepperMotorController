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

struct Entry {
    uint32_t tick;       // FreeRTOS tick at record time
    const char* tag;     // String literal tag (e.g. "MOT:RUN")
    uint32_t arg0;       // Context-dependent argument
    Dir dir;             // ENTRY or EXIT
};

static constexpr size_t RING_SIZE = 128;

/**
 * @brief Record a trace entry (ISR-safe, constant time)
 */
void record(Dir d, const char* tag, uint32_t arg0 = 0);

/**
 * @brief Number of valid entries in ring (max RING_SIZE)
 */
size_t getCount();

/**
 * @brief Get entry by index (0 = oldest valid entry)
 * @return false if index >= getCount()
 */
bool getEntry(size_t index, Entry &out);

/**
 * @brief Clear the ring buffer
 */
void reset();

} // namespace Trace

#define TRACE_ENTRY(tag, ...) Trace::record(Trace::ENTRY, tag, ##__VA_ARGS__)
#define TRACE_EXIT(tag, ...)  Trace::record(Trace::EXIT,  tag, ##__VA_ARGS__)
