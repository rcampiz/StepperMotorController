/**
 * @file test_screen_archmeter.cpp
 * @brief The Architecture screen's harness meter views (HARNESS, MATRIX) rendered through the
 *        real L4 driver into a simulated panel, fed straight from Harness::Meter.
 *
 * The meter is counted directly (Meter::count with a task source) and timed by a fake
 * microsecond clock the test advances, so the rates on screen are exact. Checks the theme
 * colours, that nothing is drawn outside the content band, that a frame inside the sample
 * period (or a sample with the same rates) sends next to nothing, and dumps
 * screens/arch_harness*.ppm and screens/arch_matrix_meter.ppm (scripts/lcd/snapshots_to_png.py).
 */
#include "minitest.hpp"
#include "fakes/fake_board.hpp"
#include "fakes/panel_sim.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include "L3_services/ui/menu_screen/menu_screen.hpp"
#include "L3_services/ui/screens/arch_screen.hpp"
#include "L3_services/ui/screen_manager/screen_manager.hpp"
#include "L3_services/infra/trace/trace.hpp"
#include "L3_services/ui/theme.hpp"
#include "harness/trace/harness_meter.hpp"
#include "F_platform/boundary/imicrosecond_clock.hpp"
#include <sys/stat.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

// FLOW and TASKS read the trace ring; the host build has none, so it is empty
namespace Trace {
size_t getTotal() { return 0; }
size_t getCount() { return 0; }
bool getEntry(size_t, Entry&) { return false; }
}

using namespace fakes;
namespace Meter = Harness::Meter;

