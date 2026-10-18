/**
 * @file test_screen_trace.cpp
 * @brief The Trace screen rendered through the real L4 driver into a simulated panel.
 *
 * Every view (list live/paused/scrolled/filtered, legend, timeline, services) is dumped
 * under screens/ in the build directory (scripts/lcd/snapshots_to_png.py converts them).
 * Assertions: the screen draws only inside the content band (outside it the panel matches a
 * chrome-only screen with the same title and footer), it uses the theme palette and no stock
 * colours, and an unchanged frame sends (almost) nothing.
 */
#include "minitest.hpp"
#include "fakes/fake_board.hpp"
#include "fakes/panel_sim.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include "L3_services/ui/menu_screen/menu_screen.hpp"
#include "L3_services/ui/screens/trace_screen.hpp"
#include "L3_services/ui/screen_manager/screen_manager.hpp"
#include "L3_services/ui/theme.hpp"
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

// The ring served by stubs/trace_screen_stub.cpp
namespace TraceStub {
void push(size_t n);
void clear();
}

using namespace fakes;
using UI::JoyDirection;
namespace Theme = UI::Theme;

namespace {

/// A panel plus the real driver, the way the Display task sees it
struct Rig {
    FakeGPIO cs, dc, nreset, te;
    PanelSim panel;
    Drivers::LCD lcd;
    UI::ScreenManager mgr;
    UI::MenuScreen root{"Main Menu"};
    Rig() : panel(dc), lcd(panel, cs, dc, nreset, te) {
        MKDIR("screens");
        UI::StatusLine s = {};
        snprintf(s.left, sizeof s.left, "RUN 150rpm"); s.leftColor = Theme::GREEN;
        snprintf(s.mid, sizeof s.mid, "CM 1/8");        s.midColor = Theme::VIOLET;
        snprintf(s.right, sizeof s.right, "FL");        s.rightColor = Theme::YELLOW;
        mgr.setStatus(s);
        root.addItem("Trace Monitor");
        mgr.init(&root, &lcd);
        mgr.render();
    }
    void push(UI::IScreen* s) { mgr.push(s); }
    void frame() { mgr.render(); }
    void key(JoyDirection d) { mgr.handleInput(d, true); }
    void dump(const char* name) {
        char path[64]; snprintf(path, sizeof path, "screens/%s.ppm", name);
        panel.writePpm(path);
    }
    uint32_t inBand(uint16_t color) const {
        return panel.countColor(color, 0, Theme::CONTENT_Y, 240,
                                static_cast<uint16_t>(Theme::CONTENT_BOTTOM - Theme::CONTENT_Y + 1));
    }
};

/// A chrome screen that draws nothing: the reference for everything outside the band
struct ChromeOnly : UI::IScreen {
    const char* t;
    const char* h;
    ChromeOnly(const char* title, const char* hint) : t(title), h(hint) {}
    UI::ScreenType getType() const override { return UI::ScreenType::TERMINAL; }
    void render(Services::ICanvas&) override {}
    UI::InputResult handleInput(JoyDirection, bool) override { return UI::InputResult::HANDLED; }
    bool usesChrome() const override { return true; }
    const char* title() const override { return t; }
    const char* centerHint() const override { return h; }
};

/// Pixels outside the content band that differ from a chrome-only screen
uint32_t strayPixels(const Rig& rig, const char* title, const char* hint)
{
    static Rig ref;     // big frame buffer: keep it off the stack
    static ChromeOnly screen("", "");
    screen.t = title;
    screen.h = hint;
    ref.mgr.popToRoot();
    ref.push(&screen);
    ref.frame();
    uint32_t n = 0;
    for (uint16_t y = 0; y < PanelSim::H; y++) {
        if (y >= Theme::CONTENT_Y && y <= Theme::CONTENT_BOTTOM) continue;
        for (uint16_t x = 0; x < PanelSim::W; x++) {
            if (rig.panel.px(x, y) != ref.panel.px(x, y)) n++;
        }
    }
    return n;
}

/// Stock (non-theme) colours the old screen used; none may appear
uint32_t stockPixels(const Rig& rig)
{
    static constexpr uint16_t STOCK[] = { 0xFFFF, 0x07FF, 0xF800, 0x07E0, 0xF81F, 0xFFE0,
                                          0xFD20, 0x001F, 0x4208, 0x8410 };
    uint32_t n = 0;
    for (uint16_t c : STOCK) n += rig.inBand(c);
    return n;
}

/// Cost of one frame in pixels sent
uint32_t frameCost(Rig& rig)
{
    uint32_t before = rig.panel.pixelsWritten;
    rig.frame();
    return rig.panel.pixelsWritten - before;
}

} // namespace

