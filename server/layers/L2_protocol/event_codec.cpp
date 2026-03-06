/**
 * @file event_codec.cpp
 * @brief Wire format encoding for async events
 */

#include "L2_protocol/event_codec.hpp"
#include "harness/pins/itransport.hpp"
#include "harness/trace/interface_trace.hpp"
#include <stdio.h>

// Shared helpers (eventTypeToString etc.) moved to
// F_platform/types/async_event_types.cpp

namespace Protocol::EventCodec {

void faultDetail(uint16_t statusReg, char* buf, size_t bufSize) {
    // Active-low fault bits: OCD (bit13), UVLO (bit9), TH_SD (bits 11-12 == 2 or 3)
    bool ocd  = !(statusReg & (1 << 13));
    bool uvlo = !(statusReg & (1 << 9));
    uint8_t th = (statusReg >> 11) & 0x03;
    bool thsd = (th >= 2);

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

void stallDetail(uint16_t statusReg, char* buf, size_t bufSize) {
    // Active-low stall bits: STALL_A (bit14), STALL_B (bit15)
    bool stallA = !(statusReg & (1 << 14));
    bool stallB = !(statusReg & (1 << 15));

    if (stallA && stallB) {
        snprintf(buf, bufSize, "AB");
    } else if (stallA) {
        snprintf(buf, bufSize, "A");
    } else if (stallB) {
        snprintf(buf, bufSize, "B");
    } else {
        snprintf(buf, bufSize, "cleared");
    }
}

void formatAscii(Harness::ITransport& transport, const AsyncEvent& evt, uint32_t seq) {
    (void)seq;  // ASCII mode doesn't include seq (deferred to v1.1)

    char line[128];
    char detail[16];
    const char* typeName = eventTypeToString(evt.type);
    ITRACE(Harness::ITrace::L2_TELEMETRY, "[L2>L1]", "event", typeName);

    switch (evt.type) {
        case EventType::FAULT:
        case EventType::FAULT_CLEAR:
            faultDetail(evt.statusReg, detail, sizeof(detail));
            snprintf(line, sizeof(line), "!%s status=0x%04X detail=%s",
                     typeName, evt.statusReg, detail);
            break;

        case EventType::STALL:
        case EventType::STALL_CLEAR:
            stallDetail(evt.statusReg, detail, sizeof(detail));
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

        default:
            return;
    }

    transport.println(line);
}

void formatJson(Harness::ITransport& transport, const AsyncEvent& evt, uint32_t seq, uint32_t ts_ms) {
    char buf[256];
    char detail[16];
    const char* typeName = eventTypeToString(evt.type);
    ITRACE(Harness::ITrace::L2_TELEMETRY, "[L2>L1]", "event", typeName);

    switch (evt.type) {
        case EventType::FAULT:
        case EventType::FAULT_CLEAR:
            faultDetail(evt.statusReg, detail, sizeof(detail));
            snprintf(buf, sizeof(buf),
                "{\"kind\":\"event\",\"type\":\"%s\",\"seq\":%lu,\"ts_ms\":%lu,"
                "\"data\":{\"status\":%u,\"detail\":\"%s\"}}",
                typeName,
                static_cast<unsigned long>(seq),
                static_cast<unsigned long>(ts_ms),
                static_cast<unsigned>(evt.statusReg),
                detail);
            break;

        case EventType::STALL:
        case EventType::STALL_CLEAR:
            stallDetail(evt.statusReg, detail, sizeof(detail));
            snprintf(buf, sizeof(buf),
                "{\"kind\":\"event\",\"type\":\"%s\",\"seq\":%lu,\"ts_ms\":%lu,"
                "\"data\":{\"status\":%u,\"detail\":\"%s\"}}",
                typeName,
                static_cast<unsigned long>(seq),
                static_cast<unsigned long>(ts_ms),
                static_cast<unsigned>(evt.statusReg),
                detail);
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

        default:
            return;
    }

    transport.println(buf);
}

} // namespace Protocol::EventCodec
