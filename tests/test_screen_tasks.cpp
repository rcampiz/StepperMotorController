/**
 * @file test_screen_tasks.cpp
 * @brief The Task Monitor screen (stats table, trace timeline, legend) rendered through the
 *        real L4 driver into a simulated panel, under the chrome.
 *
 * Checks that it draws only inside the content band, in theme colours, that an unchanged
 * frame sends nothing, and dumps every view to screens/ for a look on the host
 * (scripts/lcd/snapshots_to_png.py converts them).
 */
#include "minitest.hpp"
#include "fakes/fake_board.hpp"
#include "fakes/panel_sim.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include "L3_services/ui/menu_screen/menu_screen.hpp"
#include "L3_services/ui/screens/task_monitor_screen.hpp"
#include "L3_services/ui/screen_manager/screen_manager.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include "L3_services/infra/trace/trace.hpp"
#include "L3_services/ui/theme.hpp"
#include "F_platform/boundary/iclock.hpp"
#include "F_platform/boundary/ilock.hpp"
#include "F_platform/boundary/itask_stats.hpp"
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

// The trace ring stand-in (stubs/tasks_deps_stub.cpp)
namespace TraceStub {
void clear();
void push(const Trace::Entry& e);
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

NullLock g_lock; TickClock g_clock;
bool initStore() { static bool done = Services::g_telemetryStore.init(g_lock, g_clock); return done; }

/// The scheduler as the RTOS reports it
struct FakeTaskStats : Platform::ITaskStats {
    Platform::TaskStatsSnapshot snap = {};
    void add(const char* name, uint8_t state, uint8_t prio, uint16_t hwm, uint8_t cpu) {
        Platform::TaskStat& t = snap.tasks[snap.count++];
        snprintf(t.name, sizeof t.name, "%s", name);
        t.state = state; t.priority = prio; t.stackHWM = hwm; t.cpuPercent = cpu;
    }
    void getSnapshot(Platform::TaskStatsSnapshot& out) override { out = snap; }
    uint8_t getCurrentTaskIndex() override { return 0; }
};

FakeTaskStats g_stats;

void fillStats() {
    g_stats.snap = {};
    g_stats.add("Motor",   0, 4, 112, 12);
    g_stats.add("Encoder", 2, 5,  86, 31);
    g_stats.add("Comms",   2, 3, 210,  8);
    g_stats.add("Display", 1, 2, 148, 45);
    g_stats.add("Tmr Svc", 2, 6,  40,  0);
    g_stats.add("IDLE",    1, 0,  98, 4);
    Platform::setTaskStatsProvider(&g_stats);
    Protocol::SystemTelemetry sys = {};
    sys.uptimeTicks = 3723000; sys.freeHeap = 18432; sys.cpuLoad = 21;
    Services::g_telemetryStore.writeSystem(sys);
}

uint32_t s_tick = 100000;

void pushEntry(uint8_t task, uint8_t svc, uint32_t stepUs) {
    Trace::Entry e = {};
    s_tick += stepUs;
    e.tick = s_tick; e.tag = "T"; e.taskId = task; e.serviceId = svc;
    TraceStub::push(e);
}

/// A plausible scheduling pattern: busy encoder, motor bursts, comms with dispatch and config,
/// the display, the timer; mostly sub-ms steps, a 4 ms gap (yellow tick) and a 15 ms one (red)
void fillTrace(int n) {
    static const uint8_t TASKS[] = {2, 1, 2, 3, 3, 2, 4, 1, 2, 5, 3, 2};
    static const uint8_t SVCS[]  = {4, 1, 4, 6, 8, 4, 7, 3, 4, 0, 2, 4};
    for (int i = 0; i < n; i++) {
        uint32_t step = 350;
        if (i == 40) step = 4000;
        if (i == 80) step = 15000;
        pushEntry(TASKS[i % 12], SVCS[i % 12], step);
    }
}

constexpr uint16_t SENTINEL = 0x1234;   // not a theme colour

/// Rows outside the content band still hold the sentinel: the screen drew nothing there
bool outsideUntouched(const PanelSim& p) {
    const uint32_t above = 240u * UI::Theme::CONTENT_Y;
    const uint32_t below = 240u * (320u - UI::Theme::CONTENT_BOTTOM - 1u);
    return p.countColor(SENTINEL, 0, 0, 240, UI::Theme::CONTENT_Y) == above &&
           p.countColor(SENTINEL, 0, UI::Theme::CONTENT_BOTTOM + 1, 240,
                        static_cast<uint16_t>(320 - UI::Theme::CONTENT_BOTTOM - 1)) == below;
}

/// Pixels of a colour inside the content band
uint32_t inBand(const PanelSim& p, uint16_t color) {
    return p.countColor(color, 0, UI::Theme::CONTENT_Y, 240,
                        static_cast<uint16_t>(UI::Theme::CONTENT_BOTTOM - UI::Theme::CONTENT_Y + 1));
}

} // namespace

