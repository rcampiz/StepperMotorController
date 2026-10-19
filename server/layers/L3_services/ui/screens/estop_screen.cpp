/**
 * @file estop_screen.cpp
 * @brief E-STOP / FAULT page in the panel's look: neon hazard bands, an aberrated block title,
 *        a bracketed status panel, a segmented hold-to-clear bar and a red perspective grid,
 *        fading out and back in on another layout every cycle.
 */

#include "ui/screens/estop_screen.hpp"
#include "ui/chrome/chrome.hpp"
#include "ui/theme.hpp"
#include "ui/display_scratch.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include <stdio.h>
#include <string.h>
#include "harness/pins/motor_state.hpp"

namespace UI {

namespace {

// 5x7 glyphs, one byte per column, bit 0 = top row
struct Glyph { char c; uint8_t cols[5]; };
constexpr Glyph GLYPHS[] = {
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}}, {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}}, {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}}, {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}}, {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}}, {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
};

constexpr uint16_t CELL = 6;                 // title block size: 30 x 42 glyphs
constexpr uint16_t BAND_H = 18;              // hazard bands top and bottom
constexpr uint16_t TITLE_Y = 32;
constexpr uint16_t PANEL_Y = 100, PANEL_H = 62;
constexpr uint16_t GRID_H = 51;
constexpr uint16_t GRID_FLOOR_Y = 246;       // grid as a floor, under the middle
constexpr uint16_t GRID_CEIL_Y = BAND_H + 3; // grid as a ceiling, over it
constexpr uint8_t SEGMENTS = 20;

// Colour schemes (the title's flash starts at a different step in each): labels, brackets,
// prompt, panel fill, grid lines
struct Palette { uint16_t label, corners, prompt, panel, grid; };
constexpr Palette PALETTES[3] = {
    {Theme::CYAN,    Theme::CYAN,    Theme::YELLOW, Theme::PANEL, Theme::RED},
    {Theme::YELLOW,  Theme::MAGENTA, Theme::CYAN,   0x2004,       Theme::MAGENTA},
    {Theme::MAGENTA, Theme::YELLOW,  Theme::TEXT,   0x0866,       Theme::VIOLET},
};

// The eight layouts: the middle's offset (the floor-grid ones fit between the band and the
// grid; the ceiling-grid ones sit ~50 px lower), where the grid is, and the colours
struct Layout { int8_t dx, dy; bool gridTop; uint8_t pal; };
constexpr Layout LAYOUTS[8] = {
    {0, 0, false, 0}, {5, 50, true, 1}, {-5, 8, false, 2}, {0, 62, true, 0},
    {5, -8, false, 1}, {-5, 46, true, 2}, {3, 4, false, 2}, {-3, 56, true, 1},
};

uint16_t mix565(uint16_t a, uint16_t b, int t256)
{
    const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    return static_cast<uint16_t>(((ar + (br - ar) * t256 / 256) << 11) |
                                 ((ag + (bg - ag) * t256 / 256) << 5) | (ab + (bb - ab) * t256 / 256));
}

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

int16_t sinApprox(int32_t a1024, int16_t amp) {             // a sine from two parabolas
    int32_t a = a1024 & 1023;
    const int32_t sign = (a < 512) ? 1 : -1;
    a &= 511;
    return static_cast<int16_t>(sign * a * (512 - a) * amp / 65536);
}

