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
#include "L3_services/ui/screens/estop_screen.hpp"
#include "L3_services/ui/display_service/panel_sleep.hpp"
#include "L3_services/ui/screens/motion_screen.hpp"
#include "L3_services/infra/indicator_service/indicator_service.hpp"
#include "L3_services/ui/screen_manager/screen_manager.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include "L3_services/ui/theme.hpp"
#include "L3_services/ui/display_scratch.hpp"
#include "F_platform/boundary/iclock.hpp"
#include "F_platform/boundary/ilock.hpp"
#include <sys/stat.h>
#include <vector>
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

/// The whole panel, for comparing two frames
std::vector<uint16_t> grab(const PanelSim& panel) {
    std::vector<uint16_t> out;
    out.reserve(240u * 320u);
    for (uint16_t y = 0; y < 320; y++)
        for (uint16_t x = 0; x < 240; x++) out.push_back(panel.px(x, y));
    return out;
}
// First frames of the title pages, from their own tests (each the first to use the shared
// title-mask buffer in its turn), for the shared-buffer test further down
std::vector<uint16_t> g_saverTitleRef, g_estopRef;

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
    EXPECT_TRUE(moveCost <= 2u * 228u * 18u + 228u * 16u);  // two rows, each painted once (+ the animated bar)
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::PINK, 0, UI::Theme::CONTENT_Y, 240, 20) == 0);   // bar moved off item 0
    // Nothing changes: only the selected item animates (Hotline Miami), one bar every other frame
    const uint32_t beforeIdle = rig.panel.pixelsWritten;
    rig.frame(); rig.frame();
    EXPECT_EQ(rig.panel.pixelsWritten - beforeIdle, 228u * 16u);
    rig.dump("menu_anim_a");
    for (int i = 0; i < 9; i++) rig.frame();
    rig.dump("menu_anim_b");
    // A status change repaints only the status strip
    UI::StatusLine s = {};
    snprintf(s.left, sizeof s.left, "HOLD 0rpm"); s.leftColor = UI::Theme::CYAN;
    snprintf(s.mid, sizeof s.mid, "CM 1/8"); snprintf(s.right, sizeof s.right, "FL");
    const uint32_t beforeStatus = rig.panel.pixelsWritten;
    rig.mgr.setStatus(s);
    rig.frame();
    EXPECT_TRUE(rig.panel.pixelsWritten - beforeStatus < 240u * 12u + 228u * 16u);   // + maybe the bar
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
    EXPECT_TRUE(rig.panel.pixelsWritten - first <= 228u * 16u);  // same telemetry: only the animated selection
    Protocol::SystemTelemetry sys = {};
    sys.uptimeTicks = 61000; sys.freeHeap = 12345; sys.cpuLoad = 7;
    Services::g_telemetryStore.writeSystem(sys);
    rig.frame();
    rig.dump("device_info");
    const uint32_t cost = rig.panel.pixelsWritten - first;
    printf("  device info: first frame %u px, live update %u px\n", first, cost);
    EXPECT_TRUE(cost <= 3u * 116u * 16u + 2u * 228u * 16u);    // three value cells at most (+ the animated selection)
    // The wheel: CENTER edits, RIGHT steps through the roles, UP leaves without saving
    rig.key(UI::JoyDirection::CENTER);
    rig.key(UI::JoyDirection::RIGHT);
    rig.key(UI::JoyDirection::RIGHT);
    rig.frame();
    rig.dump("device_info_wheel");
    EXPECT_EQ(rig.mgr.current(), &info);
    rig.key(UI::JoyDirection::UP);
    rig.frame();
    rig.key(UI::JoyDirection::LEFT);                            // not editing: back to the menu
    EXPECT_EQ(rig.mgr.current(), &root);
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
    rig.key(UI::JoyDirection::DOWN);                     // "Sleep after": 15 min -> 30 min
    EXPECT_EQ(care.sleepAfterMs, 900000u);
    rig.key(UI::JoyDirection::CENTER);
    rig.key(UI::JoyDirection::RIGHT);
    rig.key(UI::JoyDirection::CENTER);
    EXPECT_EQ(care.sleepAfterMs, 1800000u);
    rig.frame();
    rig.dump("screen_care_sleep");
    rig.key(UI::JoyDirection::DOWN); rig.key(UI::JoyDirection::DOWN); rig.key(UI::JoyDirection::DOWN);
    rig.key(UI::JoyDirection::CENTER);                   // Run recovery
    EXPECT_EQ(s_recoveryRuns, 1);
    rig.key(UI::JoyDirection::LEFT);
    EXPECT_EQ(rig.mgr.depth(), 1);
}

