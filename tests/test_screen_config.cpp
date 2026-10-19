/**
 * @file test_screen_config.cpp
 * @brief The LCD Driver Config screen through the real L4 LCD driver into a simulated panel,
 *        over the real config manager holding the bench board's settings (a V4 blob migrated
 *        by the powerSTEP01's own translation).
 *
 * Driver boundary step 3: the screen shows and edits physical values; ranges and steps come
 * from the driver's caps. Dumps screens/config_*.ppm for a look (scripts/lcd/snapshots_to_png.py).
 */
#include "minitest.hpp"
#include "fakes/fake_board.hpp"
#include "fakes/panel_sim.hpp"
#include "fakes/powerstep_model.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include "L3_services/ui/screens/config_screen.hpp"
#include "L3_services/ui/screens/motion_screen.hpp"
#include "L3_services/ui/screen_manager/screen_manager.hpp"
#include "L3_services/ui/theme.hpp"
#include "L3_services/ui/chrome/chrome.hpp"
#include "L3_services/motion/motion.hpp"
#include "L3_services/infra/trace/trace.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include "F_platform/boundary/iclock.hpp"
#include "F_platform/boundary/ilock.hpp"
#include <cstring>
#include <vector>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

namespace Trace {
size_t getTotal() { return 0; }
size_t getCount() { return 0; }
bool getEntry(size_t, Entry&) { return false; }
// The motion service (behind the Motion page) traces its calls: nothing to record here
void record(Dir, const char*, uint32_t) {}
ServiceScope::ServiceScope(uint8_t) {}
ServiceScope::~ServiceScope() {}
}

using namespace fakes;

