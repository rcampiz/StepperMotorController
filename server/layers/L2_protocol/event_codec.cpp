/**
 * @file event_codec.cpp
 * @brief Wire format encoding for async events
 */

#include "L2_protocol/event_codec.hpp"
#include "harness/defs/harness_l1_l2.hpp"
#include "harness/trace/interface_trace.hpp"
#include "L2_protocol/command_parser_internal.hpp"
#include <stdio.h>
#include "harness/pins/motor_state.hpp"

// Shared helpers (eventTypeToString etc.) moved to
// F_platform/types/async_event_types.cpp

namespace Protocol::EventCodec {

void faultDetail(uint8_t faults, char* buf, size_t bufSize) {
    // The driver's decoded fault bits (no chip bit layout here)
    bool ocd  = (faults & Harness::DriverFault::OVERCURRENT) != 0u;
    bool uvlo = (faults & Harness::DriverFault::UNDERVOLTAGE) != 0u;
    bool thsd = (faults & Harness::DriverFault::THERMAL_SHUTDOWN) != 0u;

    int count = (ocd ? 1 : 0) + (uvlo ? 1 : 0) + (thsd ? 1 : 0);
    if (count > 1) {
        snprintf(buf, bufSize, "multiple");
    } else if (ocd) {
        snprintf(buf, bufSize, "OCD");
    } else if (uvlo) {
        snprintf(buf, bufSize, "UVLO");
    } else if (thsd) {
        snprintf(buf, bufSize, "TH_SD");
    } else {
        snprintf(buf, bufSize, "cleared");
    }
}

void stallDetail(uint8_t faults, char* buf, size_t bufSize) {
    // Chip-neutral: a stall, or not (which bridge saw it is the chip's own detail, in status)
    snprintf(buf, bufSize, "%s", (faults & Harness::DriverFault::STALL) != 0u ? "STALL" : "cleared");
}

void formatAscii(Transport::ITransport& transport, const AsyncEvent& evt, uint32_t seq) {
    (void)seq;  // ASCII mode doesn't include seq (deferred to v1.1)

    char line[128];
    char detail[16];
    const char* typeName = eventTypeToString(evt.type);
    ITRACE(Harness::ITrace::L2_TELEMETRY, "[L2>L1]", "event", typeName);

    switch (evt.type) {
        case EventType::FAULT:
        case EventType::FAULT_CLEAR:
            faultDetail(evt.faults, detail, sizeof(detail));
            snprintf(line, sizeof(line), "!%s status=0x%04X detail=%s",
                     typeName, evt.statusReg, detail);
            break;

        case EventType::STALL:
        case EventType::STALL_CLEAR:
            stallDetail(evt.faults, detail, sizeof(detail));
            snprintf(line, sizeof(line), "!%s status=0x%04X detail=%s",
                     typeName, evt.statusReg, detail);
            break;

        case EventType::MOTION_DONE:
        case EventType::FOLLOW_FAULT:
        case EventType::FOLLOW_RECOVERY:
        case EventType::FOLLOW_RECOVERED:
            snprintf(line, sizeof(line), "!%s status=0x%04X",
                     typeName, evt.statusReg);
            break;
        case EventType::ESTOP:
        case EventType::ESTOP_CLEAR:
            snprintf(line, sizeof(line), "!%s source=%s", typeName,
                     evt.statusReg == EVT_SOURCE_LCD ? "LCD" : "board");
            break;

        default:
            return;
    }

    transport.println(line);
}

void formatJson(Transport::ITransport& transport, const AsyncEvent& evt, uint32_t seq, uint32_t ts_ms) {
    char buf[256];
    char detail[16];
    char detailEsc[32];   // F5: escaped copy for the JSON literal
    const char* typeName = eventTypeToString(evt.type);
    ITRACE(Harness::ITrace::L2_TELEMETRY, "[L2>L1]", "event", typeName);

    switch (evt.type) {
        case EventType::FAULT:
        case EventType::FAULT_CLEAR:
            faultDetail(evt.faults, detail, sizeof(detail));
            snprintf(buf, sizeof(buf),
                "{\"kind\":\"event\",\"type\":\"%s\",\"seq\":%lu,\"ts_ms\":%lu,"
                "\"data\":{\"status\":%u,\"detail\":\"%s\"}}",
                typeName,
                static_cast<unsigned long>(seq),
                static_cast<unsigned long>(ts_ms),
                static_cast<unsigned>(evt.statusReg),
                (jsonEscape(detail, detailEsc, sizeof(detailEsc)), detailEsc));
            break;

        case EventType::STALL:
        case EventType::STALL_CLEAR:
            stallDetail(evt.faults, detail, sizeof(detail));
            snprintf(buf, sizeof(buf),
                "{\"kind\":\"event\",\"type\":\"%s\",\"seq\":%lu,\"ts_ms\":%lu,"
                "\"data\":{\"status\":%u,\"detail\":\"%s\"}}",
                typeName,
                static_cast<unsigned long>(seq),
                static_cast<unsigned long>(ts_ms),
                static_cast<unsigned>(evt.statusReg),
                (jsonEscape(detail, detailEsc, sizeof(detailEsc)), detailEsc));
            break;

        case EventType::MOTION_DONE:
        case EventType::FOLLOW_FAULT:
        case EventType::FOLLOW_RECOVERY:
        case EventType::FOLLOW_RECOVERED:
            snprintf(buf, sizeof(buf),
                "{\"kind\":\"event\",\"type\":\"%s\",\"seq\":%lu,\"ts_ms\":%lu,"
                "\"data\":{\"status\":%u}}",
                typeName,
                static_cast<unsigned long>(seq),
                static_cast<unsigned long>(ts_ms),
                static_cast<unsigned>(evt.statusReg));
            break;
        case EventType::ESTOP:
        case EventType::ESTOP_CLEAR:
            snprintf(buf, sizeof(buf),
                "{\"kind\":\"event\",\"type\":\"%s\",\"seq\":%lu,\"ts_ms\":%lu,"
                "\"data\":{\"source\":\"%s\"}}",
                typeName,
                static_cast<unsigned long>(seq),
                static_cast<unsigned long>(ts_ms),
                evt.statusReg == EVT_SOURCE_LCD ? "LCD" : "board");
            break;

        default:
            return;
    }

    transport.println(buf);
}

} // namespace Protocol::EventCodec