TEST(saver_styles_animate_and_any_key_leaves) {
    Rig rig;
    UI::MenuScreen root("Main Menu");
    UI::ScreenCareSettings care;                          // default: synthwave
    UI::SaverScreen saver(&care);
    rig.start(&root);
    rig.mgr.push(&saver);
    rig.frame();                                          // a whole scene
    rig.dump("saver_synth");
    // Palettes vary from scene to scene: the look is checked by eye in the PNGs; here, that a
    // whole scene went out and that it is not a blank panel
    EXPECT_TRUE(rig.panel.pixelsWritten >= 240u * 320u);
    EXPECT_TRUE(rig.panel.countColor(0x0000) < 240u * 320u / 2);
    const uint32_t full = rig.panel.pixelsWritten;
    rig.frame(); rig.frame();
    const uint32_t perTwo = rig.panel.pixelsWritten - full;
    printf("  saver: full scene %u px, two animation frames %u px\n", full, perTwo);
    // the floor rows a grid line moved through, the sun box and the bands' rows: not the whole
    // floor (it was, 40-80 KB a frame) and not the sky
    EXPECT_TRUE(perTwo > 2u * 240u * 4u && perTwo < 2u * 240u * 60u);
    for (int i = 0; i < 10; i++) rig.frame();
    rig.dump("saver_synth_later");
    // the title fades in 5 s in and rocks; a new scene at 30 s has another palette and horizon
    for (int i = 0; i < 128; i++) rig.frame();
    rig.dump("saver_synth_title");
    g_saverTitleRef = grab(rig.panel);                    // frame 141
    // the letters' hard shadows, in whichever of the three flash colours is up
    auto c565 = [](int r, int g, int b) { return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); };
    auto shadows = [&]() {
        return rig.panel.countColor(c565(40, 200, 255)) + rig.panel.countColor(c565(240, 0, 150)) +
               rig.panel.countColor(c565(255, 30, 120));
    };
    EXPECT_TRUE(shadows() > 300);
    // the version ("v" + Harness::fwVersion()) under the name: its magenta shadow lies below the
    // title's shadows and centred under them
    {
        const uint16_t titleInk[3] = {c565(40, 200, 255), c565(240, 0, 150), c565(255, 30, 120)};
        const uint16_t verInk = c565(255, 20, 170);
        uint32_t nT = 0, nV = 0;
        long sxT = 0, syT = 0, syTmax = 0, sxV = 0, syV = 0, syVmin = 320;
        for (uint16_t y = 0; y < 320; y++)
            for (uint16_t x = 0; x < 240; x++) {
                const uint16_t c = rig.panel.px(x, y);
                if (c == titleInk[0] || c == titleInk[1] || c == titleInk[2]) {
                    nT++; sxT += x; syT += y; if (y > syTmax) syTmax = y;
                } else if (c == verInk) {
                    nV++; sxV += x; syV += y; if (y < syVmin) syVmin = y;
                }
            }
        printf("  saver: title shadow %u px (rows to %ld), version shadow %u px (rows from %ld)\n",
               nT, syTmax, nV, syVmin);
        EXPECT_TRUE(nV > 60);
        EXPECT_TRUE(nT > 0 && nV > 0 && syV / static_cast<long>(nV) > syT / static_cast<long>(nT) + 40);
        const long dx = (nT > 0 && nV > 0) ? sxV / static_cast<long>(nV) - sxT / static_cast<long>(nT) : 99;
        EXPECT_TRUE(dx > -12 && dx < 12);
        EXPECT_TRUE(syVmin + 12 > syTmax);                       // under CONTROLLER, give or take the rock
    }
    for (int i = 0; i < 21; i++) rig.frame();                  // a quarter rock later, another flash
    rig.dump("saver_synth_title_rock");
    EXPECT_TRUE(shadows() > 300);
    const uint8_t pal0 = saver.palette();
    for (int i = 0; i < 459; i++) rig.frame();
    rig.dump("saver_synth_next");
    EXPECT_TRUE(saver.palette() != pal0);
    rig.key(UI::JoyDirection::DOWN);
    EXPECT_EQ(rig.mgr.depth(), 1);
    // hotline: stripes and the card
    care.saverStyle = UI::SaverStyle::HOTLINE;
    rig.mgr.push(&saver);
    for (int i = 0; i < 41; i++) rig.frame();
    rig.dump("saver_hotline");
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::MAGENTA) > 500);
    rig.key(UI::JoyDirection::LEFT);
    // blank: nothing at all is sent
    care.saverStyle = UI::SaverStyle::BLANK;
    rig.mgr.push(&saver);
    rig.frame();
    const uint32_t before = rig.panel.pixelsWritten;
    for (int i = 0; i < 80; i++) rig.frame();
    EXPECT_EQ(rig.panel.pixelsWritten, before);
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