TEST(trace_list_live_paused_and_scrolled) {
    TraceStub::clear();
    TraceStub::push(300);                       // ring full and wrapped
    static Rig rig;
    UI::TraceScreen trace;
    rig.push(&trace);
    rig.frame();
    rig.dump("trace_live");
    EXPECT_EQ(strayPixels(rig, "Trace", "PAUSE"), 0u);
    EXPECT_EQ(stockPixels(rig), 0u);
    EXPECT_EQ(rig.inBand(Theme::BLACK), 0u);              // text view: no black plot
    EXPECT_TRUE(rig.inBand(Theme::GREEN) > 20);           // LIVE + L2 names
    EXPECT_TRUE(rig.inBand(Theme::TEXT) > 1000);          // the lines
    EXPECT_TRUE(rig.inBand(Theme::ORANGE) > 100);         // L3 names
    EXPECT_TRUE(rig.panel.countColor(Theme::CYAN, 230, 62, 4, 220) > 10);   // live scrollbar thumb
    EXPECT_TRUE(rig.inBand(Theme::DIM) > 100);            // indices, hint

    // Nothing new: nothing is sent
    uint32_t idle = frameCost(rig);
    printf("  trace list: idle frame %u px\n", idle);
    EXPECT_EQ(idle, 0u);

    // One new entry while live: the list scrolls by a line; the header keeps its text
    TraceStub::push(1);
    uint32_t scroll = frameCost(rig);
    printf("  trace list: one new entry %u px\n", scroll);
    EXPECT_TRUE(scroll <= 20u * 222u * 11u);

    // Pause: footer says RESUME, new entries only touch the header line
    rig.key(JoyDirection::CENTER);
    rig.frame();
    EXPECT_EQ(strcmp(trace.centerHint(), "RESUME"), 0);
    TraceStub::push(5);
    uint32_t paused = frameCost(rig);
    printf("  trace list: 5 new entries while paused %u px\n", paused);
    EXPECT_TRUE(paused > 0 && paused <= 228u * 10u);
    rig.dump("trace_paused");
    EXPECT_EQ(strayPixels(rig, "Trace", "RESUME"), 0u);
    EXPECT_TRUE(rig.panel.countColor(Theme::YELLOW, 0, Theme::CONTENT_Y, 240, 14) > 20);   // PAUSED +5

    // Scroll back: the scrollbar thumb (magenta while paused) leaves the bottom
    for (int i = 0; i < 6; i++) rig.key(JoyDirection::UP);
    rig.frame();
    rig.dump("trace_scrolled");
    EXPECT_TRUE(rig.panel.countColor(Theme::MAGENTA, 230, 62, 4, 220) > 20);
    EXPECT_EQ(rig.panel.countColor(Theme::MAGENTA, 230, 262, 4, 20), 0u);   // not at the bottom any more
    EXPECT_EQ(frameCost(rig), 0u);

    // Resume and leave
    rig.key(JoyDirection::CENTER);
    rig.frame();
    EXPECT_EQ(strcmp(trace.centerHint(), "PAUSE"), 0);
    rig.key(JoyDirection::LEFT);
    EXPECT_EQ(rig.mgr.depth(), 1);
}

TEST(trace_legend_toggles_filters_and_the_list_follows) {
    TraceStub::clear();
    TraceStub::push(300);
    static Rig rig;
    UI::TraceScreen trace;
    rig.mgr.popToRoot();
    rig.push(&trace);
    rig.frame();
    rig.key(JoyDirection::RIGHT);
    rig.frame();
    rig.dump("trace_legend");
    EXPECT_EQ(strcmp(trace.title(), "Trace Legend"), 0);
    EXPECT_EQ(strayPixels(rig, "Trace Legend", "TOGGLE"), 0u);
    EXPECT_EQ(stockPixels(rig), 0u);
    // Selection bar on the first row, every enabled row says ON in green
    EXPECT_TRUE(rig.panel.countColor(Theme::MAGENTA, 0, 60, 240, 14) > 2000);
    EXPECT_TRUE(rig.inBand(Theme::GREEN) > 200);
    EXPECT_EQ(frameCost(rig), 0u);

    // Moving the cursor repaints two rows
    rig.key(JoyDirection::DOWN);
    uint32_t move = frameCost(rig);
    printf("  legend: one cursor move %u px\n", move);
    EXPECT_TRUE(move > 0 && move <= 2u * 231u * 15u);
    EXPECT_TRUE(rig.panel.countColor(Theme::MAGENTA, 0, 60, 240, 14) < 200);   // bar left row 0

    // Toggle LCD (bit 0x100, row 6): its row must say OFF once the cursor moves on
    for (int i = 0; i < 5; i++) rig.key(JoyDirection::DOWN);
    rig.frame();
    const uint16_t row6 = 60 + 6 * 15;
    const uint32_t greenBefore = rig.panel.countColor(Theme::GREEN, 0, row6, 240, 14);
    rig.key(JoyDirection::CENTER);
    rig.key(JoyDirection::DOWN);
    rig.frame();
    rig.dump("trace_legend_lcd_off");
    EXPECT_EQ(greenBefore, 0u);                        // selected row: text on magenta
    EXPECT_EQ(rig.panel.countColor(Theme::GREEN, 0, row6, 240, 14), 0u);    // OFF, not ON
    EXPECT_TRUE(rig.panel.countColor(Theme::DIM, 0, row6, 240, 14) > 50);

    // Filter down to Mot only: toggle every other boundary off from the top
    rig.key(JoyDirection::CENTER);                     // row 7 (Fls) off
    for (int i = 0; i < 7; i++) rig.key(JoyDirection::UP);
    rig.key(JoyDirection::CENTER); rig.key(JoyDirection::DOWN);   // Xpt
    rig.key(JoyDirection::CENTER); rig.key(JoyDirection::DOWN);   // Pro
    rig.key(JoyDirection::CENTER); rig.key(JoyDirection::DOWN);   // Tlm
    rig.key(JoyDirection::CENTER); rig.key(JoyDirection::DOWN);   // Cmd
    rig.key(JoyDirection::DOWN);                                  // keep Mot
    rig.key(JoyDirection::CENTER); rig.key(JoyDirection::DOWN);   // Enc
    rig.key(JoyDirection::DOWN); rig.key(JoyDirection::DOWN);     // LCD, Fls already off
    rig.key(JoyDirection::CENTER); rig.key(JoyDirection::DOWN);   // SPI
    rig.key(JoyDirection::CENTER); rig.key(JoyDirection::DOWN);   // Saf
    rig.key(JoyDirection::CENTER);                                // Mtx
    rig.frame();
    rig.dump("trace_legend_mot_only");
    rig.key(JoyDirection::LEFT);
    rig.frame();
    rig.dump("trace_filtered");
    // The header names the filter in magenta; every listed line is a motor call (L4 magenta)
    EXPECT_TRUE(rig.panel.countColor(Theme::MAGENTA, 160, Theme::CONTENT_Y, 74, 14) > 20);
    EXPECT_TRUE(rig.inBand(Theme::MAGENTA) > 20 * 20);
    EXPECT_EQ(rig.panel.countColor(Theme::GREEN, 0, 62, 240, 220), 0u);   // no L2 names in the list
}

