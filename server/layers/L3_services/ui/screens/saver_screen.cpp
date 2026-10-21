/**
 * @file saver_screen.cpp
 * @brief Hotline Miami screen saver: stripe band, aberrated title card, VHS glitches.
 */

#include "ui/screens/saver_screen.hpp"
#include "ui/theme.hpp"
#include "ui/display_scratch.hpp"
#include "graphics/primitives/primitives.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "harness/pins/protocol_version.hpp"
#include <string.h>

namespace UI {

// 5x7 glyphs, one byte per column, bit 0 = top row (the driver's font shapes)
static constexpr uint8_t GLYPH_S[5] = { 0x46, 0x49, 0x49, 0x49, 0x31 };
static constexpr uint8_t GLYPH_M[5] = { 0x7F, 0x02, 0x0C, 0x02, 0x7F };
static constexpr uint8_t GLYPH_C[5] = { 0x3E, 0x41, 0x41, 0x41, 0x22 };

// The stripe band: four colours cycling along the diagonal
static constexpr uint16_t STRIPES[4] = { Theme::MAGENTA, Theme::VIOLET, Theme::CYAN, 0x5010 /* deep purple */ };
static constexpr uint16_t STRIPE_W = 14;

void SaverScreen::onActivate()
{
    m_needsRedraw = true;       // the manager paints the panel black
    m_frames = HOLD_FRAMES;     // hotline: lay out on the first frame
    m_glitchFrames = 0;
    m_drawn = false;
    m_glitchDrawn = false;
    m_synthReady = false;       // synthwave: a new scene on the first frame
    m_frame = 0;
}

uint32_t SaverScreen::nextRandom()
{
    m_seed ^= m_seed << 13;
    m_seed ^= m_seed >> 17;
    m_seed ^= m_seed << 5;
    return m_seed;
}

void SaverScreen::drawBand(Services::ICanvas& lcd, uint16_t y, uint8_t phase, bool erase)
{
    if (erase) {
        lcd.fillRect(0, y, Theme::WIDTH, BAND_H, Theme::BLACK);
        return;
    }
    // 45-degree stripes: on each row the pattern shifts one pixel; drawn as row segments
    for (uint16_t r = 0; r < BAND_H; r++) {
        auto yy = static_cast<uint16_t>(y + r);
        // first segment starts at a negative offset so the diagonal is continuous
        int16_t x = -static_cast<int16_t>((r + phase * STRIPE_W) % (4 * STRIPE_W));
        uint8_t c = 0;
        while (x < static_cast<int16_t>(Theme::WIDTH)) {
            int16_t x0 = x < 0 ? 0 : x;
            int16_t x1 = static_cast<int16_t>(x + STRIPE_W);
            if (x1 > static_cast<int16_t>(Theme::WIDTH)) x1 = Theme::WIDTH;
            if (x1 > x0) lcd.fillRect(static_cast<uint16_t>(x0), yy, static_cast<uint16_t>(x1 - x0), 1, STRIPES[c]);
            x = static_cast<int16_t>(x + STRIPE_W);
            c = static_cast<uint8_t>((c + 1) & 3);
        }
    }
    // Hard edges above and below, the VHS way: a bright line and a dark gap
    lcd.fillRect(0, static_cast<uint16_t>(y - 2), Theme::WIDTH, 1, Theme::CYAN);
    lcd.fillRect(0, static_cast<uint16_t>(y + BAND_H + 1), Theme::WIDTH, 1, Theme::MAGENTA);
}

void SaverScreen::drawGlyph(Services::ICanvas& lcd, const uint8_t* glyph, uint16_t x, uint16_t y, uint16_t color)
{
    for (uint8_t col = 0; col < 5; col++) {
        for (uint8_t row = 0; row < 7; row++) {
            if (glyph[col] & (1u << row)) {
                lcd.fillRect(static_cast<uint16_t>(x + col * CELL), static_cast<uint16_t>(y + row * CELL), CELL, CELL, color);
            }
        }
    }
}

void SaverScreen::drawCard(Services::ICanvas& lcd, uint16_t x, uint16_t y, bool erase)
{
    if (erase) {
        lcd.fillRect(x, y, CARD_W, CARD_H, Theme::BLACK);
        return;
    }
    const uint8_t* glyphs[3] = { GLYPH_S, GLYPH_M, GLYPH_C };
    // Chromatic aberration: magenta copy shifted left, cyan copy shifted right, white on top
    for (uint8_t i = 0; i < 3; i++) {
        auto gx = static_cast<uint16_t>(x + 6 + i * 6 * CELL);
        drawGlyph(lcd, glyphs[i], static_cast<uint16_t>(gx - 3), static_cast<uint16_t>(y + 3), Theme::MAGENTA);
        drawGlyph(lcd, glyphs[i], static_cast<uint16_t>(gx + 3), static_cast<uint16_t>(y + 3), Theme::CYAN);
    }
    for (uint8_t i = 0; i < 3; i++) {
        auto gx = static_cast<uint16_t>(x + 6 + i * 6 * CELL);
        drawGlyph(lcd, glyphs[i], gx, static_cast<uint16_t>(y + 3), Theme::TEXT);
    }
}

void SaverScreen::layout(Services::ICanvas& lcd)
{
    if (m_glitchDrawn) {
        lcd.fillRect(m_glitchX, m_glitchY, m_glitchW, 3, Theme::BLACK);
        m_glitchDrawn = false;
    }
    if (m_drawn) {
        drawCard(lcd, m_cardX, m_cardY, true);
        drawBand(lcd, m_bandY, m_bandPhase, true);
        lcd.fillRect(0, static_cast<uint16_t>(m_bandY - 2), Theme::WIDTH, 1, Theme::BLACK);
        lcd.fillRect(0, static_cast<uint16_t>(m_bandY + BAND_H + 1), Theme::WIDTH, 1, Theme::BLACK);
    }
    // The band somewhere in the upper or lower third, the card clear of it
    m_bandY = static_cast<uint16_t>(8 + nextRandom() % (Theme::HEIGHT - BAND_H - 16));
    m_bandPhase = static_cast<uint8_t>(nextRandom() & 3);
    uint16_t above = m_bandY > CARD_H + 10 ? static_cast<uint16_t>(m_bandY - CARD_H - 8) : 0;
    uint16_t below = static_cast<uint16_t>(Theme::HEIGHT - (m_bandY + BAND_H) > CARD_H + 10
                                           ? Theme::HEIGHT - (m_bandY + BAND_H) - CARD_H - 8 : 0);
    if (above >= below) {
        m_cardY = static_cast<uint16_t>(2 + (above ? nextRandom() % above : 0));
    } else {
        m_cardY = static_cast<uint16_t>(m_bandY + BAND_H + 6 + nextRandom() % below);
    }
    m_cardX = static_cast<uint16_t>(nextRandom() % (Theme::WIDTH - CARD_W));
    drawBand(lcd, m_bandY, m_bandPhase, false);
    drawCard(lcd, m_cardX, m_cardY, false);
    m_drawn = true;
}

void SaverScreen::glitch(Services::ICanvas& lcd)
{
    // A VHS slice: a short bright bar at a random height, gone on the next glitch
    if (m_glitchDrawn) {
        lcd.fillRect(m_glitchX, m_glitchY, m_glitchW, 3, Theme::BLACK);
        m_glitchDrawn = false;
    }
    if ((nextRandom() & 3) == 0) return;             // sometimes nothing, so it does not tick
    m_glitchW = static_cast<uint16_t>(30 + nextRandom() % 120);
    m_glitchX = static_cast<uint16_t>(nextRandom() % (Theme::WIDTH - m_glitchW));
    m_glitchY = static_cast<uint16_t>(nextRandom() % (Theme::HEIGHT - 3));
    // Never over the band or the card: those are repainted on the next layout anyway,
    // but a slice through them would leave a hole until then
    if (m_glitchY + 3 > m_bandY - 2 && m_glitchY < m_bandY + BAND_H + 2) return;
    if (m_glitchY + 3 > m_cardY && m_glitchY < m_cardY + CARD_H) return;
    lcd.fillRect(m_glitchX, m_glitchY, m_glitchW, 1, Theme::CYAN);
    lcd.fillRect(m_glitchX, static_cast<uint16_t>(m_glitchY + 1), m_glitchW, 1, Theme::TEXT);
    lcd.fillRect(m_glitchX, static_cast<uint16_t>(m_glitchY + 2), m_glitchW, 1, Theme::MAGENTA);
    m_glitchDrawn = true;
}

void SaverScreen::render(Services::ICanvas& lcd)
{
    const SaverStyle style = (m_settings != nullptr) ? m_settings->saverStyle : SaverStyle::SYNTHWAVE;
    if (style == SaverStyle::BLANK) return;        // black, nothing moves, nothing is sent
    if (style == SaverStyle::SYNTHWAVE) { renderSynthwave(lcd); return; }
    renderHotline(lcd);
}

void SaverScreen::renderHotline(Services::ICanvas& lcd)
{
    if (++m_frames >= HOLD_FRAMES) {
        m_frames = 0;
        layout(lcd);
        return;
    }
    if (++m_glitchFrames >= GLITCH_FRAMES) {
        m_glitchFrames = 0;
        glitch(lcd);
    }
}


// =========================================================================================
// Synthwave scenes: a new one every 30 s, never the same horizon, sun or palette twice in
// a row, so no part of the panel holds one picture; the title sweeps across now and then
// =========================================================================================

namespace {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

inline uint16_t mix(uint16_t a, uint16_t b, int t256) {      // t256 = 0 -> a, 256 -> b
    if (t256 <= 0) return a;
    if (t256 >= 256) return b;
    const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    const int r = ar + (br - ar) * t256 / 256;
    const int g = ag + (bg - ag) * t256 / 256;
    const int bl = ab + (bb - ab) * t256 / 256;
    return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}

struct Palette {
    uint16_t skyTop, skyMid, haze;
    uint16_t sunTop, sunBottom, glow;
    uint16_t line, dim, fill;               // mountains / buildings
    uint16_t horizon, floorFar, floorNear, grid;
    uint16_t window;                        // lit windows in the city
};

constexpr Palette PALETTES[] = {
    // sunset: violet sky, pink haze, yellow-to-pink sun, cyan wireframe, magenta grid
    {rgb(8, 2, 26), rgb(44, 8, 78), rgb(170, 30, 120), rgb(255, 226, 96), rgb(255, 40, 160), rgb(255, 70, 170),
     rgb(40, 236, 255), rgb(18, 120, 160), rgb(6, 12, 34), rgb(255, 110, 210), rgb(44, 0, 64), rgb(8, 0, 20),
     rgb(255, 40, 210), rgb(120, 255, 255)},
    // blade runner: teal smog, amber haze, orange sun, amber lines, orange grid
    {rgb(4, 8, 14), rgb(14, 40, 52), rgb(200, 90, 40), rgb(255, 200, 120), rgb(255, 70, 30), rgb(255, 120, 50),
     rgb(255, 170, 60), rgb(150, 80, 30), rgb(10, 12, 16), rgb(255, 150, 80), rgb(40, 20, 10), rgb(6, 6, 10),
     rgb(255, 120, 40), rgb(255, 220, 140)},
    // cyber: black to purple, magenta-violet sun, magenta lines, cyan grid
    {rgb(0, 0, 0), rgb(20, 0, 40), rgb(90, 0, 140), rgb(255, 90, 220), rgb(120, 40, 255), rgb(200, 60, 255),
     rgb(255, 40, 210), rgb(140, 20, 120), rgb(10, 0, 20), rgb(80, 240, 255), rgb(0, 30, 50), rgb(0, 4, 10),
     rgb(40, 236, 255), rgb(255, 120, 230)},
    // acid: deep teal, green haze, lime sun, magenta lines, green grid
    {rgb(0, 6, 8), rgb(0, 30, 30), rgb(0, 140, 120), rgb(220, 255, 120), rgb(40, 255, 180), rgb(80, 255, 170),
     rgb(255, 60, 200), rgb(140, 30, 110), rgb(4, 14, 14), rgb(200, 255, 120), rgb(0, 40, 30), rgb(0, 6, 6),
     rgb(60, 255, 170), rgb(255, 255, 160)},
};
constexpr uint8_t PALETTE_COUNT = sizeof(PALETTES) / sizeof(PALETTES[0]);

constexpr uint16_t STAR     = rgb(255, 255, 255);
constexpr uint16_t STAR_DIM = rgb(150, 130, 210);
constexpr int16_t W = 240;
constexpr int16_t H = 320;
static_assert(W * 2 == ScratchRow::BYTES, "a saver row is one panel row");
constexpr float GRID_K = 150.0f;      // floor row = horizon + K / depth
constexpr float GRID_STEP = 26.0f;    // vertical lines' spacing on the bottom row
constexpr int16_t MIN_LINE_ROW = 18;  // above this the horizontal lines would be < 2 px apart

inline int16_t sinApprox(int32_t a1024, int16_t amp) {       // sine from a parabola, good enough for hills
    int32_t a = a1024 & 1023;
    int32_t s = (a < 512) ? 1 : -1;
    a &= 511;
    int32_t p = a * (512 - a);                               // 0 .. 65536
    return static_cast<int16_t>(s * p * amp / 65536);
}

// The title, in 5x7 block glyphs (one byte per column, bit 0 = top row)
struct Glyph { char c; uint8_t cols[5]; };
constexpr Glyph TITLE_GLYPHS[] = {
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}}, {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}}, {'P', {0x7F, 0x09, 0x09, 0x09, 0x0F}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x4F}}, {'M', {0x7F, 0x01, 0x7E, 0x01, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}}, {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}}, {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    // the release line under it
    {'v', {0x1C, 0x20, 0x40, 0x20, 0x1C}}, {'.', {0x60, 0x60, 0x00, 0x00, 0x00}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'0', {0x3E, 0x41, 0x41, 0x41, 0x3E}}, {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x62, 0x51, 0x49, 0x49, 0x46}}, {'3', {0x22, 0x41, 0x49, 0x49, 0x36}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}}, {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3E, 0x49, 0x49, 0x49, 0x32}}, {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}}, {'9', {0x26, 0x49, 0x49, 0x49, 0x3E}},
};
// The whole name at once, three centred lines, in the Hotline Miami manner: tall slanted
// block letters with a hard offset shadow and a dark outline, neon colours that flash, the
// whole title rocking and bobbing like that game's menu.
constexpr const char* TITLE_LINES[3] = {"STEPPER", "MOTOR", "CONTROLLER"};
constexpr int16_t CELL_W = 3, CELL_H = 4;               // 15 x 28 glyphs: tall and narrow
constexpr int16_t TITLE_ADV = 6 * CELL_W;
constexpr int16_t TITLE_LINE_H = 7 * CELL_H;
constexpr int16_t TITLE_PITCH = TITLE_LINE_H + 5;       // line to line
constexpr int16_t TITLE_W = 10 * TITLE_ADV - CELL_W;    // the longest line, CONTROLLER
constexpr int16_t TITLE_H = 2 * TITLE_PITCH + TITLE_LINE_H;
constexpr int16_t SLANT = (TITLE_LINE_H - 1) / 4;       // italic: tops lean right by up to 6 px
constexpr int16_t SHADOW = 3;                           // shadow offset, right and down
// The version under the name ("v0.6.3", Harness::fwVersion()): the same 5x7 glyphs, smaller
// (2x3 cells), spaced by their ink, with the same lean, a shallower shadow and a neon rule
// either side; white-to-cyan on a magenta shadow that holds still while the name flashes. It
// is part of the title's mask, so it fades, rocks and bobs with the name in the same box and
// the same pass.
constexpr int16_t VER_CW = 2, VER_CH = 3;               // 10 x 21 glyphs: tall and narrow too
constexpr int16_t VER_H = 7 * VER_CH;
constexpr int16_t VER_GAP = 6;                          // CONTROLLER's baseline to the version's top
constexpr int16_t VER_TOP = TITLE_H + VER_GAP;          // in title coordinates
constexpr int16_t VER_SHADOW = 2;
constexpr int16_t VER_RULE_GAP = 7;                     // text to rule
constexpr int16_t VER_RULE_ROW = 3 * VER_CH;            // the rules sit on the middle glyph row
static_assert(VER_GAP >= SHADOW + 3, "the version's outline must not touch CONTROLLER's shadow");
// The mask: the unrotated title, 2 bits a pixel (0 sky, 1 outline, 2 shadow, 3 face), with
// a pixel of margin for the outline
constexpr int16_t MW = TITLE_W + SLANT + SHADOW + 2;
constexpr int16_t MH = VER_TOP + VER_H + VER_SHADOW + 2;
constexpr int16_t MW4 = (MW + 3) / 4;                   // bytes a mask row
// The mask lives in the buffer the E-STOP page's title shares (only one of the two pages shows)
constexpr size_t MASK_BYTES = static_cast<size_t>(MH) * MW4;
static_assert(MASK_BYTES <= SharedTitleMask::BYTES, "the saver title mask fits the shared buffer");
inline uint8_t* maskBuf() { return SharedTitleMask::data(); }
bool s_maskBuilt = false;                               // laid while this page owns the buffer
// Rocking: up to ROCK_Q16 radians (~4 degrees) either way, over ROCK_FRAMES; a bob of BOB px
constexpr int32_t ROCK_Q16 = 4600;
constexpr int32_t ROCK_FRAMES = 80;
constexpr int32_t BOB_FRAMES = 36;
constexpr int16_t BOB = 2;
// The box the rocking title can reach (fixed for a scene): half-width and half-height
constexpr int16_t BOX_HW = MW / 2 + (MH / 2) * 72 / 1000 + 2;
constexpr int16_t BOX_HH = MH / 2 + (MW / 2) * 72 / 1000 + BOB + 2;
// Scene layout: the horizon's range, and the title box's top; the box stays in the sky (it is
// composited over synthUpper, which knows nothing of the floor)
constexpr int16_t HORIZON_MIN = 150, HORIZON_SPAN = 80;
constexpr int16_t TITLE_Y_MIN = 2, TITLE_Y_SPAN = 6;
static_assert(TITLE_Y_MIN + TITLE_Y_SPAN - 1 + 2 * BOX_HH - 1 <= HORIZON_MIN, "the title box must stay above the horizon");
// Colour schemes that flash in turn, each line one step along: face top, face bottom, shadow
struct TitleInk { uint16_t top, bottom, shadow; };
constexpr TitleInk TITLE_INKS[3] = {
    {rgb(255, 60, 200), rgb(255, 120, 40), rgb(40, 200, 255)},    // pink to orange, cyan shadow
    {rgb(255, 250, 120), rgb(255, 150, 0), rgb(240, 0, 150)},     // yellow to amber, magenta
    {rgb(90, 255, 255), rgb(30, 110, 255), rgb(255, 30, 120)},    // cyan to blue, hot pink
};
constexpr uint16_t TITLE_OUTLINE = rgb(14, 0, 24);
// The version: white at the top to neon cyan, a magenta shadow
constexpr uint16_t VER_TOP_INK = rgb(250, 255, 255), VER_BOTTOM_INK = rgb(40, 220, 255);
constexpr uint16_t VER_SHADOW_INK = rgb(255, 20, 170);
constexpr uint16_t FLASH_FRAMES = 16;                   // a new colour step every 0.8 s
// Fade in, hold, fade out (frames at 20 Hz)
constexpr uint16_t FADE_FRAMES = 8;                     // 0.4 s (was 0.8: sooner, owner 2026-10-21)
constexpr uint16_t HOLD_TITLE_FRAMES = 200;

