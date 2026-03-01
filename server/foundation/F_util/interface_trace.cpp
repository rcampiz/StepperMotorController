/**
 * @file interface_trace.cpp
 * @brief Interface trace implementation — RTT channel 0 with ANSI colors
 *
 * Only compiled when ENABLE_INTERFACE_TRACE is defined.
 */

#ifdef ENABLE_INTERFACE_TRACE

#include "F_util/interface_trace.hpp"
#include "F_platform/interfaces/itrace_sink.hpp"
#include "X_middlewares/SEGGER/RTT/SEGGER_RTT.h"
#include <string.h>

namespace ITrace {

static uint8_t s_enableMask = ALL;
static ITraceSink* s_sink = nullptr;

void setSink(ITraceSink* sink) {
    s_sink = sink;
}

// ANSI color escape sequences (short, to minimize RTT bandwidth)
// Colors chosen per layer:
//   L1 = cyan, L2 = green, L3 = yellow, L4 = magenta, L5 = blue
//   Intra-L3 = white, Errors = red
static constexpr const char* RESET = "\033[0m";

void init()
{
    s_enableMask = ALL;
}

void setEnabled(uint8_t mask)
{
    s_enableMask = mask;
}

uint8_t getEnabled()
{
    return s_enableMask;
}

// Pick ANSI color based on boundary type
static const char* colorForBoundary(uint8_t boundary) {
    switch (boundary) {
        case L1_L2_TRANSPORT: return "\033[36m";  // cyan
        case L2_L3_DISPATCH:  return "\033[32m";  // green
        case L3_L4_MOTOR:     return "\033[33m";  // yellow
        case L3_L4_ENCODER:   return "\033[35m";  // magenta
        case L3_F_SAFETY:     return "\033[31m";  // red
        case L3_CMD_SINK:     return "\033[37m";  // white
        default:              return "\033[0m";    // reset
    }
}

// Format a uint32_t as hex into buf (must be >= 11 bytes: "0x" + 8 + NUL)
static void hexFormat(uint32_t val, char* buf) {
    static const char hex[] = "0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x';
    // Skip leading zeros: find first non-zero nibble
    int start = 7;
    for (int i = 7; i > 0; i--) {
        if ((val >> (i * 4)) & 0xF) { start = i; break; }
    }
    int pos = 2;
    for (int i = start; i >= 0; i--) {
        buf[pos++] = hex[(val >> (i * 4)) & 0xF];
    }
    buf[pos] = '\0';
}

// RTT-only output (no ring buffer write)
static void rttEmit(uint8_t boundary, const char* label, const char* method,
                    const char* detail)
{
    char buf[128];
    int pos = 0;

    // Color
    const char* color = colorForBoundary(boundary);
    size_t clen = strlen(color);
    if (pos + clen < sizeof(buf)) { memcpy(buf + pos, color, clen); pos += clen; }

    // Label
    size_t llen = strlen(label);
    if (pos + llen < sizeof(buf)) { memcpy(buf + pos, label, llen); pos += llen; }

    // Reset color
    size_t rlen = strlen(RESET);
    if (pos + rlen < sizeof(buf)) { memcpy(buf + pos, RESET, rlen); pos += rlen; }

    // Space + method
    if (pos < (int)sizeof(buf)) buf[pos++] = ' ';
    size_t mlen = strlen(method);
    if (pos + mlen < sizeof(buf)) { memcpy(buf + pos, method, mlen); pos += mlen; }

    // Optional detail in parens
    if (detail != nullptr && detail[0] != '\0') {
        if (pos < (int)sizeof(buf)) buf[pos++] = '(';
        size_t dlen = strlen(detail);
        size_t maxd = sizeof(buf) - pos - 3; // room for ")\n\0"
        if (dlen > maxd) dlen = maxd;
        memcpy(buf + pos, detail, dlen); pos += dlen;
        if (pos < (int)sizeof(buf)) buf[pos++] = ')';
    }

    // Newline
    if (pos < (int)sizeof(buf)) buf[pos++] = '\n';

    SEGGER_RTT_Write(0, buf, pos);
}

void log(uint8_t boundary, const char* label, const char* method,
         const char* detail)
{
    if (!(s_enableMask & boundary)) return;

    rttEmit(boundary, label, method, detail);
    if (s_sink) s_sink->recordIface(boundary, label, method, 0, detail);
}

void logResult(uint8_t boundary, const char* label, const char* method,
               uint32_t result)
{
    if (!(s_enableMask & boundary)) return;

    // RTT: format "method->0xNNNN"
    char hexBuf[11];
    hexFormat(result, hexBuf);
    char combined[64];
    size_t mlen = strlen(method);
    size_t hlen = strlen(hexBuf);
    if (mlen + 2 + hlen < sizeof(combined)) {
        memcpy(combined, method, mlen);
        combined[mlen] = '-'; combined[mlen+1] = '>';
        memcpy(combined + mlen + 2, hexBuf, hlen + 1);
        rttEmit(boundary, label, combined, nullptr);
    }

    // Ring buffer: store method literal + numeric result
    if (s_sink) s_sink->recordIface(boundary, label, method, result);
}

} // namespace ITrace

#endif // ENABLE_INTERFACE_TRACE
