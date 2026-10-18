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
#include "L3_services/ui/screens/arch_screen.hpp"
#include "L3_services/ui/screens/saver_screen.hpp"
#include "L3_services/ui/screens/recovery_screen.hpp"
#include "L3_services/ui/screens/screen_care_screen.hpp"
#include "L3_services/infra/indicator_service/indicator_service.hpp"
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

#include "L3_services/infra/trace/trace.hpp"

// The architecture screen reads the trace ring; the host build has no ring, so it is empty
namespace Trace {
size_t getTotal() { return 0; }
size_t getCount() { return 0; }
bool getEntry(size_t, Entry&) { return false; }
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

TEST(architecture_screen_cycles_its_views_with_right_and_leaves_with_left) {
    Rig rig;
    UI::MenuScreen root("Main Menu");
    UI::ArchScreen arch;
    rig.start(&root);
    rig.mgr.push(&arch);
    rig.frame();
    rig.dump("arch_diagram");
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::PANEL, 0, UI::Theme::CONTENT_Y, 240, 240) > 6 * 200 * 30);  // six bands
    rig.key(UI::JoyDirection::RIGHT); rig.frame(); rig.dump("arch_matrix");
    rig.key(UI::JoyDirection::RIGHT); rig.frame(); rig.dump("arch_flow");
    rig.key(UI::JoyDirection::RIGHT); rig.frame(); rig.dump("arch_tasks");
    EXPECT_EQ(rig.mgr.depth(), 2);
    rig.key(UI::JoyDirection::LEFT);
    EXPECT_EQ(rig.mgr.depth(), 1);
}

static int s_recoveryRuns = 0;
static void countRecovery() { s_recoveryRuns++; }

TEST(screen_care_edits_settings_and_starts_recovery) {
    Rig rig;
    UI::MenuScreen root("Main Menu");
    UI::ScreenCareSettings care;
    UI::ScreenCareScreen screen(&care, &countRecovery);
    rig.start(&root);
    rig.mgr.push(&screen);
    rig.frame();
    rig.dump("screen_care");
    rig.key(UI::JoyDirection::CENTER);                   // edit "Saver after"
    rig.key(UI::JoyDirection::RIGHT);                    // 2 -> 5 min
    rig.key(UI::JoyDirection::CENTER);
    EXPECT_EQ(care.saverAfterMs, 300000u);
    rig.frame();
    rig.key(UI::JoyDirection::DOWN); rig.key(UI::JoyDirection::DOWN); rig.key(UI::JoyDirection::DOWN);
    rig.key(UI::JoyDirection::CENTER);                   // Run recovery
    EXPECT_EQ(s_recoveryRuns, 1);
    rig.key(UI::JoyDirection::LEFT);
    EXPECT_EQ(rig.mgr.depth(), 1);
}

TEST(saver_moves_its_mark_and_any_key_leaves_it) {
    Rig rig;
    UI::MenuScreen root("Main Menu");
    UI::ScreenCareSettings care;
    UI::SaverScreen saver(&care);
    rig.start(&root);
    rig.mgr.push(&saver);
    rig.frame();
    const uint32_t black = rig.panel.countColor(0x0000);
    EXPECT_TRUE(black > 240u * 320u / 2);                // mostly black: band, card, nothing else
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::MAGENTA) > 500);   // stripes and the aberration
    for (int i = 0; i < 40; i++) rig.frame();
    rig.dump("saver");
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::CYAN) > 0);
    rig.key(UI::JoyDirection::DOWN);
    EXPECT_EQ(rig.mgr.depth(), 1);
    care.saverStyle = UI::SaverStyle::BLANK;
    rig.mgr.push(&saver);
    rig.frame();
    const uint32_t before = rig.panel.pixelsWritten;
    for (int i = 0; i < 80; i++) rig.frame();
    EXPECT_EQ(rig.panel.pixelsWritten, before);          // blank: nothing is sent at all
}

TEST(recovery_cycles_full_fields_and_pops_itself_when_done) {
    Rig rig;
    UI::MenuScreen root("Main Menu");
    UI::RecoveryScreen rec;
    rec.setMinutes(1);
    rig.start(&root);
    rig.mgr.push(&rec);
    rig.frame();
    EXPECT_TRUE(rig.panel.countColor(0xFFFF) > 240u * 320u - 200u);   // first field: white
    for (int i = 0; i < 40; i++) rig.frame();
    EXPECT_TRUE(rig.panel.countColor(0x0000) > 240u * 320u - 200u);   // then black
    rig.dump("recovery");
    for (int i = 0; i < 1200; i++) rig.frame();
    EXPECT_TRUE(rec.finished());
}

