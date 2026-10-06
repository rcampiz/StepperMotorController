/**
 * @file status_json.hpp
 * @brief DIAG:STAT? JSON render as a pure function, so its worst-case length is testable (F4)
 *
 * The handler fills a StatusJsonView from the dispatcher, then renderStatusJson() formats
 * the full envelope. snprintf semantics: the return value is the length the output would
 * have; a value >= bufSize means it was truncated and must not be sent.
 */
#pragma once
#include "harness/pins/telemetry_types.hpp"
#include <stddef.h>
#include <stdint.h>

namespace Protocol {

/// Buffer the handler uses; the host test proves the worst case fits with margin.
constexpr size_t STATUS_JSON_BUF = 1280;

struct StatusJsonView {
    const char* stateStr = "";
    uint32_t    tick = 0;
    uint32_t    queueDepth = 0;
    int         direction = 0;
    unsigned    motStat = 0;
    uint16_t    statusReg = 0;
    bool cmdErr = false, ocd = false, thermalSD = false, thermalWarn = false;
    bool uvlo = false, stallA = false, stallB = false;
    bool     hbEnabled = false;
    uint32_t hbTimeoutMs = 0;
    uint32_t hbRemainingMs = 0;
    bool     hbTimedOut = false;
    const char* modeStr = "";
    const char* encStatusStr = "";
    bool anyError = false;
};

/// Format {"status":"ok","command":"GET_STATUS","data":{...}} into buf. Returns the
/// would-be length (never writes past bufSize-1). Caller must check `< bufSize`.
int renderStatusJson(char* buf, size_t bufSize,
                     const TelemetrySnapshot& snap, const StatusJsonView& v);

} // namespace Protocol