static uint32_t g_indNowMs = 0;                       // the indicator's clock in these tests
static void indTick(Services::IndicatorService& ind, uint32_t stepMs = 50) {
    g_indNowMs += stepMs;
    ind.tick(g_indNowMs);
}
static void settle(Services::IndicatorService& ind, int ticks = 25) {
    for (int i = 0; i < ticks; i++) indTick(ind);
}

TEST(the_selection_animates_on_time_not_on_frames) {
    // 2026-10-19: the bar swayed fast enough to make someone dizzy when the Display task
    // rendered more often than 20 Hz. Its pace now follows the clock: many renders within a
    // few ms repaint it at most once, and with time moving it repaints every 100 ms.
    Rig rig;
    TickClock clock;
    UI::MenuScreen menu("Main Menu");
    menu.addItem("Encoder"); menu.addItem("Motion");
    rig.mgr.setClock(&clock);
    rig.start(&menu);
    rig.frame();
    clock.now += 200; rig.frame();                    // settle: one repaint now due
    const uint32_t before = rig.panel.pixelsWritten;
    for (int i = 0; i < 20; i++) { clock.now += 2; rig.frame(); }      // 20 renders in 40 ms
    EXPECT_TRUE(rig.panel.pixelsWritten - before <= 228u * 16u);
    const uint32_t mid = rig.panel.pixelsWritten;
    for (int i = 0; i < 10; i++) { clock.now += 50; rig.frame(); }     // 500 ms at 20 Hz
    const uint32_t bars = (rig.panel.pixelsWritten - mid) / (228u * 16u);
    EXPECT_TRUE(bars >= 4u && bars <= 6u);                              // ~every 100 ms
}

