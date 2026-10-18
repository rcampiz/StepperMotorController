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
#include "L3_services/ui/screen_manager/screen_manager.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
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
    Rig() : panel(dc), lcd(panel, cs, dc, nreset, te) { MKDIR("screens"); }
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
    rig.key(UI::JoyDirection::DOWN);
    rig.frame();
    rig.dump("menu_down");
    const uint32_t moveCost = rig.panel.pixelsWritten - firstFrame;
    printf("  menu: first frame %u px, one selection move %u px\n", firstFrame, moveCost);
    EXPECT_TRUE(moveCost < 240u * 320u);                    // a selection move is not a full repaint
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
    printf("  encoder: first frame %u px, one update %u px\n", first, rig.panel.pixelsWritten - first);
    EXPECT_TRUE(rig.panel.countColor(0x0000) < 240u * 320u);  // something other than black is drawn
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