// The title, Hotline Miami style like the saver's: tall slanted block letters, a solid
// shadow extruded down-right, a dark outline, a hot gradient on the faces, colours that
// flash, the whole word rocking and bobbing. Laid once into a 2-bit mask (0 page, 1 outline,
// 2 shadow, 3 face) for the word showing; each update inverse-rotates the box into it.
constexpr int16_t T_CELL_W = 5, T_CELL_H = 5;           // 25 x 35 glyphs
constexpr int16_t T_ADV = 6 * T_CELL_W;
constexpr int16_t T_LINE_H = 7 * T_CELL_H;
constexpr int16_t T_W = 6 * T_ADV - T_CELL_W;           // the longest word, E-STOP
constexpr int16_t T_SLANT = (T_LINE_H - 1) / 4;
constexpr int16_t T_SHADOW = 4;
constexpr int16_t MW = T_W + T_SLANT + T_SHADOW + 2;
constexpr int16_t MH = T_LINE_H + T_SHADOW + 2;
constexpr int16_t MW4 = (MW + 3) / 4;
// The mask lives in the buffer the saver's title shares (only one of the two pages shows)
constexpr size_t MASK_BYTES = static_cast<size_t>(MH) * MW4;
static_assert(MASK_BYTES <= SharedTitleMask::BYTES, "the E-STOP title mask fits the shared buffer");
inline uint8_t* maskBuf() { return SharedTitleMask::data(); }
const char* s_maskWord = nullptr;                      // the word laid in it while this page owns it
constexpr int32_t ROCK_Q16 = 3300;                      // ~3 degrees either way
constexpr int32_t ROCK_FRAMES = 60;
constexpr int32_t BOB_FRAMES = 30;
constexpr int16_t BOB = 2;
constexpr int16_t BOX_HW = MW / 2 + 3;
constexpr int16_t BOX_HH = 28;                          // 56 rows: from 6 above TITLE_Y to the sub-line
constexpr uint16_t FLASH_FRAMES = 12;
struct TitleInk { uint16_t top, bottom, shadow; };
constexpr TitleInk T_INKS[3] = {
    {rgb(255, 40, 60), rgb(255, 150, 0), Theme::CYAN},       // red to orange, cyan shadow
    {rgb(255, 70, 210), rgb(255, 20, 50), Theme::YELLOW},    // pink to red, yellow shadow
    {rgb(255, 245, 90), rgb(255, 80, 0), Theme::MAGENTA},    // yellow to orange, magenta shadow
};
constexpr uint16_t T_OUTLINE = rgb(0, 0, 10);

inline uint8_t maskAt(int16_t x, int16_t y) {
    if (x < 0 || y < 0 || x >= MW || y >= MH) return 0;
    return static_cast<uint8_t>((maskBuf()[y * MW4 + (x >> 2)] >> ((x & 3) * 2)) & 3u);
}

inline void maskSet(int16_t x, int16_t y, uint8_t v) {
    if (x < 0 || y < 0 || x >= MW || y >= MH) return;
    uint8_t& b = maskBuf()[y * MW4 + (x >> 2)];
    const int sh = (x & 3) * 2;
    b = static_cast<uint8_t>((b & ~(3u << sh)) | (static_cast<unsigned>(v) << sh));
}

void buildMask(const char* word) {
    if (!SharedTitleMask::claim(SharedTitleMask::Owner::ESTOP_TITLE)) s_maskWord = nullptr;   // the saver had it
    if (s_maskWord == word) return;
    memset(maskBuf(), 0, MASK_BYTES);
    const int16_t len = static_cast<int16_t>(strlen(word));
    const int16_t x0 = static_cast<int16_t>((T_W - (len * T_ADV - T_CELL_W)) / 2);
    for (int16_t i = 0; i < len; i++) {
        const uint8_t* cols = nullptr;
        for (const Glyph& g : GLYPHS) if (g.c == word[i]) cols = g.cols;
        if (cols == nullptr) continue;
        for (int16_t c = 0; c < 5; c++)
            for (int16_t r = 0; r < 7; r++) {
                if ((cols[c] & (1u << r)) == 0) continue;
                for (int16_t py = 0; py < T_CELL_H; py++) {
                    const int16_t ly = static_cast<int16_t>(r * T_CELL_H + py);
                    const int16_t lean = static_cast<int16_t>((T_LINE_H - 1 - ly) / 4);
                    for (int16_t px = 0; px < T_CELL_W; px++)
                        maskSet(static_cast<int16_t>(1 + x0 + i * T_ADV + c * T_CELL_W + px + lean),
                                static_cast<int16_t>(1 + ly), 3);
                }
            }
    }
    for (int16_t y = MH - 1; y >= 0; y--)
        for (int16_t x = MW - 1; x >= 0; x--)
            if (maskAt(x, y) == 3)
                for (int16_t k = 1; k <= T_SHADOW; k++)
                    if (maskAt(static_cast<int16_t>(x + k), static_cast<int16_t>(y + k)) == 0)
                        maskSet(static_cast<int16_t>(x + k), static_cast<int16_t>(y + k), 2);
    for (int16_t y = 0; y < MH; y++)
        for (int16_t x = 0; x < MW; x++) {
            if (maskAt(x, y) != 0) continue;
            bool near = false;
            for (int16_t dy = -1; dy <= 1 && !near; dy++)
                for (int16_t dx = -1; dx <= 1; dx++)
                    if (maskAt(static_cast<int16_t>(x + dx), static_cast<int16_t>(y + dy)) >= 2) { near = true; break; }
            if (near) maskSet(x, y, 1);
        }
    s_maskWord = word;
}

} // namespace