const uint8_t* glyphFor(char c) {
    for (const Glyph& g : TITLE_GLYPHS) if (g.c == c) return g.cols;
    return nullptr;
}

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

/// The columns a glyph inks: the version line is spaced by ink, not by cell
int16_t glyphInk(const uint8_t* g, int16_t& first) {
    first = 0;
    int16_t last = -1;
    for (int16_t c = 0; c < 5; c++) {
        if (g[c] == 0) continue;
        if (last < 0) first = c;
        last = c;
    }
    return static_cast<int16_t>(last - first + 1);
}

/// One face pixel of the version line: title coordinates, leaning like the name
void versionPixel(int16_t x, int16_t ly) {
    const int16_t lean = static_cast<int16_t>((VER_H - 1 - ly) / 4);
    maskSet(static_cast<int16_t>(1 + x + lean), static_cast<int16_t>(1 + VER_TOP + ly), 3);
}

/// "v<fwVersion()>" centred under the name, with a rule either side of it
void buildVersionLine() {
    char text[16];
    text[0] = 'v';
    size_t n = 1;
    // The number only: build metadata ("+dirty") would not fit the big glyphs; SYST:VER? says it
    for (const char* p = Harness::fwVersion(); *p != '\0' && *p != '+' && n < sizeof text - 1; p++) text[n++] = *p;
    text[n] = '\0';
    // Width in cells: a blank cell between glyphs and an extra one either side of a point (it
    // is small, and leaning it would crowd the digits); what does not fit under the name is dropped
    auto span = [](char ch, const uint8_t*& g, int16_t& first, int16_t& lead) -> int16_t {
        g = glyphFor(ch);
        first = 0;
        lead = (ch == '.') ? 1 : 0;
        const int16_t cols = g ? glyphInk(g, first) : 3;
        return static_cast<int16_t>(cols + 2 * lead);
    };
    int16_t widthCells = 0;
    size_t shown = 0;
    for (; shown < n; shown++) {
        const uint8_t* g;
        int16_t first, lead;
        const int16_t next = static_cast<int16_t>(widthCells + (shown ? 1 : 0) + span(text[shown], g, first, lead));
        if (next * VER_CW > TITLE_W - VER_CW * 4) break;
        widthCells = next;
    }
    const int16_t width = static_cast<int16_t>(widthCells * VER_CW);
    const int16_t x0 = static_cast<int16_t>((TITLE_W - width) / 2);
    int16_t x = x0;
    for (size_t i = 0; i < shown; i++) {
        const uint8_t* g;
        int16_t first, lead;
        const int16_t cells = span(text[i], g, first, lead);
        const int16_t gx = static_cast<int16_t>(x + lead * VER_CW);
        for (int16_t c = 0; g != nullptr && c < cells - 2 * lead; c++)
            for (int16_t r = 0; r < 7; r++) {
                if ((g[first + c] & (1u << r)) == 0) continue;
                for (int16_t py = 0; py < VER_CH; py++)
                    for (int16_t px = 0; px < VER_CW; px++)
                        versionPixel(static_cast<int16_t>(gx + c * VER_CW + px), static_cast<int16_t>(r * VER_CH + py));
            }
        x = static_cast<int16_t>(x + (cells + 1) * VER_CW);
    }
    // The rules: from under CONTROLLER's ends in to the text, where there is room
    const int16_t inset = TITLE_ADV;
    const int16_t leftEnd = static_cast<int16_t>(x0 - VER_RULE_GAP);
    const int16_t rightStart = static_cast<int16_t>(x0 + width + VER_RULE_GAP);
    if (leftEnd - inset < 8) return;
    for (int16_t ly = VER_RULE_ROW; ly < VER_RULE_ROW + VER_CW; ly++) {
        for (int16_t rx = inset; rx < leftEnd; rx++) versionPixel(rx, ly);
        for (int16_t rx = rightStart; rx < TITLE_W - inset; rx++) versionPixel(rx, ly);
    }
}