TEST(panel_sleeps_after_the_saver_has_run_and_wakes_every_time) {
    using A = UI::PanelSleep::Action;
    UI::PanelSleep ps;
    const uint32_t AFTER = 15u * 60u * 1000u;
    uint32_t t = 1000;
    EXPECT_TRUE(ps.tick(false, t, AFTER) == A::NONE);              // menu up: nothing
    EXPECT_TRUE(ps.tick(true, t, AFTER) == A::NONE);               // saver starts
    EXPECT_TRUE(ps.tick(true, t + AFTER - 1, AFTER) == A::NONE);
    EXPECT_TRUE(ps.tick(true, t + AFTER, AFTER) == A::SLEEP);      // 15 min of saver
    EXPECT_TRUE(ps.asleep());
    EXPECT_TRUE(ps.tick(true, t + AFTER + 60000, AFTER) == A::NONE);  // stays asleep
    // a joystick move pops the saver: wake
    t = t + AFTER + 120000;
    EXPECT_TRUE(ps.tick(false, t, AFTER) == A::WAKE);
    EXPECT_FALSE(ps.asleep());
    // and it sleeps again after another full saver run, and wakes again (the host this time)
    EXPECT_TRUE(ps.tick(true, t + 100, AFTER) == A::NONE);
    EXPECT_TRUE(ps.tick(true, t + 100 + AFTER, AFTER) == A::SLEEP);
    EXPECT_TRUE(ps.tick(false, t + 100 + AFTER + 1000, AFTER) == A::WAKE);
    // a wake right after sleeping is immediate (the controller is never put to sleep)
    EXPECT_TRUE(ps.tick(true, 0, 1000) == A::NONE);
    EXPECT_TRUE(ps.tick(true, 1000, 1000) == A::SLEEP);
    EXPECT_TRUE(ps.tick(false, 1001, 1000) == A::WAKE);
    // 0 = never
    UI::PanelSleep never;
    EXPECT_TRUE(never.tick(true, 0, 0) == A::NONE);
    EXPECT_TRUE(never.tick(true, 100000000u, 0) == A::NONE);
}

TEST(motion_jog_is_sized_in_revolutions_at_the_microstep_setting) {
    // 200 full steps a turn: 1/128 -> 25600 microsteps a turn, 1/16 -> 3200
    EXPECT_EQ(UI::MotionScreen::milliRevToMicrosteps(1000, 25600), 25600);
    EXPECT_EQ(UI::MotionScreen::milliRevToMicrosteps(125, 25600), 3200);
    EXPECT_EQ(UI::MotionScreen::milliRevToMicrosteps(250, 3200), 800);
    EXPECT_EQ(UI::MotionScreen::milliRevToMicrosteps(10000, 51200), 512000);
    EXPECT_EQ(UI::MotionScreen::milliRevToMicrosteps(-500, 3200), -1600);
    char buf[20];
    UI::MotionScreen::formatEighths(buf, sizeof buf, 1, '+');  EXPECT_TRUE(strcmp(buf, "+1/8 rev") == 0);
    UI::MotionScreen::formatEighths(buf, sizeof buf, 2, 0);    EXPECT_TRUE(strcmp(buf, "1/4 rev") == 0);
    UI::MotionScreen::formatEighths(buf, sizeof buf, 3, 0);    EXPECT_TRUE(strcmp(buf, "3/8 rev") == 0);
    UI::MotionScreen::formatEighths(buf, sizeof buf, 8, '-');  EXPECT_TRUE(strcmp(buf, "-1 rev") == 0);
    UI::MotionScreen::formatEighths(buf, sizeof buf, 10, '+'); EXPECT_TRUE(strcmp(buf, "+1 1/4 rev") == 0);
    UI::MotionScreen::formatEighths(buf, sizeof buf, 13, 0);   EXPECT_TRUE(strcmp(buf, "1 5/8 rev") == 0);
    UI::MotionScreen::formatEighths(buf, sizeof buf, 80, 0);   EXPECT_TRUE(strcmp(buf, "10 rev") == 0);
    // 1/8 rev at 1/16 (3200 microsteps a turn, current mode) is exactly 400 microsteps
    EXPECT_EQ(UI::MotionScreen::milliRevToMicrosteps(3 * 125, 3200), 1200);
}