void EstopScreen::drawTitle(Services::ICanvas& lcd)
{
    const char* word = m_fault ? "FAULT" : "E-STOP";
    buildMask(word);
    ScratchRow lease;                       // the Display task's shared row
    uint8_t* const row = lease.data();
    const int16_t w = static_cast<int16_t>(2 * BOX_HW), rows = static_cast<int16_t>(2 * BOX_HH);
    const int16_t x0 = static_cast<int16_t>(Theme::WIDTH / 2 - BOX_HW + m_offX);
    const int16_t y0 = static_cast<int16_t>(TITLE_Y - 6 + m_offY);
    const int32_t sa = ROCK_Q16 * sinApprox(static_cast<int32_t>(m_tick) * 1024 / ROCK_FRAMES, 1024) / 1024;
    const int32_t ca = 65536 - (sa * sa) / (2 * 65536);
    const int16_t bob = sinApprox(static_cast<int32_t>(m_tick) * 1024 / BOB_FRAMES + 256, BOB);
    const TitleInk& k = T_INKS[(m_tick / FLASH_FRAMES + m_pal) % 3];
    uint16_t face[T_LINE_H];
    for (int ly = 0; ly < T_LINE_H; ly++) face[ly] = ink(mix565(k.top, k.bottom, ly * 256 / (T_LINE_H - 1)));
    const uint16_t shadow = ink(k.shadow), outline = ink(T_OUTLINE);
    const int32_t cx = x0 + BOX_HW, cy = y0 + BOX_HH + bob;
    lcd.streamBitmapStart(static_cast<uint16_t>(x0), static_cast<uint16_t>(y0), static_cast<uint16_t>(w),
                          static_cast<uint16_t>(rows));
    for (int16_t y = y0; y < y0 + rows; y++) {
        const int32_t dy = y - cy;
        int32_t mx = (MW / 2) * 65536 + (x0 - cx) * ca + dy * sa;
        int32_t my = (MH / 2) * 65536 - (x0 - cx) * sa + dy * ca;
        for (int16_t x = 0; x < w; x++, mx += ca, my -= sa) {
            const int16_t ix = static_cast<int16_t>((mx + 32768) >> 16), iy = static_cast<int16_t>((my + 32768) >> 16);
            const uint8_t m = m_alpha > 0 ? maskAt(ix, iy) : 0;
            uint16_t c = Theme::BG;
            if (m == 3) {
                int16_t ly = static_cast<int16_t>(iy - 1);
                if (ly < 0) ly = 0;
                if (ly >= T_LINE_H) ly = T_LINE_H - 1;
                c = face[ly];
            } else if (m == 2) {
                c = shadow;
            } else if (m == 1) {
                c = outline;
            }
            row[x * 2] = static_cast<uint8_t>(c >> 8);
            row[x * 2 + 1] = static_cast<uint8_t>(c & 0xFF);
        }
        lcd.streamBitmapData(row, static_cast<size_t>(w) * 2);
    }
    lcd.streamBitmapEnd();
}

void EstopScreen::onActivate()
{
    m_needsRedraw = true;
    m_full = true;
    m_progress = 0;
    m_message[0] = '\0';
    m_messageNew = false;
    m_phase = Phase::SHOW;
    m_phaseFrames = 0;
    m_alpha = 256;
    m_layout = 0;
    applyLayout();
}

void EstopScreen::applyLayout()
{
    const Layout& l = LAYOUTS[m_layout];
    m_offX = l.dx;
    m_offY = l.dy;
    m_gridTop = l.gridTop;
    m_pal = l.pal;
}

uint16_t EstopScreen::ink(uint16_t c) const
{
    return m_alpha >= 256 ? c : mix565(Theme::BG, c, m_alpha);
}

