/**
 * @file test_screen_dispatcher.cpp
 * @brief The Dispatcher screen rendered through the real L4 LCD driver into a simulated panel,
 *        fed by the real CommsService, CommandQueue and MotorEventManager (fake ports under
 *        them) and a fake trace ring.
 *
 * Asserts the chrome contract (the screen paints only inside the content band), the palette
 * (theme colours, no stock ones) and the diff repaint (an unchanged frame sends almost
 * nothing, a scroll repaints only the list). Dumps screens/dispatcher*.ppm for a look.
 */
#include "minitest.hpp"
#include "fakes/fake_board.hpp"
#include "fakes/panel_sim.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include "L3_services/ui/screens/dispatcher_screen.hpp"
#include "L3_services/ui/screen_manager/screen_manager.hpp"
#include "L3_services/ui/theme.hpp"
#include "L3_services/comms/comms_service/comms_service.hpp"
#include "L3_services/dispatch/command_queue/command_queue.hpp"
#include "L3_services/infra/tick_timer/tick_timer.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include "L3_services/motion/motion.hpp"
#include "L3_services/infra/trace/trace.hpp"
#include "F_platform/boundary/iclock.hpp"
#include "F_platform/boundary/ilock.hpp"
#include "F_platform/boundary/iqueue.hpp"
#include "F_platform/boundary/imicrosecond_clock.hpp"
#include "harness/pins/async_event.hpp"
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

// --- fake trace ring: the service bars count serviceIds in it ---------------------------
namespace {
uint8_t g_traceSvc[128];
size_t g_traceCount = 0;
}
namespace Trace {
size_t getTotal() { return g_traceCount; }
size_t getCount() { return g_traceCount; }
bool getEntry(size_t i, Entry& e) {
    if (i >= g_traceCount) return false;
    e = Entry{};
    e.serviceId = g_traceSvc[i];
    e.tag = "T";
    return true;
}
}

using namespace fakes;