namespace {

// The Motion page reads the telemetry store: a host one, no locking needed
struct NullLock : Platform::ILock {
    void acquire() override {}
    void release() override {}
};
NullLock g_lock;
Platform::NullClock g_clock;
bool initStore() { static bool done = Services::g_telemetryStore.init(g_lock, g_clock); return done; }

struct RamStore : Drivers::IConfigStore {
    std::vector<uint8_t> blob = std::vector<uint8_t>(4096, 0xFF);
    size_t capacity() const override { return blob.size(); }
    size_t remaining() const override { return blob.size(); }
    bool erase() override { std::fill(blob.begin(), blob.end(), 0xFF); return true; }
    bool write(const uint8_t* d, size_t n) override { std::memcpy(blob.data(), d, n); return true; }
    size_t read(uint8_t* out, size_t n) const override { std::memcpy(out, blob.data(), n); return n; }
};

RamStore g_store;
PowerStepModel g_drv;

/// The bench board's saved V4 config: TVAL 04/11/14/14, current mode, 1/128 asked (1/16 runs),
/// ACC/DEC 344, MAX_SPEED 87, OCD_TH 21, idle release 3 s
void loadBenchConfig()
{
    Services::MotorConfigV4 v{};
    v.magic = Services::MOTOR_CONFIG_MAGIC;
    v.version = 4;
    v.driveMode = 1;
    v.tvalHold = 0x04; v.tvalRun = 0x11; v.tvalAcc = 0x14; v.tvalDec = 0x14;
    v.kvalHold = 0x20; v.kvalRun = 0x30; v.kvalAcc = 0x40; v.kvalDec = 0x40;
    v.ocdThreshold = 21; v.stallThreshold = 16;
    v.acceleration = 344; v.deceleration = 344; v.maxSpeed = 87; v.fsSpeed = 0x3FF;
    v.stepMode = 7;
    v.idleReleaseMs = 3000;
    v.crc32 = Services::motion.configStore.blobCrc(reinterpret_cast<const uint8_t*>(&v), sizeof(v) - sizeof(v.crc32));
    std::fill(g_store.blob.begin(), g_store.blob.end(), 0xFF);
    std::memcpy(g_store.blob.data(), &v, sizeof(v));
    Services::motion.configStore.init(g_store, &g_drv);
}

struct Rig {
    FakeGPIO cs, dc, nreset, te;
    PanelSim panel;
    Drivers::LCD lcd;
    UI::ScreenManager mgr;
    Rig() : panel(dc), lcd(panel, cs, dc, nreset, te) {
        MKDIR("screens");
        UI::StatusLine s = {};
        snprintf(s.left, sizeof s.left, "HOLD 0rpm");  s.leftColor = UI::Theme::CYAN;
        snprintf(s.mid, sizeof s.mid, "CM 1/16");       s.midColor = UI::Theme::VIOLET;
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

uint32_t outsideBand(const PanelSim& p, uint16_t color)
{
    return p.countColor(color, 0, 0, 240, UI::Theme::CONTENT_Y)
         + p.countColor(color, 0, UI::Theme::CONTENT_BOTTOM + 1, 240,
                        static_cast<uint16_t>(320 - UI::Theme::CONTENT_BOTTOM - 1));
}

constexpr uint16_t FIELDS_Y = UI::Theme::CONTENT_Y + 4;
uint16_t rowY(uint8_t i) { return static_cast<uint16_t>(FIELDS_Y + i * UI::Chrome::ROW_H); }
constexpr uint16_t HINT_Y = FIELDS_Y + 10 * UI::Chrome::ROW_H + 4;         // under the 10th row
constexpr uint16_t HELP_Y = HINT_Y + UI::Chrome::HELP_LINE_H;

} // namespace

TEST(config_screen_shows_the_bench_settings_in_physical_units) {
    loadBenchConfig();
    Rig rig;
    UI::ConfigScreen screen;
    rig.start(&screen);
    rig.frame();
    rig.dump("config_current");
    // The chrome and the shared selection (Hotline): row 0 selected in magenta, nothing else
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::CYAN, 0, 0, 240, 26) > 500);
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::MAGENTA, 0, rowY(0), 240, UI::Chrome::ROW_H) > 500);
    EXPECT_EQ(rig.panel.countColor(UI::Theme::MAGENTA, 0, rowY(1), 240, 8 * UI::Chrome::ROW_H), 0u);
    EXPECT_EQ(rig.panel.countColor(0xF800), 0u);                    // no stock red
    EXPECT_EQ(outsideBand(rig.panel, UI::Theme::BLACK), 0u);
    // Values: the cyan text of every unselected row is there
    for (uint8_t i = 1; i < 9; ++i) {
        EXPECT_TRUE(rig.panel.countColor(UI::Theme::CYAN, 120, rowY(i), 110, UI::Chrome::ROW_H) > 20);
    }
    const Harness::DriverSettings eff = Services::motion.stepper.config.getEffectiveSettings();
    EXPECT_EQ(eff.microsteps, 16u);                                   // "1/16": the driver's cap
    EXPECT_TRUE(eff.currentA.run > 1.32f && eff.currentA.run < 1.33f);

    // Layout: all ten rows show; the key hint and the help sit below the last (Save to flash)
    // and above the deck. The label column widens for the long labels: "Idle release" and
    // "Save to flash" show whole (their last letters lie past the 112 px column)
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::TEXT, UI::Theme::MARGIN, rowY(9), 112, UI::Chrome::ROW_H) > 100);
    EXPECT_EQ(rig.panel.countColor(UI::Theme::DIM, 0, rowY(9), 240, UI::Chrome::ROW_H), 0u);   // no hint over it
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::TEXT, UI::Theme::MARGIN + 112, rowY(8), 10, UI::Chrome::ROW_H) > 0);
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::TEXT, UI::Theme::MARGIN + 112, rowY(9), 18, UI::Chrome::ROW_H) > 0);
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::DIM, 0, HINT_Y, 240, 8) > 50);           // the key hint
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::DIM, 0, HELP_Y, 240, UI::Chrome::HELP_H) > 50);   // row 0's help
}