namespace {

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

/// The meter's clock, advanced by hand
struct FakeUsClock : Platform::IMicrosecondClock {
    uint32_t now = 10000000u;
    uint32_t getTickUs() const override { return now; }
};
FakeUsClock g_us;

uint8_t g_task = 0;
uint8_t currentTask() { return g_task; }

void wireMeter() {
    Meter::setClock(&g_us);
    Meter::setTaskSource(&currentTask);
}

/// n crossings of `net` by `task`, each carrying `bytes` and spending `busyUs` inside
void cross(Meter::Net net, uint8_t task, uint32_t n, uint32_t bytes = 0, uint32_t busyUs = 0) {
    g_task = task;
    for (uint32_t i = 0; i < n; i++) Meter::count(net, bytes, busyUs);
    g_task = 0;
}

enum : uint8_t { T_NONE = 0, T_MOTOR = 1, T_ENC = 2, T_COMMS = 3, T_DISP = 4, T_TIMER = 5 };

/// Half a second of a running board (roughly the 2026-10-17 bench numbers): the display
/// streaming the canvas, the encoder polling, the motor task stepping, comms answering
void halfSecond(uint32_t canvasScale = 1) {
    cross(Meter::TASKS_MOTOR,      T_MOTOR, 10, 0, 600);
    cross(Meter::TASKS_COMMS,      T_COMMS, 48, 0, 40);
    cross(Meter::TASKS_ENCODER,    T_ENC,  500, 0, 15);
    cross(Meter::TASKS_DISPLAY,    T_DISP,  10, 0, 20000);
    cross(Meter::L1_L2_TRANSPORT,  T_COMMS, 48, 22, 30);
    cross(Meter::L2_L3_COMMANDS,   T_COMMS,  5);
    cross(Meter::L3_L2_PROCESSOR,  T_COMMS, 48, 0, 25);
    cross(Meter::L3_L2_PROCESSOR,  T_MOTOR, 10, 0, 5);
    cross(Meter::L3_L4_MOTOR,      T_MOTOR, 38, 0, 45);
    cross(Meter::L3_L4_MOTOR,      T_COMMS,  2, 0, 45);
    cross(Meter::L3_L4_CANVAS,     T_DISP,   7 * canvasScale, 21000, 15000);
    cross(Meter::L3_L4_ENCODER,    T_ENC,  500, 0, 4);
    cross(Meter::L4_L5_SPI1_MOTOR, T_MOTOR, 40, 4, 30);
    cross(Meter::L4_L5_SPI1_LCD,   T_DISP, 330, 470, 600);
    cross(Meter::L5_L1_BYTES,      T_NONE,  24, 30);
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

/// The rows outside the content band (chrome), for a before/after comparison
struct Outside {
    uint16_t rows[320][240];
    void take(const PanelSim& p) { memcpy(rows, p.frame, sizeof rows); }
    bool same(const PanelSim& p) const {
        for (uint16_t y = 0; y < 320; y++) {
            if (y >= UI::Theme::CONTENT_Y && y <= UI::Theme::CONTENT_BOTTOM) continue;
            if (memcmp(rows[y], p.frame[y], sizeof rows[y]) != 0) return false;
        }
        return true;
    }
};
Outside g_outside;

void toView(Rig& rig, int rights) {
    for (int i = 0; i < rights; i++) rig.key(UI::JoyDirection::RIGHT);
    rig.frame();
}

} // namespace

TEST(harness_view_shows_live_rates_per_signal_and_repaints_only_changes) {
    wireMeter();
    Rig rig;
    UI::MenuScreen root("Main Menu");
    UI::ArchScreen arch;
    rig.start(&root);
    rig.mgr.push(&arch);
    rig.frame();                                         // diagram
    toView(rig, 4);                                      // matrix, flow, tasks, harness
    rig.dump("arch_harness_empty");
    g_outside.take(rig.panel);
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::CYAN) > 0);   // "HARNESS" head

    // Half a second of traffic: the sample is due, every row shows its rates
    halfSecond();
    g_us.now += UI::ArchScreen::SAMPLE_US;
    const uint32_t before = rig.panel.pixelsWritten;
    rig.frame();
    rig.dump("arch_harness");
    const uint32_t sampleCost = rig.panel.pixelsWritten - before;
    printf("  harness: one sample %u px\n", sampleCost);
    EXPECT_TRUE(sampleCost > 0u);
    EXPECT_TRUE(sampleCost < 240u * 265u / 2u);          // cells only, never the whole band
    EXPECT_TRUE(g_outside.same(rig.panel));              // the chrome is untouched
    // Theme colours: harness heads, ticks and bars; task letters; values
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::YELLOW) > 40);    // H_TASKS_L3, the D letter
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::ORANGE) > 40);    // H_L3_L4 bars (canvas ~21 %)
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::MAGENTA) > 40);   // H_L4_L5 (SPI1_LCD ~40 %)
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::GREEN) > 10);     // H_L2_L3, the C letter
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::RED) > 5);        // the M letter
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::VIOLET) > 40);    // X_L5_L1, bytes/s
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::TEXT) > 200);     // signal names, calls/s
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::PANEL) > 15u * 20u);   // bar tracks

    // Frames inside the sample period send nothing
    const uint32_t idle0 = rig.panel.pixelsWritten;
    for (int i = 0; i < 5; i++) { halfSecond(); g_us.now += 50000; rig.frame(); }
    EXPECT_EQ(rig.panel.pixelsWritten - idle0, 0u);
    g_us.now += UI::ArchScreen::SAMPLE_US - 5u * 50000u;  // the period ends
    // (the five half-seconds counted above land in this sample: rates x5, cells change)
    rig.frame();
    rig.dump("arch_harness_burst");
    // Same traffic again: same rates, nothing to repaint
    halfSecond(); g_us.now += UI::ArchScreen::SAMPLE_US; rig.frame();
    const uint32_t same0 = rig.panel.pixelsWritten;
    halfSecond(); g_us.now += UI::ArchScreen::SAMPLE_US; rig.frame();
    const uint32_t sameCost = rig.panel.pixelsWritten - same0;
    printf("  harness: a sample with the same rates %u px\n", sameCost);
    EXPECT_TRUE(sameCost < 200u);
    // One signal changes: only its row's cells go out
    halfSecond(2); g_us.now += UI::ArchScreen::SAMPLE_US;
    const uint32_t one0 = rig.panel.pixelsWritten;
    rig.frame();
    const uint32_t oneCost = rig.panel.pixelsWritten - one0;
    printf("  harness: canvas doubled %u px\n", oneCost);
    EXPECT_TRUE(oneCost > 0u);
    EXPECT_TRUE(oneCost < 240u * 11u);         // the canvas row's cells, nothing else
    EXPECT_TRUE(g_outside.same(rig.panel));

    // Paused: samples stop, the head says so
    rig.key(UI::JoyDirection::CENTER);
    rig.frame();
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::YELLOW) > 40);
    halfSecond(4); g_us.now += UI::ArchScreen::SAMPLE_US;
    const uint32_t paused0 = rig.panel.pixelsWritten;
    rig.frame();
    EXPECT_EQ(rig.panel.pixelsWritten - paused0, 0u);
    rig.dump("arch_harness_paused");
    rig.key(UI::JoyDirection::CENTER);
    rig.frame();

    // RIGHT wraps to the diagram, LEFT leaves
    rig.key(UI::JoyDirection::RIGHT);
    rig.frame();
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::PANEL, 0, UI::Theme::CONTENT_Y, 240, 240) > 6 * 200 * 30);
    rig.key(UI::JoyDirection::LEFT);
    EXPECT_EQ(rig.mgr.depth(), 1);
}