// Every page with setting rows explains the selected row under its key hint (Chrome::HelpArea):
// texts from a const table, two lines at most, repainted only when the selection changes.
// The Driver Config and Motion pages' help is in test_screen_config.cpp (it links the motion
// services those pages read).
TEST(setting_pages_explain_the_selected_row) {
    for (uint8_t f = 0; f < 16; f++) {
        EXPECT_TRUE(UI::Chrome::helpFits(UI::ScreenCareScreen::helpText(f)));
    }
    EXPECT_TRUE(UI::Chrome::helpFits(UI::DeviceInfoScreen::helpText()));
    EXPECT_FALSE(UI::Chrome::helpFits("far too long for the help area: two lines of thirty-eight characters each, no more"));
    EXPECT_TRUE(initStore());

    // Screen Care: the help under the hint, the explanation block below it, all above the footer
    {
        Rig rig;
        UI::MenuScreen root("Main Menu");
        UI::ScreenCareSettings care;
        UI::ScreenCareScreen screen(&care, &countRecovery);
        rig.start(&root);
        rig.mgr.push(&screen);
        rig.key(UI::JoyDirection::DOWN);
        rig.key(UI::JoyDirection::DOWN);
        rig.frame();
        rig.dump("screen_care_help_style");                         // "Synthwave, hotline or blank."
        constexpr uint16_t HELP_Y = UI::Theme::CONTENT_Y + 4 + 5 * UI::Chrome::ROW_H + 4 + UI::Chrome::HELP_LINE_H;
        EXPECT_TRUE(rig.panel.countColor(UI::Theme::DIM, 0, HELP_Y, 240, UI::Chrome::HELP_LINE_H) > 50);
        rig.key(UI::JoyDirection::LEFT);
    }
}

