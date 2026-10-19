/**
 * @file test_screen_memory.cpp
 * @brief The Memory screen (flash and RAM gauges, RAM split, stack peaks) rendered through the
 *        real L4 driver into a simulated panel, under the chrome, on fake Platform memory and
 *        task statistics.
 *
 * Checks that it draws only inside the content band, in theme colours, with the headroom
 * colours where they belong, that it samples once a second and an unchanged sample sends
 * nothing, and dumps the views to screens/ (scripts/lcd/snapshots_to_png.py converts them).
 */
#include "minitest.hpp"
#include "fakes/fake_board.hpp"
#include "fakes/panel_sim.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include "L3_services/ui/menu_screen/menu_screen.hpp"
#include "L3_services/ui/screens/memory_screen.hpp"
#include "L3_services/ui/screen_manager/screen_manager.hpp"
#include "L3_services/ui/theme.hpp"
#include "F_platform/boundary/imemory_stats.hpp"
#include "F_platform/boundary/itask_stats.hpp"
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

using namespace fakes;

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
    void second() { for (uint16_t i = 0; i < UI::MemoryScreen::FRAMES_PER_SAMPLE; i++) frame(); }
    void key(UI::JoyDirection d) { mgr.handleInput(d, true); }
    void dump(const char* name) {
        char path[64]; snprintf(path, sizeof path, "screens/%s.ppm", name);
        panel.writePpm(path);
    }
};

/// The board's memory map as the release build reports it (arm-none-eabi-size: text 279852,
/// data 1948, bss 82024; a 4096-word stack pool, 3968 words handed out; a 1 KB MSP reserve)
struct FakeMemoryStats : Platform::IMemoryStats {
    Platform::MemoryStats m = {};
    int calls = 0;
    bool getMemoryStats(Platform::MemoryStats& out) override { calls++; out = m; return true; }
    void reset() {
        m.flashUsed = 279852 + 1948; m.flashTotal = 512 * 1024;
        m.ramStatic = 1948 + 82024;  m.ramTotal = 96 * 1024;
        m.stackPoolUsed = 3968 * 4;  m.stackPoolTotal = 4096 * 4;
        m.mainStackUsed = 612;       m.mainStackTotal = 1024;
    }
};

/// The scheduler: sizes and high-water marks (words) that cover every headroom colour
struct FakeTaskStats : Platform::ITaskStats {
    Platform::TaskStatsSnapshot snap = {};
    void add(const char* name, uint16_t words, uint16_t hwm) {
        Platform::TaskStat& t = snap.tasks[snap.count++];
        snprintf(t.name, sizeof t.name, "%s", name);
        t.stackWords = words; t.stackHWM = hwm; t.state = 2; t.priority = 1;
    }
    void reset() {
        snap = {};
        add("Motor",   1024, 380);    // 63 % green
        add("IDLE",     128,  90);    // 30 % green
        add("Comms",   2048, 1100);   // 46 % green
        add("Display",  640,  120);   // 81 % yellow
        add("Encoder",  256,   18);   // 93 % red
    }
    void getSnapshot(Platform::TaskStatsSnapshot& out) override { out = snap; }
    uint8_t getCurrentTaskIndex() override { return 0; }
};

FakeMemoryStats g_mem;
FakeTaskStats g_tasks;

void wire()
{
    g_mem.reset();
    g_tasks.reset();
    Platform::setMemoryStatsProvider(&g_mem);
    Platform::setTaskStatsProvider(&g_tasks);
}

constexpr uint16_t SENTINEL = 0x1234;   // not a theme colour

bool outsideUntouched(const PanelSim& p) {
    const uint32_t above = 240u * UI::Theme::CONTENT_Y;
    const uint32_t below = 240u * (320u - UI::Theme::CONTENT_BOTTOM - 1u);
    return p.countColor(SENTINEL, 0, 0, 240, UI::Theme::CONTENT_Y) == above &&
           p.countColor(SENTINEL, 0, UI::Theme::CONTENT_BOTTOM + 1, 240,
                        static_cast<uint16_t>(320 - UI::Theme::CONTENT_BOTTOM - 1)) == below;
}

uint32_t inBand(const PanelSim& p, uint16_t color) {
    return p.countColor(color, 0, UI::Theme::CONTENT_Y, 240,
                        static_cast<uint16_t>(UI::Theme::CONTENT_BOTTOM - UI::Theme::CONTENT_Y + 1));
}

// Gauge bars (memory_screen.cpp layout): flash 64..75, RAM 112..123, x 8..230
uint32_t inFlashBar(const PanelSim& p, uint16_t c) { return p.countColor(c, 8, 64, 223, 12); }
uint32_t inRamBar(const PanelSim& p, uint16_t c) { return p.countColor(c, 8, 112, 223, 12); }
// Stack table rows: 182 + 15 * i, 13 rows tall
uint32_t inRow(const PanelSim& p, int i, uint16_t c) {
    return p.countColor(c, 0, static_cast<uint16_t>(182 + 15 * i), 240, 13);
}

} // namespace