namespace {

struct NullLock : Platform::ILock {
    void acquire() override {}
    void release() override {}
};
struct TickClock : Platform::NullClock {
    uint32_t now = 1000;
    uint32_t getTickMs() const override { return now; }
};

/// The L2 parser as the CommsService sees it: only the dispatch stats matter here
struct FakeProcessor : Protocol::ICommandProcessor {
    Harness::DispatchStats stats = {};
    void process() override {}
    void checkBaudRevert() override {}
    void emitBanner() override {}
    void emitEvent(const AsyncEvent&, uint32_t, uint32_t) override {}
    void emitTelemetry(const Protocol::TelemetrySnapshot&) override {}
    void emitJoystick(UI::JoyDirection, bool) override {}
    void getDispatchStats(Harness::DispatchStats& out) const override { out = stats; }
};
struct NoEvents : Services::IMotorEventReceiver {
    bool receive(AsyncEvent&) override { return false; }
};
struct NoEstop : Services::IEmergencyStop {
    void emergencyStop() override {}
};
struct HoldSink : Services::IMotorCommandSink {
    bool sendCommand(const Services::MotorCommand&, uint32_t) override { return true; }
};

NullLock g_lock;
TickClock g_clock;
Platform::NullMicrosecondClock g_us;
FakeProcessor g_proc;
NoEvents g_noEvents;
NoEstop g_noEstop;
HoldSink g_sink;
Platform::NullQueue<AsyncEvent, 8> g_evtQueue;

void setupServices(uint8_t cmdQueued, uint8_t eventsQueued, size_t traceEntries)
{
    static bool once = false;
    if (!once) {
        once = true;
        Services::g_telemetryStore.init(g_lock, g_clock);
        Services::TickTimer_Init(g_us);
        Services::g_commsService.init(g_proc, g_clock, g_noEvents, g_noEstop,
                                      Services::g_telemetryStore, nullptr, nullptr);
    }
    // Dispatch stats with a full recent ring
    g_proc.stats = Harness::DispatchStats{};
    g_proc.stats.totalCommands = 1287;
    g_proc.stats.unknownCommands = 3;
    static const char* const cmds[8] = {"MOT:RUN", "MOT:STAT?", "SYST:HB", "ENC:POS?",
                                        "CFG:SAVE", "MOT:MOVE", "SYNC:ARM", "SYNC:START"};
    g_proc.stats.recentCount = 8;
    for (int i = 0; i < 8; i++) snprintf(g_proc.stats.recentCmds[i], 24, "%s", cmds[i]);

    // Command queue: `cmdQueued` moves pending
    Services::g_commandQueue.init(g_lock, g_clock, g_sink);
    for (uint8_t i = 0; i < cmdQueued; i++) {
        Services::g_commandQueue.queueCommand(Services::MotorCommand{Services::MotorCmdType::Move, 100 + i, 0});
    }

    // Event queue: `eventsQueued` MOTION_DONE events waiting for the Comms task
    Services::motion.stepper.event.init(g_evtQueue);
    Services::motion.stepper.event.enable(EVT_MASK_ALL, 0, 0xFFFF);   // no active faults
    for (uint8_t i = 0; i < eventsQueued; i++) {
        Services::motion.stepper.event.post(EventType::FAULT, 0xFFFF);
    }

    // Trace ring: a mix weighted toward Comms and Motion, nothing from SysId
    static const uint8_t mix[16] = {6, 6, 1, 6, 8, 1, 4, 6, 7, 1, 6, 3, 8, 2, 6, 1};
    g_traceCount = traceEntries;
    for (size_t i = 0; i < traceEntries && i < 128; i++) g_traceSvc[i] = mix[i % 16];
}

/// A panel plus the real driver, the way the Display task sees it
struct Rig {
    FakeGPIO cs, dc, nreset, te;
    PanelSim panel;
    Drivers::LCD lcd;
    UI::ScreenManager mgr;
    Rig() : panel(dc), lcd(panel, cs, dc, nreset, te) {
        MKDIR("screens");
        UI::StatusLine s = {};
        snprintf(s.left, sizeof s.left, "RUN 150rpm"); s.leftColor = UI::Theme::GREEN;
        snprintf(s.mid, sizeof s.mid, "CM 1/8");        s.midColor = UI::Theme::VIOLET;
        snprintf(s.right, sizeof s.right, "FL");        s.rightColor = UI::Theme::YELLOW;
        mgr.setStatus(s);
    }
    void start(UI::IScreen* root) { mgr.init(root, &lcd); }
    void frame() { mgr.render(); }
    void key(UI::JoyDirection d) { mgr.handleInput(d, true); }
    void dump(const char* name) {
        char path[64]; snprintf(path, sizeof path, "screens/%s.ppm", name);
        panel.writePpm(path);
    }
};

/// Pixels of `color` outside the content band and outside the chrome's own strips
uint32_t outsideBand(const PanelSim& p, uint16_t color)
{
    return p.countColor(color, 0, 0, 240, UI::Theme::CONTENT_Y)
         + p.countColor(color, 0, UI::Theme::CONTENT_BOTTOM + 1, 240,
                        static_cast<uint16_t>(320 - UI::Theme::CONTENT_BOTTOM - 1));
}

} // namespace