TEST(motion_indicator_shows_heading_speed_and_turn_magnitudes) {
    using Ind = Services::IndicatorService;
    Rig rig;
    Services::IndicatorService ind;
    ind.init(&rig.lcd);
    ind.draw({45, 1, true, 73, 40});                      // heading 45 deg, CW turn, 73 % speed, 40 % turn
    EXPECT_TRUE(ind.currentSpeed() == 0.0f);              // draw() only sets the target
    settle(ind);
    EXPECT_TRUE(ind.currentSpeed() == 73.0f && ind.currentTurn() == 40.0f && ind.currentAngle() == 45.0f);
    rig.dump("indicator");
    const uint32_t lit40 = rig.panel.countColor(Ind::RING_LIT, 20, 60, 200, 32);    // top band: rings only
    const uint32_t unlit40 = rig.panel.countColor(Ind::RING_UNLIT, 20, 60, 200, 200);
    EXPECT_TRUE(lit40 > 0 && unlit40 > 0);                // one full ring, one partial, one hairline
    EXPECT_TRUE(rig.panel.countColor(Ind::ARROW_COLOR, 60, 100, 120, 120) > 150);   // white outline
    EXPECT_TRUE(rig.panel.countColor(Ind::GHOST_LEFT, 60, 100, 120, 120) > 40);     // the RGB split
    EXPECT_TRUE(rig.panel.countColor(Ind::GHOST_RIGHT, 60, 100, 120, 120) > 40);
    const uint32_t fill73 = rig.panel.countColor(Ind::FILL_COLOR, 70, 110, 100, 100);
    EXPECT_TRUE(fill73 > 200);                            // the speed fill
    EXPECT_TRUE(rig.panel.countColor(Ind::CHEVRON_COLOR, 20, 50, 200, 30) > 20);    // the top chevron
    EXPECT_TRUE(rig.panel.countColor(Ind::HINT_COLOR, 0, Ind::HINT_Y, 240, Ind::TEXT_H) > 200);   // the hint
    EXPECT_TRUE(rig.panel.countColor(Ind::READOUT_COLOR, 0, Ind::READOUT_Y, 240, Ind::TEXT_H) > 150);
    EXPECT_TRUE(rig.panel.countColor(Ind::DISC_RIM) > 300);                          // the dial's rim
    // settled: only the VHS bands' rows are sent, and they move down the screen
    const int16_t top0 = ind.bandTop(0);
    uint32_t before = rig.panel.pixelsWritten;
    indTick(ind);
    const uint32_t bandsOnly = rig.panel.pixelsWritten - before;
    printf("  indicator: one tick of the VHS bands %u px\n", bandsOnly);
    EXPECT_TRUE(bandsOnly > 0 && bandsOnly <= 30u * 240u);
    EXPECT_TRUE(ind.bandTop(0) != top0);
    // a speed change repaints the arrow's square (one window), a little at a time, plus the bands
    before = rig.panel.pixelsWritten;
    const int16_t top1 = ind.bandTop(0);
    ind.draw({45, 1, true, 20, 40});
    indTick(ind);
    const uint32_t first = rig.panel.pixelsWritten - before;
    EXPECT_TRUE(ind.bandTop(0) != top1);                  // a full period on: the bands move too
    const int16_t top2 = ind.bandTop(0);
    ind.draw({45, 1, true, 60, 40});
    indTick(ind, 10);                                     // a tick right after a late one:
    EXPECT_EQ(ind.bandTop(0), top2);                      // the arrow first, the bands wait
    printf("  indicator: one easing step of the arrow %u px\n", first);
    const uint32_t square = (2u * Ind::ARROW_R + 1) * (2u * Ind::ARROW_R + 1);
    const uint32_t plate = (Ind::READOUT_R - Ind::READOUT_L + 1u) * (Ind::READOUT_B - Ind::READOUT_T + 1u);
    EXPECT_TRUE(first >= square && first <= square + plate + 30u * 240u);   // not the rings
    EXPECT_TRUE(ind.currentSpeed() > 20.0f && ind.currentSpeed() < 73.0f);  // eased, not jumped
    settle(ind);
    rig.dump("indicator_20");
    EXPECT_TRUE(rig.panel.countColor(Ind::FILL_COLOR, 70, 110, 100, 100) < fill73);
    // full turn: every ring lit, no hairline left
    ind.draw({45, -1, true, 20, 100});
    settle(ind);
    rig.dump("indicator_full");
    EXPECT_TRUE(rig.panel.countColor(Ind::RING_UNLIT, 20, 60, 200, 200) == 0);
    EXPECT_TRUE(rig.panel.countColor(Ind::RING_LIT, 20, 60, 200, 32) > lit40);
    // holding CENTER: the hint plate fills from the left (the press is seen to register)
    ind.setHoldProgress(50);
    indTick(ind);
    rig.dump("indicator_hold");
    const uint16_t plateW = Ind::HINT_R - Ind::HINT_L + 1;
    const uint32_t swept = rig.panel.countColor(Ind::HOLD_FILL, Ind::HINT_L, Ind::HINT_T, plateW / 2, Ind::HINT_B - Ind::HINT_T + 1);
    EXPECT_TRUE(swept > 300u);                                                        // the left half filled
    EXPECT_EQ(rig.panel.countColor(Ind::HOLD_FILL, Ind::HINT_L + plateW / 2 + 1, Ind::HINT_T, plateW / 2 - 1, 16), 0u);
    ind.setHoldProgress(0);                                                           // let go: the plate clears
    indTick(ind);
    EXPECT_EQ(rig.panel.countColor(Ind::HOLD_FILL, Ind::HINT_L, Ind::HINT_T, plateW, 16), 0u);
    // nothing to show: the zones go dark
    ind.clear();
    settle(ind);
    EXPECT_TRUE(rig.panel.countColor(Ind::RING_LIT, 20, 60, 200, 200) == 0);
    EXPECT_TRUE(rig.panel.countColor(Ind::ARROW_COLOR, 20, 60, 200, 200) == 0);
    EXPECT_TRUE(rig.panel.countColor(Ind::CHEVRON_COLOR, 20, 60, 200, 200) == 0);
}

