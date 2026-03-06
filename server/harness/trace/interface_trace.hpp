/**
 * @file interface_trace.hpp
 * @brief Live interface-boundary tracing via SEGGER RTT
 *
 * When ENABLE_INTERFACE_TRACE is defined, traced wrappers call ITrace::log()
 * to emit compact, color-coded messages showing cross-layer calls.
 * When not defined, all macros expand to nothing (zero overhead).
 *
 * Output format (ANSI colors via RTT channel 0):
 *   [L3>L4] run(500,fwd)
 *   [L3<L4] getStatus->0x4003
 */

#pragma once

#include <stdint.h>

namespace Harness {

class ITraceSink;

namespace ITrace {

/**
 * @brief Set the trace record sink (call before init)
 *
 * The sink receives recordIface() calls to store entries in the ring buffer.
 * If no sink is set, RTT output still works but no ring buffer recording occurs.
 */
void setSink(ITraceSink* sink);

// Per-boundary enable bits (16-bit to support >8 boundaries)
enum Boundary : uint16_t {
    L1_L2_TRANSPORT  = (1 << 0),
    L2_L3_DISPATCH   = (1 << 1),
    L3_L4_MOTOR      = (1 << 2),
    L3_L4_ENCODER    = (1 << 3),
    L3_F_SAFETY      = (1 << 4),
    L3_CMD_SINK      = (1 << 5),
    L4_L5_SPI        = (1 << 6),
    L5_F_LOCK        = (1 << 7),
    L4_LCD           = (1 << 8),
    L4_FLASH         = (1 << 9),
    L2_TELEMETRY     = (1 << 10),
    ALL              = 0x07FF
};

#ifdef ENABLE_INTERFACE_TRACE

void init();
void setEnabled(uint16_t mask);
uint16_t getEnabled();

/**
 * @brief Emit a trace line if the given boundary is enabled
 * @param boundary  Bitmask value from Boundary enum
 * @param label     Layer label string, e.g. "[L3>L4]"
 * @param method    Method name, e.g. "run"
 * @param detail    Optional detail string, e.g. "500,fwd"
 */
void log(uint16_t boundary, const char* label, const char* method,
         const char* detail = nullptr);

/**
 * @brief Emit a trace line with a uint32_t result value
 */
void logResult(uint16_t boundary, const char* label, const char* method,
               uint32_t result);

#else

inline void init() {}
inline void setEnabled(uint16_t) {}
inline uint16_t getEnabled() { return 0; }
inline void log(uint16_t, const char*, const char*, const char* = nullptr) {}
inline void logResult(uint16_t, const char*, const char*, uint32_t) {}

#endif

} // namespace ITrace
} // namespace Harness

// Convenience macros — expand to nothing when tracing is disabled
#ifdef ENABLE_INTERFACE_TRACE
  #define ITRACE(boundary, label, method, ...) \
      Harness::ITrace::log(boundary, label, method, ##__VA_ARGS__)
  #define ITRACE_RESULT(boundary, label, method, val) \
      Harness::ITrace::logResult(boundary, label, method, val)
#else
  #define ITRACE(boundary, label, method, ...) ((void)0)
  #define ITRACE_RESULT(boundary, label, method, val) ((void)0)
#endif