/// Lay the letters into the mask once: faces (slanted), then the shadow behind them, then
/// a one-pixel dark outline round both
void buildTitleMask() {
    if (!SharedTitleMask::claim(SharedTitleMask::Owner::SAVER_TITLE)) s_maskBuilt = false;   // the E-STOP page had it
    if (s_maskBuilt) return;
    memset(maskBuf(), 0, MASK_BYTES);
    for (int16_t line = 0; line < 3; line++) {
        const char* text = TITLE_LINES[line];
        const int16_t len = static_cast<int16_t>(strlen(text));
        const int16_t lineX = static_cast<int16_t>((TITLE_W - (len * TITLE_ADV - CELL_W)) / 2);
        for (int16_t i = 0; i < len; i++) {
            const uint8_t* g = glyphFor(text[i]);
            if (g == nullptr) continue;
            for (int16_t c = 0; c < 5; c++)
                for (int16_t r = 0; r < 7; r++) {
                    if ((g[c] & (1u << r)) == 0) continue;
                    for (int16_t py = 0; py < CELL_H; py++) {
                        const int16_t ly = static_cast<int16_t>(r * CELL_H + py);
                        const int16_t lean = static_cast<int16_t>((TITLE_LINE_H - 1 - ly) / 4);
                        for (int16_t px = 0; px < CELL_W; px++) {
                            maskSet(static_cast<int16_t>(1 + lineX + i * TITLE_ADV + c * CELL_W + px + lean),
                                    static_cast<int16_t>(1 + line * TITLE_PITCH + ly), 3);
                        }
                    }
                }
        }
    }
    buildVersionLine();
    for (int16_t y = MH - 1; y >= 0; y--)                  // shadow: faces moved right and down
        for (int16_t x = MW - 1; x >= 0; x--) {
            const int16_t depth = (y - 1 >= VER_TOP) ? VER_SHADOW : SHADOW;
            if (maskAt(x, y) == 3 && maskAt(static_cast<int16_t>(x + depth), static_cast<int16_t>(y + depth)) == 0)
                for (int16_t k = 1; k <= depth; k++)       // a solid extrusion, not a copy
                    if (maskAt(static_cast<int16_t>(x + k), static_cast<int16_t>(y + k)) == 0)
                        maskSet(static_cast<int16_t>(x + k), static_cast<int16_t>(y + k), 2);
        }
    for (int16_t y = 0; y < MH; y++)                       // outline round faces and shadow
        for (int16_t x = 0; x < MW; x++) {
            if (maskAt(x, y) != 0) continue;
            bool near = false;
            for (int16_t dy = -1; dy <= 1 && !near; dy++)
                for (int16_t dx = -1; dx <= 1; dx++)
                    if (maskAt(static_cast<int16_t>(x + dx), static_cast<int16_t>(y + dy)) >= 2) { near = true; break; }
            if (near) maskSet(x, y, 1);
        }
    s_maskBuilt = true;
}

