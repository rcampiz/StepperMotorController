/**
 * @file test_command_parser.cpp
 * @brief Step 5: L2 owns every host-facing byte. The parser is driven through
 *        Protocol::ICommandProcessor with a fake Transport::ITransport (lower side of H_L1_L2).
 */
#include "minitest.hpp"
#include "harness/defs/harness_l1_l2.hpp"
#include "L2_protocol/command_parser.hpp"
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