TEST(task_stats_table_under_the_chrome_repaints_only_changed_cells) {
    EXPECT_TRUE(initStore());
    fillStats();
    TraceStub::clear();
    Rig rig;
    UI::MenuScreen root("Main Menu");
    UI::TaskMonitorScreen tasks;
    rig.start(&root);
    rig.mgr.push(&tasks);
    rig.frame();
    rig.dump("tasks_stats");
    const uint32_t first = rig.panel.pixelsWritten;
    EXPECT_TRUE(strcmp(tasks.title(), "Tasks") == 0);
    EXPECT_TRUE(tasks.usesChrome());
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::CYAN, 0, 0, 240, 26) > 500);               // the title tab
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::PANEL, 0, UI::Theme::FOOTER_Y, 240, 13) > 1000);  // footer
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::BG) > 30000);                                // themed page
    EXPECT_EQ(inBand(rig.panel, 0x0000), 0u);                                             // no black page
    EXPECT_EQ(inBand(rig.panel, 0xFFFF), 0u);                                             // no stock white
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::TEXT) > 300);                                // task names
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::CYAN) > 100);                                // CPU bars
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::GREEN) > 20);                                // running state
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::ORANGE) > 20);                               // low stack (Tmr Svc)
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::RED) > 20);                                  // Motor colour tick
    // Unchanged: nothing is sent
    rig.frame();
    EXPECT_EQ(rig.panel.pixelsWritten, first);
    // One task's CPU moves: its bar and its percentage cell, nothing else
    g_stats.snap.tasks[1].cpuPercent = 64;
    rig.frame();
    const uint32_t cost = rig.panel.pixelsWritten - first;
    printf("  tasks stats: first frame %u px, one CPU change %u px\n", first, cost);
    EXPECT_TRUE(cost > 0 && cost <= 36u * 6u + 34u * 16u);
    // A task disappears: its row goes blank
    g_stats.snap.count = 5;
    rig.frame();
    EXPECT_EQ(rig.panel.countColor(UI::Theme::TEXT, 12, 56 + 5 * 18, 64, 16), 0u);
    g_stats.snap.count = 6;
    rig.key(UI::JoyDirection::LEFT);
    EXPECT_EQ(rig.mgr.depth(), 1);
}

