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

/// The compact telemetry push (SYST:TLM:FMT COMPACT): the live fields only, same key names
/// and nesting as the full object so a client merges it into its last full status. Worst
/// case about 300 bytes (host-tested); a typical line is about 170.
constexpr size_t COMPACT_JSON_BUF = 384;

/// Smallest telemetry period the link can carry: one worst-case status line (STATUS_JSON_BUF
/// bytes, 10 bits each) may take at most 80 % of the period. The Comms task transmits by
/// busy-waiting and outranks the Encoder task, so a period below this starves the encoder
/// snapshot (bench 2026-10-17: at 50 ms / 115200 baud the count froze for a whole move).
/// 139 ms at 115200, 18 ms at 921600; 0 (no floor) when the link has no baud rate. The
/// compact line (COMPACT_JSON_BUF) floors at 42 ms at 115200.
constexpr uint32_t telemetryPeriodFloorMs(uint32_t baud, size_t lineBytes = STATUS_JSON_BUF) {
    return baud == 0 ? 0
         : (static_cast<uint32_t>(lineBytes) * 10U * 1000U * 5U + baud * 4U - 1U) / (baud * 4U);
}

/// Protocol 2 wire unit of the speed-trim fields base_spd / trim_spd / final_spd. They were
/// the powerSTEP01 SPEED register values when the firmware worked in chip units: 2^-28 step
/// per 250 ns tick, i.e. floor(fsps x 2^20 / 15625) (67.108864 per full step/s). L3 now
/// works in full steps/s; this is the one place that converts back, for wire compatibility
/// only (driver boundary step 4 sends steps/s and removes it). Exact for whole steps/s
/// (fsps x 2^20 is exact in float); truncates toward zero; saturates to the int32 range.
inline int32_t protocol2SpeedUnits(float fsps) {
    if (fsps < 0.0f) {
        const int32_t m = protocol2SpeedUnits(-fsps);
        return (m == INT32_MAX) ? INT32_MIN : -m;
    }
    if (!(fsps > 0.0f)) return 0;                       // 0 or NaN
    if (fsps >= 32000000.0f) return INT32_MAX;          // 2^31 x 15625 / 2^20
    return static_cast<int32_t>(static_cast<uint64_t>(fsps * 1048576.0f) / 15625u);
}

/// base_spd, trim_spd, final_spd in protocol 2 units; trim is final - base, so they add up
struct Protocol2TrimSpeeds { long base, trim, final_; };
inline Protocol2TrimSpeeds protocol2TrimSpeeds(const ControlTelemetry& c) {
    const int64_t base = protocol2SpeedUnits(c.baseSpeedFsps);
    const int64_t fin = protocol2SpeedUnits(c.finalSpeedFsps);
    int64_t trim = fin - base;
    if (trim > INT32_MAX) trim = INT32_MAX;
    if (trim < INT32_MIN) trim = INT32_MIN;
    return { static_cast<long>(base), static_cast<long>(trim), static_cast<long>(fin) };
}

struct StatusJsonView {
    const char* stateStr = "";
    uint32_t    tick = 0;
    uint32_t    queueDepth = 0;
    int         direction = 0;
    unsigned    motStat = 0;
    uint32_t    statusReg = 0;   ///< the driver chip's status word (opaque)
    bool cmdErr = false, ocd = false, thermalSD = false, thermalWarn = false;
    bool uvlo = false, stallA = false, stallB = false;
    bool     hbEnabled = false;
    uint32_t hbTimeoutMs = 0;
    uint32_t hbRemainingMs = 0;
    bool     hbTimedOut = false;
    const char* modeStr = "";
    const char* encStatusStr = "";
    bool anyError = false;
    uint32_t uptimeTicks = 0;
    uint32_t freeHeap = 0;
    uint32_t rxOverflows = 0;        ///< bytes dropped by the RX ring (F1)
    uint32_t rxOverflowLines = 0;    ///< lines refused because of an overflow (F1)
};

/// Envelope heads: the DIAG:STAT? reply and the F9 telemetry push share one `data` object.
constexpr const char* STATUS_JSON_HEAD    = "{\"status\":\"ok\",\"command\":\"GET_STATUS\",\"data\":";
constexpr const char* TELEMETRY_JSON_HEAD = "{\"kind\":\"telemetry\",\"data\":";

/// Format <head>{...}} into buf. Returns the would-be length (never writes past
/// bufSize-1). Caller must check `< bufSize`.
int renderStatusJson(char* buf, size_t bufSize,
                     const TelemetrySnapshot& snap, const StatusJsonView& v,
                     const char* head = STATUS_JSON_HEAD);

/// The compact push: {"kind":"telemetry","data":{state, tick, motor{position, speed, busy,
/// hi_z}, encoder{count, velocity}, control{following_error}, heartbeat{remaining_ms}}}.
/// No "system" key: that is how a client tells it from the full object. Same return rule.
int renderCompactJson(char* buf, size_t bufSize,
                      const TelemetrySnapshot& snap, const StatusJsonView& v);

} // namespace Protocol
