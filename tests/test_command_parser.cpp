/**
 * @file test_command_parser.cpp
 * @brief Step 5: L2 owns every host-facing byte. The parser is driven through
 *        Protocol::ICommandProcessor with a fake Transport::ITransport (lower side of H_L1_L2).
 */
#include "minitest.hpp"
#include "harness/defs/harness_l1_l2.hpp"
#include "L2_protocol/command_parser.hpp"
#include "L2_protocol/command_parser_internal.hpp"
#include "L2_protocol/service_dispatcher.hpp"
#include "harness/pins/async_event.hpp"
#include <string>

namespace {
struct FakeTransport : Transport::ITransport {
    std::string out;
    std::string in;
    bool init() override { return true; }
    bool available() override { return !in.empty(); }
    size_t read(uint8_t* buf, size_t maxLen) override {
        size_t n = in.size() < maxLen ? in.size() : maxLen;
        for (size_t i = 0; i < n; ++i) buf[i] = static_cast<uint8_t>(in[i]);
        in.erase(0, n); return n;
    }
    bool readByte(uint8_t& b, uint32_t) override {
        if (in.empty()) return false;
        b = static_cast<uint8_t>(in[0]); in.erase(0, 1); return true;
    }
    size_t write(const uint8_t* d, size_t n) override { out.append(reinterpret_cast<const char*>(d), n); return n; }
    size_t print(const char* s) override { out += s; return std::string(s).size(); }
    size_t println(const char* s) override { out += s; out += "\r\n"; return std::string(s).size() + 2; }
    void flush() override {}
    uint32_t overflows = 0;
    uint32_t rxOverflowCount() const override { return overflows; }
};

struct Rig {
    FakeTransport transport;
    Protocol::ServiceDispatcher dispatcher;      // sub-dispatchers unset: emit paths never touch them
    Protocol::CommandParser parser{transport, dispatcher};
    Protocol::ICommandProcessor& proc = parser;
};
}

TEST(banner_is_emitted_by_l2_not_by_the_task) {
    Rig r;
    r.proc.emitBanner();
    EXPECT_TRUE(r.transport.out.find("Stepper Motor Controller") != std::string::npos);
    EXPECT_TRUE(r.transport.out.find("HELP") != std::string::npos);
}

// --- F17: machine-readable reset line and identification -----------------------
TEST(banner_starts_with_a_reset_line_carrying_boot_id_fw_and_proto) {
    Rig r;
    r.proc.emitBanner();
    EXPECT_TRUE(r.transport.out.rfind("!RESET boot=B007CAFE fw=", 0) == 0);
    EXPECT_TRUE(r.transport.out.find(" proto=") != std::string::npos);
    EXPECT_TRUE(r.transport.out.find(" build=host") != std::string::npos);
}

TEST(idn_reports_vendor_model_serial_fw_proto) {
    Rig r;
    r.transport.in = "*IDN?\n";
    r.proc.process();
    EXPECT_TRUE(r.transport.out.find("OK SMC,NUCLEO-F401RE-IHM03A1,HOST0001,") != std::string::npos);
}

TEST(telemetry_line_formats_signed_values) {
    Rig r;
    Protocol::TelemetrySnapshot snap{};
    snap.motor.position = -42; snap.motor.speed = 1200; snap.encoder.count = 99; snap.encoder.velocity = -7;
    r.proc.emitTelemetry(snap);
    EXPECT_STREQ(r.transport.out.c_str(), "TELEM: pos=-42 spd=1200 enc=99 vel=-7\r\n");
}

TEST(joystick_event_uses_wire_direction_names) {
    Rig r;
    r.proc.emitJoystick(UI::JoyDirection::LEFT, true);
    r.proc.emitJoystick(UI::JoyDirection::CENTER, false);
    EXPECT_STREQ(r.transport.out.c_str(), "EVENT JOY LEFT pressed\r\nEVENT JOY CENTER released\r\n");
}

TEST(async_event_is_emitted_in_ascii_by_default) {
    Rig r;
    AsyncEvent evt{EventType::FAULT, 0x1234};
    r.proc.emitEvent(evt, 7, 1000);
    EXPECT_TRUE(r.transport.out.find("FAULT") != std::string::npos);
    EXPECT_TRUE(r.transport.out.find('!') != std::string::npos);
}

// --- F4: DIAG:STAT? JSON never truncates ---------------------------------------
#include "L2_protocol/status_json.hpp"
#include <limits.h>
#include <stdint.h>