TEST(config_screen_explains_the_selected_row) {
    // Every row's help fits the two help lines, in either drive mode
    for (uint8_t f = 0; f < 16; ++f) {
        EXPECT_TRUE(UI::Chrome::helpFits(UI::ConfigScreen::helpText(f, true)));
        EXPECT_TRUE(UI::Chrome::helpFits(UI::ConfigScreen::helpText(f, false)));
    }
    EXPECT_TRUE(std::strcmp(UI::ConfigScreen::helpText(1, true), UI::ConfigScreen::helpText(1, false)) != 0);
    loadBenchConfig();
    Rig rig;
    UI::ConfigScreen screen;
    rig.start(&screen);
    rig.frame();
    auto helpInk = [&]() { return rig.panel.countColor(UI::Theme::DIM, 0, HELP_Y, 240, UI::Chrome::HELP_H); };
    const uint32_t stepInk = helpInk();
    for (int i = 0; i < 3; ++i) rig.key(UI::JoyDirection::DOWN);       // OCD
    rig.frame();
    rig.dump("config_help_ocd");
    const uint32_t ocdInk = helpInk();
    EXPECT_TRUE(ocdInk > 100 && ocdInk != stepInk);                    // two lines of it
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::DIM, 0, HELP_Y + UI::Chrome::HELP_LINE_H, 240, 8) > 30);
    // Same selection: not painted again (a pixel planted in it survives); editing keeps it
    // and shows the key hint on its own line
    rig.panel.frame[HELP_Y + 2][100] = 0xFFFF;
    rig.frame();
    rig.key(UI::JoyDirection::RIGHT);                                  // edit OCD
    rig.frame();
    rig.dump("config_help_ocd_edit");
    EXPECT_EQ(rig.panel.px(100, HELP_Y + 2), 0xFFFF);
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::DIM, 0, HINT_Y, 240, 8) > 50);           // "LEFT/RIGHT adjust ..."
    rig.key(UI::JoyDirection::CENTER);
    // Down to the last row: the help follows the selection
    for (int i = 0; i < 6; ++i) rig.key(UI::JoyDirection::DOWN);
    rig.frame();
    rig.dump("config_help_save");
    EXPECT_TRUE(rig.panel.px(100, HELP_Y + 2) != 0xFFFF);
    // The help never reaches the deck or the footer
    EXPECT_EQ(rig.panel.countColor(UI::Theme::DIM, 0, HELP_Y + UI::Chrome::HELP_H, 240,
                                   static_cast<uint16_t>(UI::Theme::CONTENT_BOTTOM - HELP_Y - UI::Chrome::HELP_H)), 0u);
}

TEST(config_screen_edits_move_by_the_drivers_own_steps) {
    loadBenchConfig();
    Rig rig;
    UI::ConfigScreen screen;
    rig.start(&screen);
    rig.frame();
    auto& sc = Services::motion.stepper.config;
    // OCD row (3): one press = one driver step (31.25 mV of VDS, ~26 A on this board)
    for (int i = 0; i < 3; ++i) rig.key(UI::JoyDirection::DOWN);
    rig.key(UI::JoyDirection::RIGHT);                                 // edit
    const float before = sc.getEffectiveSettings().overcurrentA;
    rig.key(UI::JoyDirection::RIGHT);                                 // +1 step
    rig.frame();
    rig.dump("config_edit_ocd");
    const float after = sc.getEffectiveSettings().overcurrentA;
    EXPECT_NEAR(after - before, sc.caps().overcurrentStepA, 1e-3);
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::CYAN, 0, rowY(3), 240, UI::Chrome::ROW_H) > 500);   // editing bar
    rig.key(UI::JoyDirection::CENTER);                                // done (the apply job just finds no Motor task)

    // Step mode (row 0): in current mode the driver offers 1/16 at most
    for (int i = 0; i < 3; ++i) rig.key(UI::JoyDirection::UP);
    rig.key(UI::JoyDirection::RIGHT);
    rig.key(UI::JoyDirection::RIGHT);                                 // already at 1/16: stays
    EXPECT_EQ(sc.getEffectiveMicrosteps(), 16u);
    rig.key(UI::JoyDirection::LEFT);                                  // 1/8
    EXPECT_EQ(sc.getMicrosteps(), 8u);
    rig.key(UI::JoyDirection::CENTER);

    // Run current (row 2): one TVAL-sized step, acc/dec kept 3 steps above
    rig.key(UI::JoyDirection::DOWN);
    rig.key(UI::JoyDirection::DOWN);
    rig.key(UI::JoyDirection::RIGHT);
    rig.key(UI::JoyDirection::RIGHT);
    const Harness::DriverSettings e = sc.getEffectiveSettings();
    EXPECT_NEAR(e.currentA.run, 18 * 0.078, 1e-4);
    EXPECT_NEAR(e.currentA.acc, 21 * 0.078, 1e-4);
    rig.key(UI::JoyDirection::CENTER);
}