/// Rows streamed ScratchRow::ROWS a call (a call per row cost ~0.2 ms of lock and DMA set-up,
/// ~1500 a second: bench 2026-10-21): compose(y, buf) fills one row of w pixels
template <typename Compose>
void streamRows(Services::ICanvas& lcd, int16_t x0, int16_t y0, int16_t w, int16_t rows, Compose compose)
{
    if (w <= 0 || rows <= 0) return;
    ScratchRow lease;                       // the Display task's shared rows
    uint8_t* const block = lease.data();
    const size_t rowBytes = static_cast<size_t>(w) * 2;
    lcd.streamBitmapStart(static_cast<uint16_t>(x0), static_cast<uint16_t>(y0), static_cast<uint16_t>(w),
                          static_cast<uint16_t>(rows));
    size_t k = 0;
    for (int16_t r = 0; r < rows; r++) {
        compose(static_cast<int16_t>(y0 + r), block + k * rowBytes);
        if (++k == ScratchRow::ROWS) {
            lcd.streamBitmapData(block, k * rowBytes);
            k = 0;
        }
    }
    if (k > 0) lcd.streamBitmapData(block, k * rowBytes);
    lcd.streamBitmapEnd();
}

// The title as last prepared (synthPrepareTitle): the box repaint and the band rows that
// cross it between repaints compose the same pixels from it
struct TitleFrame {
    int32_t sa = 0, ca = 65536, cx = 0, cy = 0;
    uint16_t face[3][TITLE_LINE_H] = {};
    uint16_t shadow[3] = {};
    uint16_t verFace[VER_H] = {};
};
TitleFrame s_tf;

} // namespace