TEST(task_timeline_paints_blocks_pauses_zooms_and_shows_the_legend) {
    EXPECT_TRUE(initStore());
    fillStats();
    TraceStub::clear();
    fillTrace(120);
    Rig rig;
    UI::MenuScreen root("Main Menu");
    UI::TaskMonitorScreen tasks;
    rig.start(&root);
    rig.mgr.push(&tasks);
    rig.frame();
    rig.key(UI::JoyDirection::RIGHT);
    EXPECT_TRUE(tasks.needsFullRedraw());                      // view switch: the page is re-framed
    rig.frame();
    rig.dump("tasks_timeline");
    EXPECT_TRUE(strcmp(tasks.title(), "Task Timeline") == 0);
    EXPECT_TRUE(strcmp(tasks.centerHint(), "PAUSE") == 0);
    const uint16_t px = UI::TaskMonitorScreen::PLOT_X, py = UI::TaskMonitorScreen::PLOT_Y;
    const uint16_t pw = UI::TaskMonitorScreen::PLOT_W, ph = UI::TaskMonitorScreen::PLOT_H;
    // Service-coloured blocks in the plot: encoder (cyan), motion (red), comms (green), dispatch
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::CYAN, px, py, pw, ph) > 500);
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::RED, px, py, pw, ph) > 100);
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::GREEN, px, py, pw, ph) > 100);
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::ORANGE, px, py, pw, ph) > 100);
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::GREEN, 6, 44, 60, 14) > 300);   // LIVE badge
    EXPECT_EQ(inBand(rig.panel, 0xFFFF), 0u);
    const uint32_t first = rig.panel.pixelsWritten;
    for (int i = 0; i < 6; i++) rig.frame();
    EXPECT_EQ(rig.panel.pixelsWritten, first);                // no new trace data: nothing sent
    // New entries: the plot is redrawn at ~5 Hz, per column, never cleared first
    fillTrace(3);
    for (int i = 0; i < 4; i++) rig.frame();
    const uint32_t live = rig.panel.pixelsWritten - first;
    printf("  tasks timeline: live update with 3 new entries %u px (plot %u px)\n", live, pw * ph);
    EXPECT_TRUE(live > 0 && live < pw * ph);
    // Pause: badge, hint and footer change; the cursor lane label turns magenta
    rig.key(UI::JoyDirection::CENTER);
    EXPECT_TRUE(strcmp(tasks.centerHint(), "RESUME") == 0);
    rig.key(UI::JoyDirection::DOWN);
    rig.frame();
    rig.dump("tasks_timeline_paused");
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::YELLOW, 6, 44, 60, 14) > 300);   // PAUSED badge
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::MAGENTA, 6, py, 36, ph) > 300);  // cursor on lane 2
    const uint32_t paused = rig.panel.pixelsWritten;
    fillTrace(2);
    for (int i = 0; i < 8; i++) rig.frame();
    EXPECT_TRUE(rig.panel.pixelsWritten - paused <= 168u * 16u);   // paused: only the entry count
    rig.key(UI::JoyDirection::LEFT);                          // pan older
    rig.frame();
    // Resume and zoom to a time window
    rig.key(UI::JoyDirection::CENTER);
    rig.key(UI::JoyDirection::UP);
    rig.key(UI::JoyDirection::UP);
    rig.key(UI::JoyDirection::UP);
    rig.frame();
    rig.dump("tasks_timeline_zoom");
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::CYAN, px, py, pw, ph) > 50);
    // Legend
    rig.key(UI::JoyDirection::RIGHT);
    rig.frame();
    rig.dump("tasks_legend");
    EXPECT_TRUE(strcmp(tasks.title(), "Task Legend") == 0);
    EXPECT_TRUE(tasks.centerHint() == nullptr);
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::MAGENTA) > 100);
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::ORANGE) > 100);
    EXPECT_EQ(inBand(rig.panel, 0xFFFF), 0u);
    const uint32_t legend = rig.panel.pixelsWritten;
    rig.frame();
    EXPECT_EQ(rig.panel.pixelsWritten, legend);               // static
    rig.key(UI::JoyDirection::LEFT);                          // back to the timeline
    rig.frame();
    EXPECT_TRUE(strcmp(tasks.title(), "Task Timeline") == 0);
    rig.key(UI::JoyDirection::LEFT);                          // back to the stats
    rig.frame();
    EXPECT_TRUE(strcmp(tasks.title(), "Tasks") == 0);
    rig.key(UI::JoyDirection::LEFT);
    EXPECT_EQ(rig.mgr.depth(), 1);
}

TEST(task_monitor_draws_only_inside_the_content_band) {
    EXPECT_TRUE(initStore());
    fillStats();
    TraceStub::clear();
    fillTrace(128);
    Rig rig;
    UI::TaskMonitorScreen tasks;
    tasks.onActivate();
    rig.lcd.fillScreen(SENTINEL);
    // Every view and state, drawn straight to the panel without the manager's chrome
    tasks.render(rig.lcd);                                         // stats
    EXPECT_TRUE(outsideUntouched(rig.panel));
    tasks.handleInput(UI::JoyDirection::RIGHT, true);              // timeline
    tasks.render(rig.lcd);
    EXPECT_TRUE(outsideUntouched(rig.panel));
    tasks.handleInput(UI::JoyDirection::CENTER, true);             // paused, cursor at the bottom
    for (int i = 0; i < 6; i++) tasks.handleInput(UI::JoyDirection::DOWN, true);
    tasks.render(rig.lcd);
    EXPECT_TRUE(outsideUntouched(rig.panel));
    tasks.handleInput(UI::JoyDirection::CENTER, true);
    for (int i = 0; i < 9; i++) tasks.handleInput(UI::JoyDirection::UP, true);   // widest zoom
    tasks.render(rig.lcd);
    EXPECT_TRUE(outsideUntouched(rig.panel));
    tasks.handleInput(UI::JoyDirection::RIGHT, true);              // legend
    tasks.render(rig.lcd);
    EXPECT_TRUE(outsideUntouched(rig.panel));
    // and the band itself was drawn in
    EXPECT_TRUE(inBand(rig.panel, SENTINEL) < 240u * 265u);
}