TEST(status_json_worst_case_fits_the_buffer_with_margin) {
    Protocol::TelemetrySnapshot snap{};
    snap.motor.position = INT32_MIN; snap.motor.speed = UINT32_MAX;
    snap.motor.statusReg = 0xFFFF; snap.motor.busy = true; snap.motor.hiZ = true;
    snap.encoder.count = INT64_MIN; snap.encoder.velocity = INT32_MIN;
    snap.encoder.indexSeen = true; snap.encoder.revolutions = INT32_MIN;
    snap.encoder.indexPeriodUs = UINT32_MAX; snap.encoder.velocityQuality = 255;
    snap.control.followingError = INT32_MIN; snap.control.setpoint = INT32_MIN;
    snap.control.mode = 255; snap.control.tracking = true;
    snap.control.pidOutput = INT16_MIN; snap.control.pTerm = INT16_MIN;
    snap.control.iTerm = INT16_MIN; snap.control.dTerm = INT16_MIN;
    snap.control.supervisorState = 255; snap.control.currentTier = 255;
    snap.control.velError = INT32_MIN; snap.control.retryCount = 255;
    snap.control.baseSpeedRaw = INT32_MIN; snap.control.trimSpeedRaw = INT32_MIN;
    snap.control.finalSpeedRaw = INT32_MIN; snap.control.trimFrozen = 255;
    snap.control.velQuality = 255;

    Protocol::StatusJsonView v;
    v.stateStr = "RUNNING_XXXXXXXXXXXXXXXX";          // 24 chars: longer than any real state
    v.tick = UINT32_MAX; v.queueDepth = UINT32_MAX; v.direction = 1; v.motStat = 3;
    v.statusReg = 0xFFFF;
    v.cmdErr = v.ocd = v.thermalSD = v.thermalWarn = v.uvlo = v.stallA = v.stallB = true;
    v.hbEnabled = true; v.hbTimeoutMs = UINT32_MAX; v.hbRemainingMs = UINT32_MAX; v.hbTimedOut = true;
    v.modeStr = "CLOSED_LOOP_MONITOR_XXXX"; v.encStatusStr = "NOT_READY_XXXXXXXXXXXXXX";
    v.anyError = true;
    v.uptimeTicks = UINT32_MAX; v.freeHeap = UINT32_MAX;
    v.rxOverflows = UINT32_MAX; v.rxOverflowLines = UINT32_MAX;

    char buf[Protocol::STATUS_JSON_BUF];
    int n = Protocol::renderStatusJson(buf, sizeof(buf), snap, v);
    EXPECT_TRUE(n > 600);
    EXPECT_TRUE(n < static_cast<int>(sizeof(buf)) - 64);        // >= 64 bytes of margin
    std::string js(buf);
    EXPECT_EQ(js.size(), static_cast<size_t>(n));
    EXPECT_TRUE(js.rfind("}}") == js.size() - 2);
    int depth = 0;
    for (char c : js) { if (c == '{') ++depth; if (c == '}') --depth; EXPECT_TRUE(depth >= 0); }
    EXPECT_EQ(depth, 0);
    EXPECT_TRUE(js.find("\"position\":-2147483648") != std::string::npos);
    EXPECT_TRUE(js.find("\"count\":-9223372036854775808") != std::string::npos);
}

TEST(status_json_reports_truncation_instead_of_sending_garbage) {
    Protocol::TelemetrySnapshot snap{};
    Protocol::StatusJsonView v;
    char small[200];
    int n = Protocol::renderStatusJson(small, sizeof(small), snap, v);
    EXPECT_TRUE(n >= static_cast<int>(sizeof(small)));   // caller must refuse to send
}

// --- F1.1: a line that spanned an RX overflow is refused, the next clean line is parsed ---
TEST(line_spanning_an_rx_overflow_is_discarded) {
    Rig r;
    r.transport.in = "FM";                 // first half of "FMT?" arrives ...
    r.proc.process();
    r.transport.overflows = 1;             // ... the ring overflowed (a byte was lost) ...
    r.transport.in = "T?\n";              // ... the rest arrives
    r.proc.process();
    EXPECT_TRUE(r.transport.out.find("ERROR RX_OVERFLOW") != std::string::npos);
    EXPECT_TRUE(r.transport.out.find("OK ASCII") == std::string::npos);
    EXPECT_EQ(r.parser.getDispatchStats().rxOverflowLines, 1u);

    r.transport.out.clear();
    r.transport.in = "FMT?\n";            // clean line after the overflow: parsed normally
    r.proc.process();
    EXPECT_TRUE(r.transport.out.find("OK ASCII") != std::string::npos);
    EXPECT_EQ(r.parser.getDispatchStats().rxOverflowLines, 1u);
}

TEST(overflow_between_lines_is_attributed_to_the_next_line) {
    Rig r;
    r.transport.in = "FMT?\n";
    r.proc.process();
    r.transport.out.clear();
    r.transport.overflows = 7;             // bytes lost while the host was mid-way through a line
    r.transport.in = "T 1\n";             // what survived of "SYST:HB:TIMEOUT 1" (say)
    r.proc.process();
    EXPECT_TRUE(r.transport.out.find("ERROR RX_OVERFLOW") != std::string::npos);
}