void SaverScreen::synthNewScene()
{
    // Never the same palette, horizon or sun twice in a row
    m_palette = static_cast<uint8_t>((m_palette + 1 + nextRandom() % (PALETTE_COUNT - 1)) % PALETTE_COUNT);
    const Palette& P = PALETTES[m_palette];
    m_horizon = static_cast<int16_t>(HORIZON_MIN + nextRandom() % HORIZON_SPAN);   // 150..229
    m_sunR = static_cast<int16_t>(32 + nextRandom() % 22);               // 32..53
    m_sunX = static_cast<int16_t>(W / 2 - 50 + static_cast<int16_t>(nextRandom() % 101));
    m_city = (nextRandom() % 3) == 0;                                    // a skyline one time in three
    for (int16_t y = 0; y < m_horizon; y++) {
        const int t = y * 256 / m_horizon;
        m_sky[y] = (t < 150) ? mix(P.skyTop, P.skyMid, t * 256 / 150) : mix(P.skyMid, P.haze, (t - 150) * 256 / 106);
    }
    if (!m_city) {
        // Mountains: three hills of seeded phase, higher at the sides, a low pass behind the sun
        const int32_t p1 = static_cast<int32_t>(nextRandom() & 1023), p2 = static_cast<int32_t>(nextRandom() & 1023);
        const int32_t p3 = static_cast<int32_t>(nextRandom() & 1023);
        const int32_t scale = 70 + static_cast<int32_t>(nextRandom() % 60);   // 70..129 % height
        for (int16_t x = 0; x < W; x++) {
            int32_t h = 34 + sinApprox(x * 9 + p1, 22) + sinApprox(x * 23 + p2, 12) + sinApprox(x * 61 + p3, 5);
            const int32_t off = (x > m_sunX) ? (x - m_sunX) : (m_sunX - x);
            const int32_t near = off > 110 ? 110 : off;
            h = h * (24 + near * near * 232 / (110 * 110)) / 256 * scale / 100;
            h += static_cast<int32_t>(nextRandom() % 3);
            m_ridge[x] = static_cast<uint8_t>(h < 2 ? 2 : (h > 110 ? 110 : h));
            m_bldStart[x] = 0;
        }
    } else {
        // Skyline: buildings of random width and height, lower in front of the sun
        int16_t x = 0;
        while (x < W) {
            const int16_t w = static_cast<int16_t>(8 + nextRandom() % 18);
            const int32_t off = (x > m_sunX) ? (x - m_sunX) : (m_sunX - x);
            int32_t h = 18 + static_cast<int32_t>(nextRandom() % 70);
            if (off < m_sunR + 6) h = h / 3;                 // the sun shows over the low ones
            for (int16_t i = 0; i < w && x + i < W; i++) {
                m_ridge[x + i] = static_cast<uint8_t>(h > 110 ? 110 : h);
                m_bldStart[x + i] = static_cast<uint8_t>(x);
            }
            x = static_cast<int16_t>(x + w + (nextRandom() % 3 == 0 ? 2 : 0));   // the odd gap
        }
    }
    // Stars in the upper sky, away from the sun
    for (uint8_t i = 0; i < STAR_COUNT; i++) {
        uint8_t sx, sy;
        int guard = 0;
        do {
            sx = static_cast<uint8_t>(nextRandom() % W);
            sy = static_cast<uint8_t>(4 + nextRandom() % (m_horizon > 90 ? m_horizon - 80 : 10));
        } while (++guard < 20 && sx > m_sunX - m_sunR - 10 && sx < m_sunX + m_sunR + 10 &&
                 sy > m_horizon - 2 * m_sunR - 30);
        m_starX[i] = sx;
        m_starY[i] = sy;
    }
    // The title's place this scene (its box's top-left): centred give or take 10 px, high up
    m_titleX = static_cast<int16_t>(W / 2 - BOX_HW - 10 + static_cast<int16_t>(nextRandom() % 21));
    if (m_titleX < 0) m_titleX = 0;
    if (m_titleX + 2 * BOX_HW > W) m_titleX = static_cast<int16_t>(W - 2 * BOX_HW);
    m_titleY = static_cast<int16_t>(TITLE_Y_MIN + nextRandom() % TITLE_Y_SPAN);
    m_titleAlpha = 0;
}