TEST(trace_timeline_and_services_views) {
    TraceStub::clear();
    TraceStub::push(300);
    static Rig rig;
    UI::TraceScreen trace;
    rig.mgr.popToRoot();
    rig.push(&trace);
    rig.frame();
    rig.key(JoyDirection::RIGHT);
    rig.frame();
    rig.key(JoyDirection::RIGHT);
    rig.frame();
    rig.dump("trace_timeline");
    EXPECT_EQ(strcmp(trace.title(), "Trace Timeline"), 0);
    EXPECT_EQ(strayPixels(rig, "Trace Timeline", "PAUSE"), 0u);
    EXPECT_EQ(stockPixels(rig), 0u);
    EXPECT_TRUE(rig.inBand(Theme::BLACK) > 6 * 190);       // lane gaps
    EXPECT_TRUE(rig.inBand(Theme::CYAN) > 300);            // L1 blocks, label, brackets
    EXPECT_TRUE(rig.inBand(Theme::MAGENTA) > 300);         // L4 motor blocks
    EXPECT_TRUE(rig.inBand(Theme::VIOLET) > 300);          // L5 SPI blocks
    EXPECT_EQ(frameCost(rig), 0u);

    // Live: new entries repaint at most every 4th frame, block by block
    uint32_t live = 0;
    for (int i = 0; i < 4; i++) { TraceStub::push(1); live += frameCost(rig); }
    printf("  timeline: 4 live frames, one entry each %u px\n", live);
    EXPECT_TRUE(live > 0 && live < 196u * 198u);

    // Zoom in, pause, pan back
    rig.key(JoyDirection::UP); rig.key(JoyDirection::UP); rig.key(JoyDirection::UP);
    rig.frame();
    rig.dump("trace_timeline_zoom");
    rig.key(JoyDirection::CENTER);
    rig.key(JoyDirection::LEFT);
    rig.frame();
    rig.dump("trace_timeline_paused");
    EXPECT_EQ(strayPixels(rig, "Trace Timeline", "RESUME"), 0u);
    TraceStub::push(3);
    uint32_t frozen = frameCost(rig);
    EXPECT_TRUE(frozen <= 228u * 10u);                     // header only
    rig.key(JoyDirection::CENTER);                         // resume
    rig.frame();

    // Services view
    rig.key(JoyDirection::RIGHT);
    rig.frame();
    rig.dump("trace_services");
    EXPECT_EQ(strcmp(trace.title(), "Trace Services"), 0);
    EXPECT_EQ(strayPixels(rig, "Trace Services", "PAUSE"), 0u);
    EXPECT_EQ(stockPixels(rig), 0u);
    EXPECT_TRUE(rig.inBand(Theme::BLACK) > 8 * 190);
    EXPECT_EQ(frameCost(rig), 0u);
    rig.key(JoyDirection::DOWN);                           // already auto: no change
    rig.key(JoyDirection::UP); rig.key(JoyDirection::UP);
    rig.frame();
    rig.dump("trace_services_zoom");
    rig.key(JoyDirection::LEFT);
    rig.frame();
    EXPECT_EQ(strcmp(trace.title(), "Trace Timeline"), 0);
}