TEST(estop_page_shows_the_latch_and_fills_the_clear_bar) {
    EXPECT_TRUE(initStore());
    Rig rig;
    UI::MenuScreen root("Main Menu");
    UI::EstopScreen page;
    rig.start(&root);
    rig.mgr.push(&page);
    rig.frame();
    g_estopRef = grab(rig.panel);
    // the Hotline title: its extruded shadow, in whichever flash colour is up
    auto shadows = [&](uint16_t y, uint16_t h) {
        return rig.panel.countColor(UI::Theme::CYAN, 0, y, 240, h) + rig.panel.countColor(UI::Theme::YELLOW, 0, y, 240, h) +
               rig.panel.countColor(UI::Theme::MAGENTA, 0, y, 240, h);
    };
    EXPECT_TRUE(shadows(24, 56) > 400);
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::YELLOW, 0, 0, 240, 18) > 1500);     // hazard band
    const uint32_t empty = rig.panel.countColor(UI::Theme::PANEL, UI::EstopScreen::BAR_X, UI::EstopScreen::BAR_Y,
                                                UI::EstopScreen::BAR_W, UI::EstopScreen::BAR_H);
    page.setProgress(60);
    rig.frame();
    const uint32_t left = rig.panel.countColor(UI::Theme::PANEL, UI::EstopScreen::BAR_X, UI::EstopScreen::BAR_Y,
                                               UI::EstopScreen::BAR_W, UI::EstopScreen::BAR_H);
    EXPECT_TRUE(left * 100 / empty >= 35 && left * 100 / empty <= 45);              // 12 of 20 segments lit
    page.setMessage("Hardware fault active: UVLO");
    rig.frame();
    rig.dump("estop");
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::ORANGE, 0, 222, 240, 12) > 20);
    // Nothing stands still: every frame repaints the stripes and the grid...
    const uint32_t before = rig.panel.pixelsWritten;
    rig.frame();
    EXPECT_TRUE(rig.panel.pixelsWritten - before > 2u * 240u * 18u);
    // ...the page holds still while CENTER is held; let go and the cycle runs: after
    // SHOW_FRAMES it fades out to stripes and grid only...
    page.setProgress(0);
    for (int i = 0; i < UI::EstopScreen::SHOW_FRAMES + UI::EstopScreen::FADE_FRAMES + 2; i++) rig.frame();
    rig.dump("estop_dark");
    EXPECT_EQ(rig.panel.countColor(UI::Theme::RED, 0, 22, 240, 220), 0u);           // no title
    EXPECT_EQ(rig.panel.countColor(UI::Theme::YELLOW, 0, 22, 240, 276), 0u);        // no prompt
    // ...and fades back in on the next layout: the grid on top, the middle ~50 px lower
    for (int i = 0; i < UI::EstopScreen::DARK_FRAMES + UI::EstopScreen::FADE_FRAMES; i++) rig.frame();
    EXPECT_TRUE(page.gridOnTop());
    EXPECT_TRUE(page.offsetY() >= 40);
    EXPECT_TRUE(shadows(72, 56) > 400);                                              // its title, lower
    rig.dump("estop_shifted");
    rig.frame();
    rig.dump("estop_shifted_rock");
    // Holding CENTER mid-fade brings the page straight back
    for (int i = 0; i < UI::EstopScreen::SHOW_FRAMES + 3; i++) rig.frame();
    page.setProgress(10);
    rig.frame();
    EXPECT_TRUE(shadows(72, 56) > 400);
}