// --- F7: echo is a terminal convenience; off in JSON mode, switchable ----------------
TEST(echo_is_on_by_default_and_off_after_fmt_json) {
    Rig r;
    r.transport.in = "FMT?\n";
    r.proc.process();
    EXPECT_TRUE(r.transport.out.find("FMT?\r\n") != std::string::npos);     // echoed
    r.transport.out.clear();
    r.transport.in = "FMT JSON\n";
    r.proc.process();
    r.transport.out.clear();
    r.transport.in = "FMT?\n";
    r.proc.process();
    EXPECT_TRUE(r.transport.out.find("FMT?\r\n") == std::string::npos);     // no echo
    EXPECT_TRUE(r.transport.out.find("\"format\":\"JSON\"") != std::string::npos);
}

TEST(syst_echo_switches_echo_in_either_mode) {
    Rig r;
    r.transport.in = "SYST:ECHO OFF\n";
    r.proc.process();
    EXPECT_TRUE(r.transport.out.find("OK OFF") != std::string::npos);
    r.transport.out.clear();
    r.transport.in = "SYST:ECHO?\n";
    r.proc.process();
    EXPECT_TRUE(r.transport.out.find("SYST:ECHO?\r\n") == std::string::npos);
    EXPECT_TRUE(r.transport.out.find("OK OFF") != std::string::npos);
    r.transport.out.clear();
    r.transport.in = "SYST:ECHO ON\n";
    r.proc.process();
    EXPECT_TRUE(r.transport.out.find("OK ON") != std::string::npos);
    r.transport.out.clear();
    r.transport.in = "SYST:ECHO?\n";
    r.proc.process();
    EXPECT_TRUE(r.transport.out.find("SYST:ECHO?\r\n") != std::string::npos);
}

// --- F6: machine-readable error codes in JSON ---------------------------------------
TEST(json_errors_carry_codes_for_unknown_usage_and_overflow) {
    Rig r;
    r.transport.in = "FMT JSON\n";
    r.proc.process();
    r.transport.out.clear();
    r.transport.in = "NOPE:X\n";
    r.proc.process();
    EXPECT_TRUE(r.transport.out.find("\"code\":\"UNKNOWN_CMD\"") != std::string::npos);
    r.transport.out.clear();
    r.transport.in = "SYST:ECHO\n";                      // missing argument -> usage
    r.proc.process();
    EXPECT_TRUE(r.transport.out.find("\"code\":\"USAGE\"") != std::string::npos);
    r.transport.out.clear();
    r.transport.in = "FM";
    r.proc.process();
    r.transport.overflows = 3;
    r.transport.in = "T?\n";
    r.proc.process();
    EXPECT_TRUE(r.transport.out.find("\"code\":\"RX_OVERFLOW\"") != std::string::npos);
}

TEST(status_codes_are_enumerator_names) {
    EXPECT_STREQ(Harness::statusToCode(Harness::ServiceStatus::QueueFull), "QUEUE_FULL");
    EXPECT_STREQ(Harness::statusToCode(Harness::ServiceStatus::InvalidState), "INVALID_STATE");
    EXPECT_STREQ(Harness::statusToCode(Harness::ServiceStatus::Fault), "FAULT");
    EXPECT_STREQ(Harness::statusToCode(Harness::ServiceStatus::Ok), "OK");
}

// --- F5: JSON strings are escaped -----------------------------------------------------
TEST(json_escape_handles_quotes_backslashes_and_control_chars) {
    char out[64];
    Protocol::jsonEscape("say \"hi\" \\ tab\tnl\n", out, sizeof(out));
    EXPECT_STREQ(out, "say \\\"hi\\\" \\\\ tab\\tnl\\n");
    Protocol::jsonEscape("\x01", out, sizeof(out));
    EXPECT_STREQ(out, "\\u0001");
    Protocol::jsonEscape("abcdef", out, 4);            // truncates, always terminated
    EXPECT_STREQ(out, "abc");
    EXPECT_EQ(Protocol::jsonEscape(nullptr, out, sizeof(out)), 0u);
    EXPECT_STREQ(out, "");
}

TEST(json_error_message_with_quotes_is_still_valid_json) {
    Rig r;
    r.transport.in = "FMT JSON\n";
    r.proc.process();
    r.transport.out.clear();
    r.transport.in = "UI:MODE \"x\"\n";                     // argument with quotes -> usage/unknown mode
    r.proc.process();
    std::string js = r.transport.out;
    // naive validity check: an odd number of unescaped quotes would break parsing
    int quotes = 0;
    for (size_t i = 0; i < js.size(); ++i) if (js[i] == '"' && (i == 0 || js[i-1] != '\\')) ++quotes;
    EXPECT_EQ(quotes % 2, 0);
    EXPECT_TRUE(js.find("\"status\":\"error\"") != std::string::npos);
}