TEST(dispatcher_draws_under_the_chrome_in_theme_colours) {
    setupServices(3, 2, 96);
    Rig rig;
    UI::DispatcherScreen screen;
    rig.start(&screen);
    rig.frame();
    rig.dump("dispatcher");
    const uint32_t first = rig.panel.pixelsWritten;
    printf("  dispatcher: first frame %u px\n", first);

    // The chrome is there: title tab, footer
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::CYAN, 0, 0, 240, 26) > 500);
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::PANEL, 0, UI::Theme::FOOTER_Y, 240, 13) > 1000);
    // No stock colours anywhere: the old screen's black page, pure white, pure cyan, pure red
    EXPECT_EQ(rig.panel.countColor(0xFFFF), 0u);
    EXPECT_EQ(rig.panel.countColor(0x07FF), 0u);
    EXPECT_EQ(rig.panel.countColor(0xF800), 0u);
    EXPECT_EQ(rig.panel.countColor(0x07E0), 0u);
    // Black only as plot interiors, inside the band
    EXPECT_EQ(outsideBand(rig.panel, UI::Theme::BLACK), 0u);
    // Content colours stay in the band (the chrome uses none of these)
    EXPECT_EQ(outsideBand(rig.panel, UI::Theme::ORANGE), 0u);
    EXPECT_EQ(outsideBand(rig.panel, UI::Theme::RED), 0u);
    // The service bars are lit in their theme colours, Comms the busiest
    const uint16_t barsY = 106, barsH = 80;
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::GREEN, 30, barsY, 170, barsH) > 300);
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::RED, 30, barsY, 170, barsH) > 100);
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::ORANGE, 30, barsY, 170, barsH) > 50);
    EXPECT_EQ(rig.panel.countColor(UI::Theme::MAGENTA, 30, barsY + 4 * 10, 170, 10), 0u);   // SysId idle
    // The queue history: 3 commands pending shows as an orange column at the right end
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::ORANGE, 200, 192, 34, 16) > 0);
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::BLACK, 52, 192, 180, 16) > 2000);
    // Recent commands are listed in body text
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::TEXT, 0, 246, 240, 52) > 300);

    // An unchanged frame sends almost nothing (one new history column at most)
    rig.frame();
    const uint32_t second = rig.panel.pixelsWritten - first;
    printf("  dispatcher: unchanged frame %u px\n", second);
    EXPECT_TRUE(second <= 2u * 3u * 16u);

    // Let the history fill, then a steady frame sends nothing at all
    for (int i = 0; i < 70; i++) rig.frame();
    const uint32_t before = rig.panel.pixelsWritten;
    rig.frame();
    EXPECT_EQ(rig.panel.pixelsWritten - before, 0u);
    rig.dump("dispatcher_full");

    // A scroll repaints the list rows and the position note only
    const uint32_t beforeScroll = rig.panel.pixelsWritten;
    rig.key(UI::JoyDirection::DOWN);
    rig.frame();
    const uint32_t scrollCost = rig.panel.pixelsWritten - beforeScroll;
    printf("  dispatcher: one scroll step %u px\n", scrollCost);
    EXPECT_TRUE(scrollCost > 0u);
    EXPECT_TRUE(scrollCost <= 4u * 228u * 13u + 30u * 8u);
    rig.key(UI::JoyDirection::DOWN);
    rig.key(UI::JoyDirection::DOWN);
    rig.key(UI::JoyDirection::DOWN);
    rig.key(UI::JoyDirection::DOWN);   // clamps: 8 commands, 4 on show
    rig.frame();
    rig.dump("dispatcher_scrolled");
}

TEST(dispatcher_idle_shows_empty_bars_and_no_commands) {
    setupServices(0, 0, 0);
    g_proc.stats.recentCount = 0;
    g_proc.stats.totalCommands = 0;
    g_proc.stats.unknownCommands = 0;
    Rig rig;
    UI::DispatcherScreen screen;
    rig.start(&screen);
    rig.frame();
    rig.dump("dispatcher_idle");
    EXPECT_EQ(outsideBand(rig.panel, UI::Theme::BLACK), 0u);
    // Every bar track is black, nothing lit
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::BLACK, 34, 106, 166, 80) >= 8u * 166u * 6u);
    EXPECT_EQ(rig.panel.countColor(UI::Theme::ORANGE), 0u);
    EXPECT_EQ(rig.panel.countColor(UI::Theme::RED), 0u);
}
