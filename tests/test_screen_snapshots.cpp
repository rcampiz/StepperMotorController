/**
 * @file test_screen_snapshots.cpp
 * @brief The LCD screens rendered through the real L4 driver into a simulated panel.
 *
 * Two jobs: assertions on what a screen paints (so layout regressions fail the build), and a
 * PPM dump of each screen under screens/ in the build directory for a look on the host
 * (scripts/lcd/snapshots_to_png.py converts them). The render cost per frame is recorded too,
 * since every pixel sent twice is flicker on the panel.
 */
#include "minitest.hpp"
#include "fakes/fake_board.hpp"
#include "fakes/panel_sim.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include "L3_services/ui/menu_screen/menu_screen.hpp"
#include "L3_services/ui/screens/encoder_screen.hpp"
#include "L3_services/ui/screens/graph_screen.hpp"
#include "L3_services/ui/screens/device_info_screen.hpp"
#include "L3_services/ui/screen_manager/screen_manager.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include "L3_services/ui/theme.hpp"
#include "F_platform/boundary/iclock.hpp"
#include "F_platform/boundary/ilock.hpp"
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

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

void setEncoder(int64_t count, int32_t velocity) {
    Services::EncoderTelemetryData d;
    d.count = count; d.velocity = velocity; d.indexSeen = true; d.sampleRateHz = 1000;
    Services::g_telemetryStore.writeEncoder(d);
}

} // namespace

TEST(menu_paints_every_item_and_the_selection_bar) {
    Rig rig;
    UI::MenuScreen menu("Main Menu");
    const char* items[] = {"Boot Color Screen", "Device Info", "Encoder Monitor", "Motion Control",
                           "Driver Config", "Telemetry Graph", "Flash Images", "Trace Monitor",
                           "Task Monitor", "Dispatcher", "Architecture"};
    for (const char* it : items) menu.addItem(it);
    rig.start(&menu);
    rig.frame();
    rig.dump("menu");
    const uint32_t firstFrame = rig.panel.pixelsWritten;
    EXPECT_TRUE(firstFrame >= 240u * 320u);                 // the whole panel was painted once
    EXPECT_TRUE(firstFrame < 240u * 320u * 2);              // but nowhere near twice
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::PINK, 0, UI::Theme::CONTENT_Y, 240, 20) > 500);  // bar on item 0
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::CYAN, 0, 0, 240, 26) > 500);                     // the title tab
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::PANEL, 0, UI::Theme::FOOTER_Y, 240, 13) > 1000); // the footer
    rig.key(UI::JoyDirection::DOWN);
    rig.frame();
    rig.dump("menu_down");
    const uint32_t moveCost = rig.panel.pixelsWritten - firstFrame;
    printf("  menu: first frame %u px, one selection move %u px\n", firstFrame, moveCost);
    EXPECT_TRUE(moveCost <= 2u * 228u * 18u);               // two rows, each painted once
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::PINK, 0, UI::Theme::CONTENT_Y, 240, 20) == 0);   // bar moved off item 0
    rig.frame();
    EXPECT_EQ(rig.panel.pixelsWritten - firstFrame, moveCost);   // nothing changes: nothing is sent
    // A status change repaints only the status strip
    UI::StatusLine s = {};
    snprintf(s.left, sizeof s.left, "HOLD 0rpm"); s.leftColor = UI::Theme::CYAN;
    snprintf(s.mid, sizeof s.mid, "CM 1/8"); snprintf(s.right, sizeof s.right, "FL");
    const uint32_t beforeStatus = rig.panel.pixelsWritten;
    rig.mgr.setStatus(s);
    rig.frame();
    EXPECT_TRUE(rig.panel.pixelsWritten - beforeStatus < 240u * 12u);
}

TEST(a_long_menu_scrolls_and_the_back_hint_appears_below_the_root) {
    Rig rig;
    UI::MenuScreen root("Main Menu");
    root.addItem("Only");
    UI::MenuScreen menu("Images");
    char names[20][8];
    for (int i = 0; i < 20; i++) { snprintf(names[i], 8, "Img %d", i); menu.addItem(names[i]); }
    rig.start(&root);
    rig.frame();
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::CYAN, 4, UI::Theme::FOOTER_Y, 56, 13) == 0);  // no "<" at the root
    rig.mgr.push(&menu);
    rig.frame();
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::CYAN, 4, UI::Theme::FOOTER_Y, 56, 13) > 0);   // "<BACK"
    for (int i = 0; i < 16; i++) rig.key(UI::JoyDirection::DOWN);
    rig.frame();
    rig.dump("menu_scrolled");
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::PINK, 0, UI::Theme::CONTENT_Y, 240, 265) > 500);  // selection visible
    rig.key(UI::JoyDirection::LEFT);
    EXPECT_EQ(rig.mgr.depth(), 1);
}

TEST(encoder_screen_shows_the_dial_and_the_count) {
    EXPECT_TRUE(initStore());
    setEncoder(1366, 10000);
    Rig rig;
    UI::MenuScreen root("Main Menu");
    UI::EncoderScreen enc;
    rig.start(&root);
    rig.mgr.push(&enc);
    rig.frame();
    const uint32_t first = rig.panel.pixelsWritten;
    setEncoder(1400, 10000);
    rig.frame();
    rig.dump("encoder");
    const uint32_t update = rig.panel.pixelsWritten - first;
    printf("  encoder: first frame %u px, one update %u px\n", first, update);
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::MAGENTA, 60, 60, 120, 120) > 30);   // the needle
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::CYAN, 50, 45, 140, 140) > 300);     // the ring
    EXPECT_TRUE(update < 4000);                                 // needle, angle, two cells, two scope columns
    // 400 frames later the scope has swept the width and wrapped; nothing else grows
    for (int i = 0; i < 400; i++) { setEncoder(1400 + i * 3, 10000 - i * 40); rig.frame(); }
    rig.dump("encoder_swept");
}

TEST(device_info_repaints_only_the_live_cells) {
    EXPECT_TRUE(initStore());
    Rig rig;
    UI::MenuScreen root("Main Menu");
    UI::DeviceInfoScreen info;
    rig.start(&root);
    rig.mgr.push(&info);
    rig.frame();
    const uint32_t first = rig.panel.pixelsWritten;
    rig.frame();
    EXPECT_EQ(rig.panel.pixelsWritten, first);                 // same telemetry: nothing sent
    Protocol::SystemTelemetry sys = {};
    sys.uptimeTicks = 61000; sys.freeHeap = 12345; sys.cpuLoad = 7;
    Services::g_telemetryStore.writeSystem(sys);
    rig.frame();
    rig.dump("device_info");
    const uint32_t cost = rig.panel.pixelsWritten - first;
    printf("  device info: first frame %u px, live update %u px\n", first, cost);
    EXPECT_TRUE(cost <= 3u * 116u * 16u);                      // three value cells at most
}

TEST(graph_screen_draws_a_trace_from_telemetry) {
    EXPECT_TRUE(initStore());
    Rig rig;
    UI::MenuScreen root("Main Menu");
    UI::GraphScreen graph;
    rig.start(&root);
    rig.mgr.push(&graph);
    for (int i = 0; i < 120; i++) {
        setEncoder(i * 100, (i % 40 < 20) ? 10000 : 2000);
        rig.frame();
    }
    const uint32_t before = rig.panel.pixelsWritten;
    rig.frame();
    rig.dump("graph");
    printf("  graph: one frame %u px\n", rig.panel.pixelsWritten - before);
    EXPECT_TRUE(rig.panel.windows > 0);
}