TEST(memory_page_shows_gauges_split_and_stack_peaks_under_the_chrome) {
    wire();
    Rig rig;
    UI::MenuScreen root("Main Menu");
    UI::MemoryScreen mem;
    rig.start(&root);
    rig.mgr.push(&mem);
    rig.frame();
    rig.dump("memory");
    EXPECT_TRUE(strcmp(mem.title(), "Memory") == 0);
    EXPECT_TRUE(mem.usesChrome());
    EXPECT_TRUE(mem.centerHint() == nullptr);
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::CYAN, 0, 0, 240, 26) > 500);               // the title tab
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::BG) > 25000);                                // themed page
    EXPECT_EQ(inBand(rig.panel, 0x0000), 0u);                                             // no black page
    EXPECT_EQ(inBand(rig.panel, 0xFFFF), 0u);                                             // no stock white
    // Flash 54 %: green segments lit, no yellow or red ones lit
    EXPECT_TRUE(inFlashBar(rig.panel, UI::Theme::GREEN) > 13u * 7u * 8u);
    EXPECT_EQ(inFlashBar(rig.panel, UI::Theme::YELLOW), 0u);
    EXPECT_EQ(inFlashBar(rig.panel, UI::Theme::RED), 0u);
    // RAM 85 %: green and yellow lit, red not
    EXPECT_TRUE(inRamBar(rig.panel, UI::Theme::GREEN) > 17u * 7u * 8u);
    EXPECT_TRUE(inRamBar(rig.panel, UI::Theme::YELLOW) > 3u * 7u * 8u);
    EXPECT_EQ(inRamBar(rig.panel, UI::Theme::RED), 0u);
    // The RAM percentage in large yellow type, the flash one green
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::YELLOW, 186, 92, 48, 16) > 60);
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::GREEN, 186, 44, 48, 16) > 60);
    // The split: stacks magenta, globals cyan
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::MAGENTA, 8, 143, 223, 8) > 30u * 8u);
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::CYAN, 8, 143, 223, 8) > 140u * 8u);
    // Stack rows: ISR first, then by size: Comms, Motor, Display (yellow), Encoder (red), IDLE
    EXPECT_TRUE(inRow(rig.panel, 0, UI::Theme::GREEN) > 50);     // ISR 60 %
    EXPECT_TRUE(inRow(rig.panel, 3, UI::Theme::YELLOW) > 50);    // Display 81 %
    EXPECT_EQ(inRow(rig.panel, 3, UI::Theme::RED), 0u);
    EXPECT_TRUE(inRow(rig.panel, 4, UI::Theme::RED) > 50);       // Encoder 93 %
    EXPECT_TRUE(inRow(rig.panel, 5, UI::Theme::TEXT) > 50);      // IDLE's name
    EXPECT_EQ(inRow(rig.panel, 5, UI::Theme::RED), 0u);

    // Sampled once a second: no figure changed, nothing is sent; nor between samples
    const uint32_t first = rig.panel.pixelsWritten;
    const int calls = g_mem.calls;
    rig.second();
    rig.second();
    EXPECT_EQ(rig.panel.pixelsWritten, first);
    EXPECT_TRUE(g_mem.calls - calls <= 2 * 2);                  // gauges + ISR row, per sample
    // One task goes deeper: only its row's changed parts
    g_tasks.snap.tasks[0].stackHWM = 100;                        // Motor 63 % -> 90 % (yellow)
    rig.second();
    const uint32_t cost = rig.panel.pixelsWritten - first;
    printf("  memory: first frame %u px, one stack peak change %u px\n", first, cost);
    EXPECT_TRUE(cost > 0 && cost <= 240u * 15u);
    EXPECT_TRUE(inRow(rig.panel, 2, UI::Theme::YELLOW) > 50);
    // RAM fills past 90 %: red segments light up, the percentage turns red
    g_mem.m.ramStatic = 92 * 1024;
    rig.second();
    rig.dump("memory_ram_full");
    EXPECT_TRUE(inRamBar(rig.panel, UI::Theme::RED) > 7u * 8u);
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::RED, 186, 92, 48, 16) > 60);
    // A task goes away: its row is blanked
    g_tasks.snap.count = 4;
    rig.second();
    EXPECT_EQ(inRow(rig.panel, 5, UI::Theme::TEXT), 0u);
    // LEFT goes back
    rig.key(UI::JoyDirection::LEFT);
    EXPECT_EQ(rig.mgr.depth(), 1);
}

TEST(memory_page_draws_only_inside_the_content_band) {
    wire();
    Rig rig;
    UI::MemoryScreen mem;
    mem.onActivate();
    rig.lcd.fillScreen(SENTINEL);
    mem.render(rig.lcd);
    EXPECT_TRUE(outsideUntouched(rig.panel));
    g_mem.m.ramStatic = g_mem.m.ramTotal;                        // everything at the extreme
    g_mem.m.mainStackUsed = 2048;                                // the MSP past its reserve
    for (int i = 0; i < 20; i++) mem.render(rig.lcd);
    EXPECT_TRUE(outsideUntouched(rig.panel));
    EXPECT_TRUE(inBand(rig.panel, SENTINEL) < 240u * 265u);
}

TEST(memory_page_without_statistics_says_so) {
    Platform::setMemoryStatsProvider(nullptr);
    Rig rig;
    UI::MenuScreen root("Main Menu");
    UI::MemoryScreen mem;
    rig.start(&root);
    rig.mgr.push(&mem);
    rig.frame();
    rig.dump("memory_none");
    EXPECT_TRUE(inBand(rig.panel, UI::Theme::TEXT) > 100);       // the message
    EXPECT_EQ(inBand(rig.panel, UI::Theme::GREEN), 0u);          // no gauges
    const uint32_t first = rig.panel.pixelsWritten;
    rig.second();
    EXPECT_EQ(rig.panel.pixelsWritten, first);
    wire();
}