TEST(config_screen_in_voltage_mode_shows_duty) {
    loadBenchConfig();
    Services::motion.stepper.config.setDriveMode(Harness::DriveMode::VOLTAGE);
    Rig rig;
    UI::ConfigScreen screen;
    rig.start(&screen);
    rig.frame();
    rig.dump("config_voltage");
    EXPECT_EQ(Services::motion.stepper.config.getEffectiveMicrosteps(), 128u);
    EXPECT_TRUE(rig.panel.countColor(UI::Theme::CYAN, 120, rowY(1), 110, UI::Chrome::ROW_H) > 20);
}

// The Motion page explains its selected row the same way (Chrome::HelpArea), under its key
// hint below the eight rows, repainted only when the selection changes
TEST(motion_screen_explains_the_selected_row) {
    for (uint8_t f = 0; f < 16; ++f) {
        EXPECT_TRUE(UI::Chrome::helpFits(UI::MotionScreen::helpText(f, true)));
        EXPECT_TRUE(UI::Chrome::helpFits(UI::MotionScreen::helpText(f, false)));
    }
    EXPECT_TRUE(std::strcmp(UI::MotionScreen::helpText(7, true), UI::MotionScreen::helpText(7, false)) != 0);
    EXPECT_TRUE(initStore());
    loadBenchConfig();
    Rig rig;
    UI::MotionScreen motion;
    rig.start(&motion);
    rig.frame();
    constexpr uint16_t ROWS_END = UI::Theme::CONTENT_Y + 40 + 8 * UI::Chrome::ROW_H;
    constexpr uint16_t MOTION_HELP_Y = ROWS_END + 4 + UI::Chrome::HELP_LINE_H;
    auto helpInk = [&]() { return rig.panel.countColor(UI::Theme::DIM, 0, MOTION_HELP_Y, 240, UI::Chrome::HELP_H); };
    const uint32_t speedInk = helpInk();
    EXPECT_TRUE(speedInk > 50);                                       // "Run and jog speed (RPM)."
    for (int i = 0; i < 3; ++i) rig.key(UI::JoyDirection::DOWN);
    rig.frame();
    rig.dump("motion_help_jog");                                      // "Distance per jog (revolutions)."
    const uint32_t jogInk = helpInk();
    EXPECT_TRUE(jogInk > 50 && jogInk != speedInk);
    // Same selection: not painted again (a pixel planted in it survives)
    rig.panel.frame[MOTION_HELP_Y + 2][120] = 0xFFFF;
    rig.frame();
    rig.frame();
    EXPECT_EQ(rig.panel.px(120, MOTION_HELP_Y + 2), 0xFFFF);
    // A new selection repaints it
    for (int i = 0; i < 4; ++i) rig.key(UI::JoyDirection::DOWN);      // Enable / Release
    rig.frame();
    rig.dump("motion_help_enable");
    EXPECT_TRUE(rig.panel.px(120, MOTION_HELP_Y + 2) != 0xFFFF);
    // ... and it stays clear of the rows and the deck
    EXPECT_EQ(rig.panel.countColor(UI::Theme::DIM, 0, ROWS_END, 240, 4), 0u);
    EXPECT_EQ(rig.panel.countColor(UI::Theme::DIM, 0, MOTION_HELP_Y + UI::Chrome::HELP_H, 240,
                                   static_cast<uint16_t>(UI::Theme::CONTENT_BOTTOM - MOTION_HELP_Y - UI::Chrome::HELP_H)), 0u);
}