static void settle(Services::IndicatorService& ind, int ticks = 25) {
    for (int i = 0; i < ticks; i++) ind.tick();
}

TEST(motion_indicator_shows_heading_speed_and_turn_magnitudes) {
    Rig rig;
    Services::IndicatorService ind;
    ind.init(&rig.lcd);
    ind.draw({45, 1, true, 73, 40});                      // heading 45 deg, CW turn, 73 % speed, 40 % turn
    EXPECT_TRUE(ind.currentSpeed() == 0.0f);              // draw() only sets the target
    settle(ind);
    EXPECT_TRUE(ind.currentSpeed() == 73.0f && ind.currentTurn() == 40.0f && ind.currentAngle() == 45.0f);
    rig.dump("indicator");
    const uint32_t lit40 = rig.panel.countColor(0x079F, 20, 60, 200, 32);    // top band: rings only
    const uint32_t unlit40 = rig.panel.countColor(0x3B31, 20, 60, 200, 200);
    EXPECT_TRUE(lit40 > 0 && unlit40 > 0);                // one full ring, one partial, one hairline
    EXPECT_TRUE(rig.panel.countColor(0xF8DB, 60, 100, 120, 120) > 150);     // magenta outline
    const uint32_t fill73 = rig.panel.countColor(0x079F, 70, 110, 100, 100);
    EXPECT_TRUE(fill73 > 200);                            // cyan fill
    EXPECT_TRUE(rig.panel.countColor(0xF8DB, 20, 50, 200, 30) > 20);         // the top chevron
    EXPECT_TRUE(rig.panel.countColor(0x4A8D, 0, 300, 240, 20) > 20);         // the way-out hint
    // settled and unchanged: nothing is sent
    const uint32_t before = rig.panel.pixelsWritten;
    settle(ind, 5);
    EXPECT_EQ(rig.panel.pixelsWritten, before);
    // a speed change repaints only the arrow disc, a little at a time
    ind.draw({45, 1, true, 20, 40});
    ind.tick();
    const uint32_t first = rig.panel.pixelsWritten - before;
    printf("  indicator: one easing step of the arrow %u px\n", first);
    EXPECT_TRUE(first > 0 && first < 19000u);              // the arrow disc and the readout, not the rings
    EXPECT_TRUE(ind.currentSpeed() > 20.0f && ind.currentSpeed() < 73.0f);  // eased, not jumped
    settle(ind);
    rig.dump("indicator_20");
    EXPECT_TRUE(rig.panel.countColor(0x079F, 70, 110, 100, 100) < fill73);
    // full turn: every ring lit, no hairline left
    ind.draw({45, -1, true, 20, 100});
    settle(ind);
    rig.dump("indicator_full");
    EXPECT_TRUE(rig.panel.countColor(0x3B31, 20, 60, 200, 200) == 0);
    EXPECT_TRUE(rig.panel.countColor(0x079F, 20, 60, 200, 32) > lit40);
    // nothing to show: the zones go dark
    ind.clear();
    settle(ind);
    EXPECT_TRUE(rig.panel.countColor(0x079F, 20, 60, 200, 200) == 0);
    EXPECT_TRUE(rig.panel.countColor(0xF8DB, 20, 60, 200, 200) == 0);
}

TEST(graph_screen_draws_a_trace_from_telemetry) {
    EXPECT_TRUE(initStore());
    Rig rig;
    UI::MenuScreen root("Main Menu");
    UI::GraphScreen graph;
    rig.start(&root);
    rig.mgr.push(&graph);
    for (int i = 0; i < 260; i++) {
        setEncoder(i * 100, (i % 40 < 20) ? 10000 : 2000);
        rig.frame();
    }
    const uint32_t before = rig.panel.pixelsWritten;
    setEncoder(26000, 2000);
    rig.frame();
    rig.dump("graph");
    const uint32_t cost = rig.panel.pixelsWritten - before;
    printf("  graph: one scrolling frame %u px\n", cost);
    EXPECT_TRUE(cost < 12000);                                  // per-column diff, not the whole plot
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::CYAN, 32, 62, 200, 190) > 400);   // the trace
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::GRID_MAJ, 32, 62, 200, 190) > 800); // the major grid
    rig.key(UI::JoyDirection::CENTER);                          // next channel: fresh frame
    rig.frame();
    rig.key(UI::JoyDirection::LEFT);
    EXPECT_EQ(rig.mgr.depth(), 1);
}