void EstopScreen::drawBands(Services::ICanvas& lcd)
{
    // Diagonal neon-yellow / page stripes, 12 px wide, crawling one pixel per frame
    ScratchRow lease;                       // the Display task's shared row
    uint8_t* const row = lease.data();
    for (int band = 0; band < 2; band++) {
        const uint16_t y0 = band == 0 ? 0 : static_cast<uint16_t>(Theme::HEIGHT - BAND_H);
        lcd.streamBitmapStart(0, y0, Theme::WIDTH, BAND_H);
        for (uint16_t r = 0; r < BAND_H; r++) {
            for (uint16_t x = 0; x < Theme::WIDTH; x++) {
                const bool on = (((x + r + m_stripePhase) / 12) & 1u) == 0;
                const uint16_t c = on ? Theme::YELLOW : Theme::BG;
                row[x * 2] = static_cast<uint8_t>(c >> 8);
                row[x * 2 + 1] = static_cast<uint8_t>(c & 0xFF);
            }
            lcd.streamBitmapData(row, ScratchRow::BYTES);
        }
        lcd.streamBitmapEnd();
    }
}

void EstopScreen::drawGrid(Services::ICanvas& lcd)
{
    // A grid like the saver's floor, scrolling toward the viewer; as a ceiling it is the same
    // grid upside down (its horizon along the bottom edge)
    ScratchRow lease;                       // the Display task's shared row
    uint8_t* const row = lease.data();
    constexpr float K = 60.0f;
    const uint16_t lineColour = PALETTES[m_pal].grid;
    const uint16_t y0 = m_gridTop ? GRID_CEIL_Y : GRID_FLOOR_Y;
    lcd.streamBitmapStart(0, y0, Theme::WIDTH, GRID_H);
    for (uint16_t i = 0; i < GRID_H; i++) {
        const uint16_t r = m_gridTop ? static_cast<uint16_t>(GRID_H - 1 - i) : i;   // depth row
        const uint16_t bg = mix565(0x3005 /* deep red-violet */, Theme::BG, r * 256 / GRID_H);
        const uint16_t line = mix565(bg, lineColour, 90 + r * 166 / GRID_H);
        bool horizontal = (r == 0);
        if (r >= 5) {
            const float depth = K / static_cast<float>(r);
            const int n = static_cast<int>(depth + m_gridPhase + 0.5f);
            const float off = K / (static_cast<float>(n) - m_gridPhase) - static_cast<float>(r);
            horizontal = n > 0 && off > -0.5f && off <= 0.5f;
        }
        for (uint16_t x = 0; x < Theme::WIDTH; x++) {
            const uint16_t c = horizontal ? (r == 0 ? Theme::MAGENTA : line) : bg;
            row[x * 2] = static_cast<uint8_t>(c >> 8);
            row[x * 2 + 1] = static_cast<uint8_t>(c & 0xFF);
        }
        const int span = 4 + r * 22 / GRID_H;
        if (!horizontal && span >= 9) {
            for (int j = -12; j <= 12; j++) {
                const int x = Theme::WIDTH / 2 + j * span;
                if (x >= 0 && x < Theme::WIDTH) {
                    row[x * 2] = static_cast<uint8_t>(line >> 8);
                    row[x * 2 + 1] = static_cast<uint8_t>(line & 0xFF);
                }
            }
        }
        lcd.streamBitmapData(row, ScratchRow::BYTES);
    }
    lcd.streamBitmapEnd();
}

void EstopScreen::setMessage(const char* msg)
{
    snprintf(m_message, sizeof m_message, "%s", msg != nullptr ? msg : "");
    m_messageNew = true;
}

void EstopScreen::drawStatic(Services::ICanvas& lcd)
{
    lcd.waitForVSync();
    lcd.fillScreen(Theme::BG);
    drawBands(lcd);
    lcd.fillRect(0, BAND_H, Theme::WIDTH, 2, Theme::MAGENTA);
    lcd.fillRect(0, static_cast<uint16_t>(Theme::HEIGHT - BAND_H - 2), Theme::WIDTH, 2, Theme::MAGENTA);
    drawGrid(lcd);
    if (m_alpha > 0) drawMiddle(lcd);
    m_drawnProgress = 0xFF;
    m_drawnMessage[0] = '\x01';
    m_drawnStatus = 0xFFFF;
}