uint16_t SaverScreen::synthUpper(int16_t x, int16_t y) const
{
    const Palette& P = PALETTES[m_palette];
    if (y == m_horizon) return P.horizon;
    const int16_t top = static_cast<int16_t>(m_horizon - m_ridge[x]);
    if (y >= top) {
        if (m_city) {
            // Buildings: neon outline, dark body, a grid of windows of which some are lit
            const int16_t bx = m_bldStart[x];
            const bool edge = (x == bx) || (x + 1 < W && m_bldStart[x + 1] != bx) || y == top;
            if (edge) return P.line;
            const int16_t wx = static_cast<int16_t>(x - bx), wy = static_cast<int16_t>(y - top);
            if ((wx % 4) == 2 && (wy % 5) == 2) {
                const uint32_t h = static_cast<uint32_t>(x * 73856093u) ^ static_cast<uint32_t>(y * 19349663u) ^ m_seedScene;
                if ((h >> 7) % 3 == 0) return P.window;
                return P.dim;
            }
            return P.fill;
        }
        const int16_t prevTop = static_cast<int16_t>(m_horizon - m_ridge[x > 0 ? x - 1 : x]);
        const int16_t nextTop = static_cast<int16_t>(m_horizon - m_ridge[x < W - 1 ? x + 1 : x]);
        const int16_t lo = top < prevTop ? (top < nextTop ? top : nextTop) : (prevTop < nextTop ? prevTop : nextTop);
        const int16_t hi = top > prevTop ? (top > nextTop ? top : nextTop) : (prevTop > nextTop ? prevTop : nextTop);
        if (y <= hi && y >= lo && y <= top + 1) return P.line;          // the ridge, gap-free on slopes
        const int16_t h = m_ridge[x];
        const int16_t depth = static_cast<int16_t>(y - top);
        for (int k = 1; k <= 3; k++) {
            if (depth == h * k / 4) return P.dim;                       // contours
        }
        if ((x % 10) == 0) return P.dim;                                // the mesh's verticals
        return P.fill;
    }
    // Sun with a glow, banded in its lower half
    const int16_t sunCY = static_cast<int16_t>(m_horizon - m_sunR / 2);
    const int32_t dx = x - m_sunX, dy = y - sunCY;
    const int32_t d2 = dx * dx + dy * dy;
    const uint16_t sky = m_sky[y];
    if (d2 <= static_cast<int32_t>(m_sunR) * m_sunR) {
        const int16_t rel = static_cast<int16_t>(y - (sunCY - m_sunR));
        const uint16_t c = mix(P.sunTop, P.sunBottom, rel * 256 / (2 * m_sunR));
        const int16_t bandStart = static_cast<int16_t>(sunCY - m_sunR / 5);
        if (y >= bandStart) {
            const int gap = 1 + (y - bandStart) / 10;                    // bands widen downward
            if (((y + m_stripe) % 9) < gap) return sky;
        }
        return c;
    }
    const int32_t glowR = m_sunR + 7;
    if (d2 <= glowR * glowR) {
        const int32_t edge = (d2 - static_cast<int32_t>(m_sunR) * m_sunR) * 256 / (glowR * glowR - m_sunR * m_sunR);
        return mix(P.glow, sky, static_cast<int>(edge));
    }
    return sky;
}

void SaverScreen::composeUpper(int16_t y, int16_t xa, int16_t xb, uint8_t* buf) const
{
    // By runs, not a scene call a pixel (bench 2026-10-21: the title's frames took ~200 ms, 5
    // frames/s): the row's sky colour, the sun's span (its disc one colour a row, the glow a
    // mix a pixel), the scene a pixel only where the mountains or buildings reach the row
    const Palette& P = PALETTES[m_palette];
    auto put = [&](int16_t x, uint16_t c) {
        buf[(x - xa) * 2] = static_cast<uint8_t>(c >> 8);
        buf[(x - xa) * 2 + 1] = static_cast<uint8_t>(c & 0xFF);
    };
    auto fill = [&](int16_t a, int16_t b, uint16_t c) {
        for (int16_t x = a < xa ? xa : a; x <= (b > xb ? xb : b); x++) put(x, c);
    };
    if (y == m_horizon) {
        fill(xa, xb, P.horizon);
    } else {
        const uint16_t sky = m_sky[y];
        fill(xa, xb, sky);
        const int16_t sunCY = static_cast<int16_t>(m_horizon - m_sunR / 2);
        const int32_t dy = y - sunCY;
        const int32_t r2 = static_cast<int32_t>(m_sunR) * m_sunR;
        const int32_t glowR = m_sunR + 7, g2 = glowR * glowR;
        if (dy * dy <= g2) {
            const int16_t gx = Graphics::isqrt16(g2 - dy * dy);
            const int16_t rel = static_cast<int16_t>(y - (sunCY - m_sunR));
            uint16_t disc = mix(P.sunTop, P.sunBottom, rel * 256 / (2 * m_sunR));
            const int16_t bandStart = static_cast<int16_t>(sunCY - m_sunR / 5);
            if (y >= bandStart) {
                const int gap = 1 + (y - bandStart) / 10;                // bands widen downward
                if (((y + m_stripe) % 9) < gap) disc = sky;
            }
            const int16_t a = static_cast<int16_t>(m_sunX - gx < xa ? xa : m_sunX - gx);
            const int16_t b = static_cast<int16_t>(m_sunX + gx > xb ? xb : m_sunX + gx);
            for (int16_t x = a; x <= b; x++) {
                const int32_t dx = x - m_sunX;
                const int32_t d2 = dx * dx + dy * dy;
                put(x, d2 <= r2 ? disc : mix(P.glow, sky, static_cast<int>((d2 - r2) * 256 / (g2 - r2))));
            }
        }
        for (int16_t x = xa; x <= xb; x++) {                             // mountains / buildings
            if (y >= m_horizon - m_ridge[x]) put(x, synthUpper(x, y));
        }
    }

    // The stars (not under the title's box while it shows: its repaint never had them)
    const bool titleUp = m_titleAlpha > 0;
    const bool inBoxRows = y >= m_titleY && y < m_titleY + 2 * BOX_HH;
    for (uint8_t i = 0; i < STAR_COUNT; i++) {
        if (m_starY[i] != y || m_starX[i] < xa || m_starX[i] > xb) continue;
        if (titleUp && inBoxRows && m_starX[i] >= m_titleX && m_starX[i] < m_titleX + 2 * BOX_HW) continue;
        put(m_starX[i], (i % 3) ? STAR_DIM : STAR);
    }

    // The title at m_titleAlpha, rocked and bobbed: each pixel inverse-rotated into the mask,
    // stepped along the row
    if (titleUp && inBoxRows) {
        const int16_t a = m_titleX < xa ? xa : m_titleX;
        const int16_t b = static_cast<int16_t>(m_titleX + 2 * BOX_HW - 1 > xb ? xb : m_titleX + 2 * BOX_HW - 1);
        const int32_t dy = y - s_tf.cy;
        int32_t mx = (MW / 2) * 65536 + (a - s_tf.cx) * s_tf.ca + dy * s_tf.sa;
        int32_t my = (MH / 2) * 65536 - (a - s_tf.cx) * s_tf.sa + dy * s_tf.ca;
        for (int16_t x = a; x <= b; x++, mx += s_tf.ca, my -= s_tf.sa) {
            const int16_t ix = static_cast<int16_t>((mx + 32768) >> 16), iy = static_cast<int16_t>((my + 32768) >> 16);
            const uint8_t m = maskAt(ix, iy);
            if (m == 0) continue;
            uint16_t c;
            const int16_t ty = static_cast<int16_t>(iy - 1);
            int16_t line = static_cast<int16_t>(ty / TITLE_PITCH);
            if (line < 0) line = 0;
            if (line > 2) line = 2;
            if (ty >= VER_TOP - 1) {                      // the version line, its outline included
                int16_t ly = static_cast<int16_t>(ty - VER_TOP);
                if (ly < 0) ly = 0;
                if (ly >= VER_H) ly = VER_H - 1;
                c = (m == 3) ? s_tf.verFace[ly] : (m == 2 ? VER_SHADOW_INK : TITLE_OUTLINE);
            } else if (m == 3) {
                int16_t ly = static_cast<int16_t>(ty - line * TITLE_PITCH);
                if (ly < 0) ly = 0;
                if (ly >= TITLE_LINE_H) ly = TITLE_LINE_H - 1;
                c = s_tf.face[line][ly];
            } else if (m == 2) {
                c = s_tf.shadow[line];
            } else {
                c = TITLE_OUTLINE;
            }
            if (m_titleAlpha < 256) {
                const uint16_t under = static_cast<uint16_t>((buf[(x - xa) * 2] << 8) | buf[(x - xa) * 2 + 1]);
                c = mix(under, c, m_titleAlpha);
            }
            put(x, c);
        }
    }
    m_vhs.row(y).apply(buf, xa, xa, xb);
}

