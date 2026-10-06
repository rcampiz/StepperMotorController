/**
 * @file test_debug_service.cpp
 * @brief The L3 DebugService gates bring-up commands behind DBG:UNLOCK, pauses the Motor and
 *        Display tasks by flag (never by suspending them), and forwards register-level
 *        commands to Drivers::IDebugAccess.
 */
#include "minitest.hpp"
#include "L3_services/debug/debug_service.hpp"
#include <string>
#include <vector>
#include <cstring>

namespace {

struct Sink : Harness::IResponseSink {
    std::string out;
    void write(const char* s) override { out += s; }
    void writeLine(const char* s) override { out += s; out += "\n"; }
};

struct FakeAccess : Drivers::IDebugAccess {
    std::vector<std::string> executed;
    bool knowsCommand(const char* cmd) const override {
        return std::strcmp(cmd, "SPI_DEBUG") == 0 || std::strcmp(cmd, "SPI_STOP") == 0 ||
               std::strcmp(cmd, "SPI_START") == 0 || std::strcmp(cmd, "LCD_DISABLE") == 0 ||
               std::strcmp(cmd, "ENC_DEBUG") == 0;
    }
    bool rawCommand(const char* cmd, const char* const*, uint8_t, Harness::IResponseSink& out) override {
        if (!knowsCommand(cmd)) return false;
        executed.push_back(cmd); out.writeLine("raw ok"); return true;
    }
    int formatSpi2Diag(char* buf, size_t n) override { return (n > 4) ? (std::strcpy(buf, "SPI2"), 4) : 0; }
};

struct FakeMotorCtrl : Services::IMotorTaskControl {
    int reinits = 0; bool paused = false;
    std::vector<std::string> log;
    void reinit() override { ++reinits; log.push_back("reinit"); }
    bool applyConfig() override { return true; }
    bool getDebugInfo(Harness::MotorDebugParams& info) override { info = Harness::MotorDebugParams{}; info.status = 0x0003; info.absPos = -5; return true; }
    bool setStepModeSafe(uint8_t m, uint8_t& rb) override { rb = m; return true; }
    void setDebugPaused(bool p) override { paused = p; log.push_back(p ? "pause" : "resume"); }
    bool isDebugPaused() const override { return paused; }
};

struct FakeDisplay : Services::IDisplayControl {
    bool enabled = true;
    void setDisplayEnabled(bool e) override { enabled = e; }
    bool isDisplayEnabled() const override { return enabled; }
};

struct Rig {
    FakeAccess access; FakeMotorCtrl motor; FakeDisplay display; Sink sink;
    Services::DebugService svc;
    const char* argv[1] = {nullptr};
    Rig() { svc.init(&access, &motor, &display); }
};

} // namespace

TEST(locked_by_default_rejects_known_commands_without_executing) {
    Rig r;
    EXPECT_FALSE(r.svc.debugIsUnlocked());
    EXPECT_TRUE(r.svc.debugDispatch("SPI_DEBUG", r.argv, 0, r.sink));   // claimed
    EXPECT_TRUE(r.access.executed.empty());
    EXPECT_TRUE(r.sink.out.find("locked") != std::string::npos);
}

TEST(read_only_dumps_bypass_the_lock) {
    Rig r;
    EXPECT_FALSE(r.svc.debugIsUnlocked());
    EXPECT_TRUE(r.svc.debugDispatch("MOTOR_DEBUG", r.argv, 0, r.sink));   // handled in L3
    EXPECT_TRUE(r.sink.out.find("STATUS=0003") != std::string::npos);
    EXPECT_TRUE(r.sink.out.find("locked") == std::string::npos);
    r.access.executed.clear(); r.sink.out.clear();
    EXPECT_TRUE(r.svc.debugDispatch("ENC_DEBUG", r.argv, 0, r.sink));     // forwarded to L4
    EXPECT_EQ(r.access.executed.size(), 1u);
    EXPECT_STREQ(r.access.executed[0].c_str(), "ENC_DEBUG");
    r.sink.out.clear();
    EXPECT_TRUE(r.svc.debugDispatch("SPI_STOP", r.argv, 0, r.sink));      // still gated
    EXPECT_TRUE(r.sink.out.find("locked") != std::string::npos);
    EXPECT_FALSE(r.motor.paused);
}

TEST(unknown_commands_are_not_claimed_even_when_locked) {
    Rig r;
    EXPECT_FALSE(r.svc.debugDispatch("MOT:RUN", r.argv, 0, r.sink));
    EXPECT_TRUE(r.sink.out.empty());
}

TEST(unlock_forwards_raw_commands_to_l4) {
    Rig r;
    EXPECT_EQ(r.svc.debugUnlock(), Harness::ServiceStatus::Ok);
    EXPECT_TRUE(r.svc.debugDispatch("SPI_DEBUG", r.argv, 0, r.sink));
    EXPECT_EQ(r.access.executed.size(), 1u);
    EXPECT_STREQ(r.access.executed[0].c_str(), "SPI_DEBUG");
    r.svc.debugLock();
    EXPECT_FALSE(r.svc.debugIsUnlocked());
}

TEST(spi_stop_pauses_the_motor_task_by_flag_before_touching_hardware) {
    Rig r; r.svc.debugUnlock();
    r.svc.debugDispatch("SPI_STOP", r.argv, 0, r.sink);
    EXPECT_TRUE(r.motor.paused);
    EXPECT_STREQ(r.motor.log[0].c_str(), "pause");
    EXPECT_STREQ(r.access.executed.back().c_str(), "SPI_STOP");
    r.svc.debugDispatch("SPI_START", r.argv, 0, r.sink);
    EXPECT_FALSE(r.motor.paused);
    EXPECT_STREQ(r.motor.log.back().c_str(), "resume");
}

TEST(lcd_disable_disables_the_display_by_flag_and_reinits_the_motor) {
    Rig r; r.svc.debugUnlock();
    r.svc.debugDispatch("LCD_DISABLE", r.argv, 0, r.sink);
    EXPECT_FALSE(r.display.enabled);
    EXPECT_EQ(r.motor.reinits, 1);
    EXPECT_STREQ(r.access.executed.back().c_str(), "LCD_DISABLE");
}

TEST(motor_debug_formats_controller_info) {
    Rig r; r.svc.debugUnlock();
    r.svc.debugDispatch("MOTOR_DEBUG", r.argv, 0, r.sink);
    EXPECT_TRUE(r.sink.out.find("STATUS=0003") != std::string::npos);
    EXPECT_TRUE(r.sink.out.find("POS=-5") != std::string::npos);
    EXPECT_TRUE(r.access.executed.empty());   // handled in L3, not forwarded
}