void EstopScreen::drawMiddle(Services::ICanvas& lcd)
{
    // Everything between the bands but the grid, at m_alpha: drawn over itself while it
    // fades, so at alpha 0 it is gone with nothing else to clear
    const Palette& p = PALETTES[m_pal];
    const int16_t ox = m_offX, oy = m_offY;
    auto X = [&](int v) { return static_cast<uint16_t>(v + ox); };
    auto Y = [&](int v) { return static_cast<uint16_t>(v + oy); };
    const uint16_t panel = ink(p.panel);
    const uint16_t page = Theme::BG;

    // Title: the Hotline Miami word (it keeps rocking between these repaints)
    drawTitle(lcd);
    lcd.blitTextLine(X(Theme::MARGIN), Y(82), Theme::WIDTH - 2 * Theme::MARGIN - 6, 8,
                     m_fault ? "     DRIVER FAULT  //  MOTOR SAFE" : "   EMERGENCY STOP  //  MOTOR SAFE",
                     ink(Theme::DIM), page);

    // Status panel, bracketed like the menu pages
    lcd.fillRect(X(Theme::MARGIN), Y(PANEL_Y), Theme::WIDTH - 2 * Theme::MARGIN, PANEL_H, panel);
    Chrome::drawCorners(lcd, X(Theme::MARGIN), Y(PANEL_Y), Theme::WIDTH - 2 * Theme::MARGIN, PANEL_H, 8, ink(p.corners));
    lcd.blitTextLine(X(16), Y(PANEL_Y + 8), 50, 8, "MOTOR", ink(p.label), panel);
    lcd.drawString(X(70), Y(PANEL_Y + 6), "released (Hi-Z)", ink(Theme::TEXT), panel, Theme::FONT_MEDIUM);
    lcd.blitTextLine(X(16), Y(PANEL_Y + 28), 50, 8, "DRIVER", ink(p.label), panel);
    lcd.blitTextLine(X(16), Y(PANEL_Y + 46), 50, 8, "LATCH", ink(p.label), panel);
    lcd.blitTextLine(X(70), Y(PANEL_Y + 46), 140, 8, m_fault ? "driver fault" : "E-STOP", ink(Theme::MAGENTA), panel);

    // The way out
    lcd.drawString(X(28), Y(176), "HOLD CENTER 3s TO CLEAR", ink(p.prompt), page, Theme::FONT_MEDIUM);
    Chrome::drawCorners(lcd, X(BAR_X - 4), Y(BAR_Y - 4),
                        static_cast<uint16_t>(BAR_W + 8), static_cast<uint16_t>(BAR_H + 8), 6, ink(Theme::GRID_MAJ));

    // The live parts follow in render(), at the same strength
    m_drawnProgress = 0xFF;
    m_drawnMessage[0] = '\x01';
    m_drawnStatus = 0xFFFF;
}

void EstopScreen::drawFaults(Services::ICanvas& lcd, uint8_t faults, bool readable)
{
    char line[40];
    const bool ocd = (faults & Harness::DriverFault::OVERCURRENT) != 0u;
    const bool uvlo = (faults & Harness::DriverFault::UNDERVOLTAGE) != 0u;
    const bool thsd = (faults & Harness::DriverFault::THERMAL_SHUTDOWN) != 0u;
    const bool stalled = (faults & Harness::DriverFault::STALL) != 0u;
    if (!readable) {
        snprintf(line, sizeof line, "in standby");
    } else if (ocd || uvlo || thsd || stalled) {
        snprintf(line, sizeof line, "%s%s%s%s", ocd ? "OCD " : "", uvlo ? "UVLO " : "",
                 thsd ? "THERMAL " : "", stalled ? "STALL" : "");
    } else {
        snprintf(line, sizeof line, "no fault");
    }
    const bool bad = readable && (ocd || uvlo || thsd || stalled);
    lcd.blitTextLine(static_cast<uint16_t>(70 + m_offX), static_cast<uint16_t>(PANEL_Y + 28 + m_offY), 140, 8,
                     line, ink(bad ? Theme::ORANGE : Theme::GREEN), ink(PALETTES[m_pal].panel));
}