void SaverScreen::synthUpperRows(Services::ICanvas& lcd, int16_t first, int16_t last)
{
    // Whole rows of the sky
    if (last > m_horizon) last = m_horizon;
    if (first < 0) first = 0;
    if (first > last) return;
    streamRows(lcd, 0, first, W, static_cast<int16_t>(last - first + 1),
               [&](int16_t y, uint8_t* buf) { composeUpper(y, 0, W - 1, buf); });
}

void SaverScreen::synthFullPaint(Services::ICanvas& lcd)
{
    lcd.waitForVSync();
    synthRows(lcd, 0, H - 1);
}

bool SaverScreen::floorLine(int16_t r, float phase) const
{
    if (r < MIN_LINE_ROW) return false;                              // crowded: no lines up there
    const float depth = GRID_K / static_cast<float>(r);
    const int n = static_cast<int>(depth + phase + 0.5f);
    const float off = GRID_K / (static_cast<float>(n) - phase) - static_cast<float>(r);
    return n > 0 && off > -0.5f && off <= 0.5f;
}

void SaverScreen::floorRow(int16_t y, uint8_t* row) const
{
    const Palette& P = PALETTES[m_palette];
    const int16_t rows = static_cast<int16_t>(H - m_horizon - 1);
    const int16_t r = static_cast<int16_t>(y - m_horizon);
    const uint16_t bg = mix(P.floorFar, P.floorNear, r * 256 / rows);
    const int fade = 80 + r * 176 / rows;                                // far lines dimmer
    const uint16_t line = mix(bg, P.grid, fade);
    const bool horizontal = floorLine(r, m_phase);
    for (int16_t x = 0; x < W; x++) {
        const uint16_t c = horizontal ? line : bg;
        row[x * 2] = static_cast<uint8_t>(c >> 8);
        row[x * 2 + 1] = static_cast<uint8_t>(c & 0xFF);
    }
    const float span = GRID_STEP * static_cast<float>(r) / static_cast<float>(rows > 0 ? rows : 1) *
                       (static_cast<float>(rows) / 119.0f);              // same spacing whatever the horizon
    if (!horizontal && span >= 7.0f) {
        for (int j = -14; j <= 14; j++) {
            const int x = static_cast<int>(W / 2 + j * span + (j >= 0 ? 0.5f : -0.5f));
            if (x >= 0 && x < W) {
                row[x * 2] = static_cast<uint8_t>(line >> 8);
                row[x * 2 + 1] = static_cast<uint8_t>(line & 0xFF);
            }
        }
    }
    m_vhs.row(y).apply(row, 0, 0, W - 1);
}

void SaverScreen::synthRows(Services::ICanvas& lcd, int16_t first, int16_t last)
{
    // Whole rows: the sky's above the horizon, the floor's below it
    if (first < 0) first = 0;
    if (last > H - 1) last = H - 1;
    if (first <= m_horizon) synthUpperRows(lcd, first, last < m_horizon ? last : m_horizon);
    const int16_t f = first > m_horizon ? first : static_cast<int16_t>(m_horizon + 1);
    if (f <= last) streamRows(lcd, 0, f, W, static_cast<int16_t>(last - f + 1), [&](int16_t y, uint8_t* buf) { floorRow(y, buf); });
}

void SaverScreen::synthSunBox(Services::ICanvas& lcd)
{
    const int16_t sunCY = static_cast<int16_t>(m_horizon - m_sunR / 2);
    int16_t y0 = static_cast<int16_t>(sunCY - m_sunR / 5);              // the banded part only
    int16_t x0 = static_cast<int16_t>(m_sunX - m_sunR);
    int16_t x1 = static_cast<int16_t>(m_sunX + m_sunR);
    if (x0 < 0) x0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    // Keep clear of the title while it shows (it repaints its own rows)
    if (m_titleAlpha > 0 && y0 < m_titleY + 2 * BOX_HH) y0 = static_cast<int16_t>(m_titleY + 2 * BOX_HH);
    if (y0 >= m_horizon) return;
    streamRows(lcd, x0, y0, static_cast<int16_t>(x1 - x0 + 1), static_cast<int16_t>(m_horizon - y0),
               [&](int16_t y, uint8_t* buf) { composeUpper(y, x0, x1, buf); });
}