TEST(matrix_view_shows_harness_by_task_rates_from_the_meter) {
    wireMeter();
    Rig rig;
    UI::MenuScreen root("Main Menu");
    UI::ArchScreen arch;
    rig.start(&root);
    rig.mgr.push(&arch);
    rig.frame();
    toView(rig, 1);                                      // matrix
    g_outside.take(rig.panel);
    halfSecond();
    g_us.now += UI::ArchScreen::SAMPLE_US;
    rig.frame();
    rig.dump("arch_matrix_meter");
    EXPECT_TRUE(g_outside.same(rig.panel));
    // Full-brightness cells: L3L4 x Encoder (1000/s, orange), L4L5 x Display (660/s, magenta),
    // T>L3 x Encoder (1000/s, yellow); L1L2 x Comms 96/s is medium, L3L4 x Comms 4/s dim
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::ORANGE) > 36u * 26u);
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::MAGENTA) > 36u * 26u);
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::YELLOW) > 36u * 26u);
    // Unchanged traffic: no cell changes, nothing sent
    halfSecond(); g_us.now += UI::ArchScreen::SAMPLE_US; rig.frame();
    const uint32_t same0 = rig.panel.pixelsWritten;
    halfSecond(); g_us.now += UI::ArchScreen::SAMPLE_US; rig.frame();
    EXPECT_EQ(rig.panel.pixelsWritten - same0, 0u);
    // One rate changes: one cell (38 x 28) goes out
    halfSecond(2); g_us.now += UI::ArchScreen::SAMPLE_US;
    const uint32_t one0 = rig.panel.pixelsWritten;
    rig.frame();
    const uint32_t oneCost = rig.panel.pixelsWritten - one0;
    printf("  matrix: one cell changed %u px\n", oneCost);
    EXPECT_TRUE(oneCost > 0u && oneCost <= 2u * 38u * 28u);
}

TEST(meter_views_draw_only_inside_the_content_band) {
    wireMeter();
    Rig rig;
    UI::ArchScreen arch;
    arch.onActivate();
    rig.lcd.fillScreen(SENTINEL);
    // Every view, drawn straight to the panel without the manager's chrome
    for (int view = 0; view < 5; view++) {                   // diagram .. harness
        arch.render(rig.lcd);
        halfSecond(); g_us.now += UI::ArchScreen::SAMPLE_US;
        arch.render(rig.lcd);
        EXPECT_TRUE(outsideUntouched(rig.panel));
        if (view != 0) {                                        // paused, then resumed
            arch.handleInput(UI::JoyDirection::CENTER, true);
            arch.render(rig.lcd);
            EXPECT_TRUE(outsideUntouched(rig.panel));
            arch.handleInput(UI::JoyDirection::CENTER, true);
        }
        arch.handleInput(UI::JoyDirection::RIGHT, true);
    }
    EXPECT_TRUE(inBand(rig.panel, SENTINEL) < 240u * 265u);
}