void EstopScreen::render(Services::ICanvas& lcd)
{
    // Someone is acting on the page (holding CENTER, or a clear just answered): show it now
    // and keep it while they do
    const bool wake = m_progress > 0 || m_messageNew;
    m_messageNew = false;
    if (wake && m_phase != Phase::SHOW) {
        m_phase = Phase::SHOW;
        m_alpha = 256;
        m_full = true;
    }
    if (wake) m_phaseFrames = 0;

    m_tick++;
    bool middle = false;           // the middle fades this frame
    if (!m_full) {
        switch (m_phase) {
            case Phase::SHOW:
                if (++m_phaseFrames >= SHOW_FRAMES) { m_phase = Phase::FADE_OUT; m_phaseFrames = 0; }
                break;
            case Phase::FADE_OUT:
                m_phaseFrames++;
                m_alpha = static_cast<uint16_t>(256 - m_phaseFrames * 256 / FADE_FRAMES);
                middle = true;
                if (m_phaseFrames >= FADE_FRAMES) { m_alpha = 0; m_phase = Phase::DARK; m_phaseFrames = 0; }
                break;
            case Phase::DARK:
                if (++m_phaseFrames >= DARK_FRAMES) {
                    // The next layout: the grid may move, so the page is repainted (the
                    // middle is invisible at alpha 0) and the fade in starts there
                    m_layout = static_cast<uint8_t>((m_layout + 1) % 8);
                    applyLayout();
                    m_phase = Phase::FADE_IN;
                    m_phaseFrames = 0;
                    m_full = true;
                }
                break;
            case Phase::FADE_IN:
                m_phaseFrames++;
                m_alpha = static_cast<uint16_t>(m_phaseFrames * 256 / FADE_FRAMES);
                middle = true;
                if (m_phaseFrames >= FADE_FRAMES) { m_alpha = 256; m_phase = Phase::SHOW; m_phaseFrames = 0; }
                break;
        }
    }

    if (m_full) {
        m_full = false;
        drawStatic(lcd);
    } else if (middle) {
        // Fading: just the middle (the stripes and grid pause for these few frames)
        lcd.waitForVSync();
        drawMiddle(lcd);
    } else {
        // The stripes crawl and the grid scrolls every frame
        m_stripePhase = static_cast<uint8_t>((m_stripePhase + 1) % 24);
        m_gridPhase += 0.06f;
        if (m_gridPhase >= 1.0f) m_gridPhase -= 1.0f;
        lcd.waitForVSync();
        drawBands(lcd);
        drawGrid(lcd);
        if ((m_tick & 1) != 0 && m_alpha > 0) drawTitle(lcd);     // rocking and flashing
    }

    Protocol::TelemetrySnapshot t = Services::g_telemetryStore.getSnapshot();
    const uint16_t shown = static_cast<uint16_t>(t.motor.faults | (t.motor.statusReadable ? 0x100u : 0u));
    if (shown != m_drawnStatus) {
        m_drawnStatus = shown;
        drawFaults(lcd, t.motor.faults, t.motor.statusReadable);
    }
    if (m_progress != m_drawnProgress) {
        // Segments, filled left to right, cyan shading to magenta
        const uint8_t lit = static_cast<uint8_t>(m_progress * SEGMENTS / 100);
        const uint16_t seg = BAR_W / SEGMENTS;
        for (uint8_t i = 0; i < SEGMENTS; i++) {
            const uint16_t c = (i < lit) ? mix565(Theme::CYAN, Theme::MAGENTA, i * 256 / (SEGMENTS - 1))
                                         : PALETTES[m_pal].panel;
            lcd.fillRect(static_cast<uint16_t>(BAR_X + i * seg + m_offX), static_cast<uint16_t>(BAR_Y + m_offY),
                         static_cast<uint16_t>(seg - 2), BAR_H, ink(c));
        }
        m_drawnProgress = m_progress;
    }
    if (strcmp(m_message, m_drawnMessage) != 0) {
        snprintf(m_drawnMessage, sizeof m_drawnMessage, "%s", m_message);
        lcd.blitTextLine(static_cast<uint16_t>(20 + m_offX), static_cast<uint16_t>(224 + m_offY), 196, 8,
                         m_message, ink(Theme::ORANGE), Theme::BG);
    }
}

} // namespace UI