void SaverScreen::synthPrepareTitle(uint16_t t)
{
    // This frame's angle (sin a ~ a, cos a ~ 1 - a^2/2, Q16) and bob, and the colours: each
    // line one step along the flash cycle, a vertical gradient on the faces
    buildTitleMask();
    s_tf.sa = ROCK_Q16 * sinApprox(static_cast<int32_t>(t) * 1024 / ROCK_FRAMES, 1024) / 1024;
    s_tf.ca = 65536 - (s_tf.sa * s_tf.sa) / (2 * 65536);
    const int16_t bob = sinApprox(static_cast<int32_t>(t) * 1024 / BOB_FRAMES + 256, BOB);
    s_tf.cx = m_titleX + BOX_HW;                          // where the mask's centre lands
    s_tf.cy = m_titleY + BOX_HH + bob;
    const uint8_t step = static_cast<uint8_t>((t / FLASH_FRAMES) % 3);
    for (int line = 0; line < 3; line++) {
        const TitleInk& k = TITLE_INKS[(step + line) % 3];
        s_tf.shadow[line] = k.shadow;
        for (int ly = 0; ly < TITLE_LINE_H; ly++) s_tf.face[line][ly] = mix(k.top, k.bottom, ly * 256 / (TITLE_LINE_H - 1));
    }
    for (int ly = 0; ly < VER_H; ly++) s_tf.verFace[ly] = mix(VER_TOP_INK, VER_BOTTOM_INK, ly * 256 / (VER_H - 1));
}

void SaverScreen::synthTitle(Services::ICanvas& lcd, uint16_t t)
{
    // The title's box: the scene underneath, the title over it at m_titleAlpha, the bands over
    // both. At alpha 0 it is just the scene, stars included.
    synthPrepareTitle(t);
    const int16_t x0 = m_titleX;
    const int16_t w = static_cast<int16_t>(2 * BOX_HW);
    streamRows(lcd, x0, m_titleY, w, static_cast<int16_t>(2 * BOX_HH),
               [&](int16_t y, uint8_t* buf) { composeUpper(y, x0, static_cast<int16_t>(x0 + w - 1), buf); });
}

void SaverScreen::synthTwinkle(Services::ICanvas& lcd)
{
    const Palette& P = PALETTES[m_palette];
    for (int k = 0; k < 3; k++) {
        const uint8_t i = static_cast<uint8_t>(nextRandom() % STAR_COUNT);
        if (m_vhs.covers(m_starY[i])) continue;           // a dot would punch through a band
        if (m_titleAlpha > 0 && m_starX[i] >= m_titleX && m_starX[i] < m_titleX + 2 * BOX_HW &&
            m_starY[i] >= m_titleY && m_starY[i] < m_titleY + 2 * BOX_HH) continue;
        const uint32_t r = nextRandom() % 4;
        const uint16_t c = (r == 0) ? m_sky[m_starY[i]] : (r == 1 ? STAR : (r == 2 ? STAR_DIM : P.glow));
        lcd.drawPixel(m_starX[i], m_starY[i], c);
    }
}

void SaverScreen::renderSynthwave(Services::ICanvas& lcd)
{
    m_vhsTicks++;                              // the bands' clock: 50 ms a frame
    if (!m_synthReady || m_frame >= SCENE_FRAMES) {
        m_seedScene = nextRandom();
        synthNewScene();
        m_vhs.reset();
        m_vhs.step(m_vhsTicks * 50u);
        synthFullPaint(lcd);
        m_synthReady = true;
        m_frame = 0;
        return;
    }
    m_frame++;
    m_vhs.step(m_vhsTicks * 50u);
    const float oldPhase = m_phase;
    m_phase += 0.05f;                          // the floor comes toward the viewer
    if (m_phase >= 1.0f) m_phase -= 1.0f;
    lcd.waitForVSync();
    // Once per scene the name fades in, rocks and flashes for a while, and fades out; its box
    // repaints every frame (it was every other frame, the sun's stripes taking the others: the
    // text looked choppy, owner 2026-10-21), and once more at the end to leave the sky clean.
    if (m_frame >= TITLE_START_FRAME && m_frame <= TITLE_START_FRAME + 2 * FADE_FRAMES + HOLD_TITLE_FRAMES) {
        const uint16_t t = static_cast<uint16_t>(m_frame - TITLE_START_FRAME);
        const uint16_t last = 2 * FADE_FRAMES + HOLD_TITLE_FRAMES;
        uint16_t a;
        if (t < FADE_FRAMES) a = static_cast<uint16_t>((t + 1) * 256 / FADE_FRAMES);
        else if (t < FADE_FRAMES + HOLD_TITLE_FRAMES) a = 256;
        else a = static_cast<uint16_t>((last - t) * 256 / FADE_FRAMES);
        {
            m_titleAlpha = a;
            synthTitle(lcd, t);
        }
    }
    if ((m_frame & 1) == 0) {
        m_stripe = static_cast<uint8_t>((m_stripe + 8) % 9);   // bands slide down one row
        synthSunBox(lcd);
    }
    // The floor rows a grid line left or reached, in runs (it was the whole floor every frame,
    // 40-80 KB: bench 2026-10-21, 8 frames/s while the title showed)
    const int16_t floorRows = static_cast<int16_t>(H - m_horizon - 1);
    int16_t runStart = -1;
    for (int16_t r = 1; r <= floorRows + 1; r++) {
        const bool changed = r <= floorRows && floorLine(r, oldPhase) != floorLine(r, m_phase);
        if (changed && runStart < 0) runStart = r;
        if (!changed && runStart >= 0) {
            synthRows(lcd, static_cast<int16_t>(m_horizon + runStart), static_cast<int16_t>(m_horizon + r - 1));
            runStart = -1;
        }
    }
    // The rows the bands left and reached, sky or floor
    m_vhs.dirtyRuns([&](int first, int last) {
        synthRows(lcd, static_cast<int16_t>(first), static_cast<int16_t>(last));
    });
    if ((m_frame % 6) == 0) synthTwinkle(lcd);
}

InputResult SaverScreen::handleInput(JoyDirection dir, bool pressed)
{
    (void)dir;
    return pressed ? InputResult::EXIT_SCREEN : InputResult::HANDLED;
}

} // namespace UI
