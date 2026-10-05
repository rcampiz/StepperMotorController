/**
 * @file test_debug_service.cpp
 * @brief Step 6: the L3 DebugService gates bring-up commands behind DBG:UNLOCK, suspends tasks
 *        through Platform, and forwards register-level commands to Drivers::IDebugAccess.
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
               std::strcmp(cmd, "SPI_START") == 0 || std::strcmp(cmd, "LCD_DISABLE") == 0;
    }
    bool rawCommand(const char* cmd, const char* const*, uint8_t, Harness::IResponseSink& out) override {
        if (!knowsCommand(cmd)) return false;
        executed.push_back(cmd); out.writeLine("raw ok"); return true;
    }
    int formatSpi2Diag(char* buf, size_t n) override { return (n > 4) ? (std::strcpy(buf, "SPI2"), 4) : 0; }
};

struct FakeScheduler : Platform::IScheduler {
    std::vector<std::string> log;
    Platform::TaskHandle createTask(const Platform::TaskDescriptor&) override { return nullptr; }
    [[noreturn]] void start() override { for (;;) {} }
    void suspendTask(Platform::TaskHandle h) override { log.push_back(std::string("suspend:") + static_cast<const char*>(h)); }
    void resumeTask(Platform::TaskHandle h) override { log.push_back(std::string("resume:") + static_cast<const char*>(h)); }
};

struct FakeMotorCtrl : Services::IMotorControlService {
    int reinits = 0;
    void processCommand(const Services::MotorCommand&) override {}
    void periodicUpdate() override {}
    void applyConfig() override {}
    bool setStepModeSafe(uint8_t, uint8_t&) override { return true; }
    void reinit() override { ++reinits; }
    void getDebugInfo(Harness::MotorDebugParams& info) override { info = Harness::MotorDebugParams{}; info.status = 0x0003; info.absPos = -5; }
};

struct Rig {
    FakeAccess access; FakeScheduler sched; FakeMotorCtrl motor; Sink sink;
    Services::DebugService svc;
    const char* argv[1] = {nullptr};
    Rig() {
        static char motorName[] = "motor", displayName[] = "display";
        Harness::TaskHandles h; h.motor = motorName; h.display = displayName;
        Harness::setTaskHandles(h);
        svc.init(&access, sched, motor);
    }
};

} // namespace

TEST(locked_by_default_rejects_known_commands_without_executing) {
    Rig r;
    EXPECT_FALSE(r.svc.debugIsUnlocked());
    EXPECT_TRUE(r.svc.debugDispatch("SPI_DEBUG", r.argv, 0, r.sink));   // claimed
    EXPECT_TRUE(r.access.executed.empty());
    EXPECT_TRUE(r.sink.out.find("locked") != std::string::npos);
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

TEST(spi_stop_suspends_motor_task_before_touching_hardware) {
    Rig r; r.svc.debugUnlock();
    r.svc.debugDispatch("SPI_STOP", r.argv, 0, r.sink);
    EXPECT_EQ(r.sched.log.size(), 1u);
    EXPECT_STREQ(r.sched.log[0].c_str(), "suspend:motor");
    EXPECT_STREQ(r.access.executed.back().c_str(), "SPI_STOP");
    r.svc.debugDispatch("SPI_START", r.argv, 0, r.sink);
    EXPECT_STREQ(r.sched.log.back().c_str(), "resume:motor");
}

TEST(lcd_disable_suspends_display_and_reinits_the_controller) {
    Rig r; r.svc.debugUnlock();
    r.svc.debugDispatch("LCD_DISABLE", r.argv, 0, r.sink);
    EXPECT_STREQ(r.sched.log[0].c_str(), "suspend:display");
    EXPECT_EQ(r.motor.reinits, 1);
}

TEST(motor_debug_formats_controller_info) {
    Rig r; r.svc.debugUnlock();
    r.svc.debugDispatch("MOTOR_DEBUG", r.argv, 0, r.sink);
    EXPECT_TRUE(r.sink.out.find("STATUS=0003") != std::string::npos);
    EXPECT_TRUE(r.sink.out.find("POS=-5") != std::string::npos);
    EXPECT_TRUE(r.access.executed.empty());   // handled in L3, not forwarded
}