// --- parser over a fake L3 bundle: commands that reach the dispatchers ----------------
#include "fakes/fake_dispatchers.hpp"

namespace {
struct BoundRig {
    FakeTransport transport;
    Protocol::ServiceDispatcher dispatcher;
    FakeL3::Bundle l3;
    Protocol::CommandParser parser{transport, dispatcher};
    Protocol::ICommandProcessor& proc = parser;
    BoundRig() { dispatcher.bind(l3.dispatchers); }
    std::string run(const char* line) {
        transport.out.clear();
        transport.in = std::string(line) + "\n";
        proc.process();
        return transport.out;
    }
};
}

TEST(dev_role_query_has_its_own_handler_and_reply) {           // F15
    BoundRig r;
    r.run("FMT JSON");
    std::string out = r.run("DEV:ROLE?");
    EXPECT_TRUE(out.find("\"command\":\"GET_ROLE\"") != std::string::npos);
    EXPECT_TRUE(out.find("\"role_id\":0") != std::string::npos);
    EXPECT_TRUE(out.find("\"role\":\"") != std::string::npos);
    EXPECT_TRUE(r.l3.system.calls.back() == "getDeviceInfo");
}

TEST(ping_round_trips_the_sequence_number) {
    BoundRig r;
    r.run("FMT JSON");
    std::string out = r.run("DIAG:PING 5");
    EXPECT_TRUE(out.find("\"command\":\"PING\"") != std::string::npos);
    EXPECT_TRUE(out.find("\"seq\":5") != std::string::npos);
}

// --- F9: host-settable telemetry push with the DIAG:STAT? schema ---------------------
TEST(telemetry_rate_command_reaches_l3_and_replies_in_both_formats) {
    BoundRig r;
    std::string out = r.run("SYST:TLM:RATE 50");
    EXPECT_TRUE(r.l3.system.calls.back() == "setTelemetryPeriod");
    EXPECT_TRUE(out.find("OK TLM_RATE 0") != std::string::npos);    // the fake accepts nothing
    out = r.run("SYST:TLM:RATE?");
    EXPECT_TRUE(r.l3.system.calls.back() == "getTelemetryPeriod");
    EXPECT_TRUE(out.find("OK 0\r\n") != std::string::npos);
    out = r.run("SYST:TLM:RATE");
    EXPECT_TRUE(out.find("ERROR USAGE Usage: SYST:TLM:RATE <ms>") != std::string::npos);
    r.run("FMT JSON");
    out = r.run("SYST:TLM:RATE 50");
    EXPECT_TRUE(out.find("\"command\":\"TLM_RATE\"") != std::string::npos);
    EXPECT_TRUE(out.find("\"requested\":50") != std::string::npos);
    EXPECT_TRUE(out.find("\"accepted\":0") != std::string::npos);
    EXPECT_TRUE(out.find("\"enabled\":false") != std::string::npos);
    out = r.run("SYST:TLM:RATE?");
    EXPECT_TRUE(out.find("\"period_ms\":0") != std::string::npos);
}

TEST(json_telemetry_push_carries_exactly_the_diag_stat_data_object) {
    BoundRig r;
    r.run("FMT JSON");
    std::string stat = r.run("DIAG:STAT?");
    Protocol::TelemetrySnapshot snap{};                 // same (zero) snapshot the fake hands out
    r.transport.out.clear();
    r.proc.emitTelemetry(snap);
    std::string tlm = r.transport.out;
    const std::string sh = Protocol::STATUS_JSON_HEAD;
    const std::string th = Protocol::TELEMETRY_JSON_HEAD;
    EXPECT_TRUE(stat.compare(0, sh.size(), sh) == 0);
    EXPECT_TRUE(tlm.compare(0, th.size(), th) == 0);
    EXPECT_EQ(stat.substr(sh.size()), tlm.substr(th.size()));
    EXPECT_TRUE(tlm.find("\"kind\":\"telemetry\"") == 1);
    EXPECT_TRUE(tlm.find("\"rx_overflow_lines\":") != std::string::npos);
}

TEST(ascii_mode_keeps_the_short_telem_line) {
    BoundRig r;
    Protocol::TelemetrySnapshot snap{};
    snap.motor.position = 7;
    r.proc.emitTelemetry(snap);
    EXPECT_STREQ(r.transport.out.c_str(), "TELEM: pos=7 spd=0 enc=0 vel=0\r\n");
}