// The saver's and the E-STOP page's title masks share one buffer (UI::SharedTitleMask): each
// page must rebuild its mask after the other one had it. A page shown again after the other
// paints exactly what it painted the first time (the references, taken by the tests above).
TEST(the_saver_and_estop_titles_take_turns_in_one_buffer) {
    EXPECT_TRUE(initStore());
    EXPECT_EQ(g_saverTitleRef.size(), 240u * 320u);
    EXPECT_EQ(g_estopRef.size(), 240u * 320u);
    auto saverFrame = []() {
        Rig rig;
        UI::MenuScreen root("Main Menu");
        UI::ScreenCareSettings care;                      // synthwave
        UI::SaverScreen saver(&care);
        rig.start(&root);
        rig.mgr.push(&saver);
        for (int i = 0; i < 141; i++) rig.frame();        // the title is up (saver_synth_title)
        return grab(rig.panel);
    };
    auto estopFrame = []() {
        Rig rig;
        UI::MenuScreen root("Main Menu");
        UI::EstopScreen page;
        rig.start(&root);
        rig.mgr.push(&page);
        rig.frame();
        return grab(rig.panel);
    };
    // The E-STOP test above left its mask in the buffer: the saver takes it, then the page
    // takes it back with the same word cached
    EXPECT_TRUE(saverFrame() == g_saverTitleRef);
    EXPECT_TRUE(estopFrame() == g_estopRef);
    EXPECT_TRUE(saverFrame() == g_saverTitleRef);
    // The motion indicator builds its frames in the same buffer (remote mode, 2026-10-20): both
    // pages still come back exactly after it
    auto indicatorFrame = []() {
        Rig rig;
        Services::IndicatorService ind;
        ind.init(&rig.lcd);
        ind.draw({30, 1, true, 60, 50});
        for (uint32_t t = 50; t <= 1250; t += 50) ind.tick(t);
    };
    indicatorFrame();
    EXPECT_TRUE(saverFrame() == g_saverTitleRef);
    indicatorFrame();
    EXPECT_TRUE(estopFrame() == g_estopRef);
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

// Last in the file: every screen above has drawn through the shared row by now
TEST(the_shared_scratch_row_was_never_taken_twice_at_once) {
    EXPECT_EQ(UI::ScratchRow::collisions(), 0u);
}

TEST(the_indicator_eases_on_time_not_on_frames) {
    // 2026-10-20: the Display task runs ~5 frames/s while the arrow moves; per-frame easing lagged.
    // One 200 ms frame covers what four 50 ms frames do.
    Rig a, b;
    Services::IndicatorService fast, slow;
    fast.init(&a.lcd);
    slow.init(&b.lcd);
    fast.draw({90, 0, true, 100, 0});
    slow.draw({90, 0, true, 100, 0});
    fast.tick(1000);
    slow.tick(1000);
    for (uint32_t t = 1050; t <= 1200; t += 50) fast.tick(t);
    slow.tick(1200);
    EXPECT_NEAR(slow.currentAngle(), fast.currentAngle(), 0.5);
    EXPECT_NEAR(slow.currentSpeed(), fast.currentSpeed(), 0.5);
    EXPECT_TRUE(slow.currentAngle() > 80.0f);               // 0.55^5 of the way left: nearly there
}

TEST(the_arrow_appears_pointing_where_the_base_goes) {
    // 2026-10-20: from a stop (angle 0, no arrow) a flick left drew the arrow pointing up and
    // swung it round; it now appears at its heading, and later turns still ease
    Rig rig;
    Services::IndicatorService ind;
    ind.init(&rig.lcd);
    ind.draw({0, 0, false, 0, 0});                       // stopped: no arrow
    ind.tick(1000);
    ind.draw({270, 0, true, 30, 0});                     // flick left
    ind.tick(1050);
    EXPECT_NEAR(ind.currentAngle(), 270.0, 0.01);
    ind.draw({180, 0, true, 30, 0});                     // then down: eased, not jumped
    ind.tick(1100);
    EXPECT_TRUE(ind.currentAngle() < 270.0f && ind.currentAngle() > 180.0f);
}
