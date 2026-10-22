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

// The title as last prepared: the box repaint and the band rows that cross it compose the
// same pixels from it
struct TitleFrame {
    int32_t sa = 0, ca = 65536, cx = 0, cy = 0;
    int16_t x0 = 0, y0 = 0;
    uint16_t face[T_LINE_H] = {};
    uint16_t shadow = 0, outline = 0;
};
TitleFrame s_tf;
constexpr int16_t T_BOX_W = 2 * BOX_HW, T_BOX_H = 2 * BOX_HH;

} // namespace

void EstopScreen::prepareTitle()
{
    buildMask(m_fault ? "FAULT" : "E-STOP");
    s_tf.x0 = static_cast<int16_t>(Theme::WIDTH / 2 - BOX_HW + m_offX);
    s_tf.y0 = static_cast<int16_t>(TITLE_Y - 6 + m_offY);
    s_tf.sa = ROCK_Q16 * sinApprox(static_cast<int32_t>(m_tick) * 1024 / ROCK_FRAMES, 1024) / 1024;
    s_tf.ca = 65536 - (s_tf.sa * s_tf.sa) / (2 * 65536);
    const int16_t bob = sinApprox(static_cast<int32_t>(m_tick) * 1024 / BOB_FRAMES + 256, BOB);
    s_tf.cx = s_tf.x0 + BOX_HW;
    s_tf.cy = s_tf.y0 + BOX_HH + bob;
    const TitleInk& k = T_INKS[(m_tick / FLASH_FRAMES + m_pal) % 3];
    for (int ly = 0; ly < T_LINE_H; ly++) s_tf.face[ly] = ink(mix565(k.top, k.bottom, ly * 256 / (T_LINE_H - 1)));
    s_tf.shadow = ink(k.shadow);
    s_tf.outline = ink(T_OUTLINE);
}

bool EstopScreen::titlePixel(int16_t x, int16_t y, uint16_t& c) const
{
    // The pixel inverse-rotated into the mask; false where the page shows
    if (m_alpha == 0 || x < s_tf.x0 || x >= s_tf.x0 + T_BOX_W || y < s_tf.y0 || y >= s_tf.y0 + T_BOX_H) return false;
    const int32_t dx = x - s_tf.cx, dy = y - s_tf.cy;
    const int32_t mx = (MW / 2) * 65536 + dx * s_tf.ca + dy * s_tf.sa;
    const int32_t my = (MH / 2) * 65536 - dx * s_tf.sa + dy * s_tf.ca;
    const int16_t ix = static_cast<int16_t>((mx + 32768) >> 16), iy = static_cast<int16_t>((my + 32768) >> 16);
    const uint8_t m = maskAt(ix, iy);
    if (m == 0) return false;
    if (m == 3) {
        int16_t ly = static_cast<int16_t>(iy - 1);
        if (ly < 0) ly = 0;
        if (ly >= T_LINE_H) ly = T_LINE_H - 1;
        c = s_tf.face[ly];
    } else {
        c = (m == 2) ? s_tf.shadow : s_tf.outline;
    }
    return true;
}

void EstopScreen::drawTitle(Services::ICanvas& lcd)
{
    // The title's box, row by row: the letters, the page (and the bands) behind them
    prepareTitle();
    ScratchRow lease;                       // the Display task's shared row
    uint8_t* const row = lease.data();
    lcd.streamBitmapStart(static_cast<uint16_t>(s_tf.x0), static_cast<uint16_t>(s_tf.y0),
                          static_cast<uint16_t>(T_BOX_W), static_cast<uint16_t>(T_BOX_H));
    for (int16_t y = s_tf.y0; y < s_tf.y0 + T_BOX_H; y++) {
        const Graphics::VhsBands::Row band = m_vhs.row(y);
        for (int16_t x = 0; x < T_BOX_W; x++) {
            uint16_t c;
            if (!titlePixel(static_cast<int16_t>(s_tf.x0 + x), y, c)) c = band.pixel(s_tf.x0 + x, Theme::BG);
            row[x * 2] = static_cast<uint8_t>(c >> 8);
            row[x * 2 + 1] = static_cast<uint8_t>(c & 0xFF);
        }
        lcd.streamBitmapData(row, static_cast<size_t>(T_BOX_W) * 2);
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

void EstopScreen::stripeRow(uint16_t r, uint8_t* row) const
{
    // Diagonal neon-yellow / page stripes, 12 px wide, crawling one pixel per frame
    for (uint16_t x = 0; x < Theme::WIDTH; x++) {
        const bool on = (((x + r + m_stripePhase) / 12) & 1u) == 0;
        const uint16_t c = on ? Theme::YELLOW : Theme::BG;
        row[x * 2] = static_cast<uint8_t>(c >> 8);
        row[x * 2 + 1] = static_cast<uint8_t>(c & 0xFF);
    }
}

void EstopScreen::drawBands(Services::ICanvas& lcd)
{
    ScratchRow lease;                       // the Display task's shared row
    uint8_t* const row = lease.data();
    for (int band = 0; band < 2; band++) {
        const uint16_t y0 = band == 0 ? 0 : static_cast<uint16_t>(Theme::HEIGHT - BAND_H);
        lcd.streamBitmapStart(0, y0, Theme::WIDTH, BAND_H);
        for (uint16_t r = 0; r < BAND_H; r++) {
            stripeRow(r, row);
            m_vhs.row(y0 + r).apply(row, 0, 0, Theme::WIDTH - 1);
            lcd.streamBitmapData(row, ScratchRow::BYTES);
        }
        lcd.streamBitmapEnd();
    }
}

void EstopScreen::gridRow(uint16_t i, uint8_t* row) const
{
    // A grid like the saver's floor, scrolling toward the viewer; as a ceiling it is the same
    // grid upside down (its horizon along the bottom edge)
    constexpr float K = 60.0f;
    const uint16_t lineColour = PALETTES[m_pal].grid;
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
}

void EstopScreen::drawGrid(Services::ICanvas& lcd)
{
    ScratchRow lease;                       // the Display task's shared row
    uint8_t* const row = lease.data();
    const uint16_t y0 = m_gridTop ? GRID_CEIL_Y : GRID_FLOOR_Y;
    lcd.streamBitmapStart(0, y0, Theme::WIDTH, GRID_H);
    for (uint16_t i = 0; i < GRID_H; i++) {
        gridRow(i, row);
        m_vhs.row(y0 + i).apply(row, 0, 0, Theme::WIDTH - 1);
        lcd.streamBitmapData(row, ScratchRow::BYTES);
    }
    lcd.streamBitmapEnd();
}

bool EstopScreen::keepOut(int16_t y, int16_t& a, int16_t& b) const
{
    // The middle's drawn parts (rectangles and text, not row-composable): the subtitle, the
    // status panel, the prompt, the bar's frame, a message. They never share a row.
    if (m_alpha == 0) return false;                       // faded out: only the page
    const int16_t ox = m_offX, oy = m_offY;
    struct Box { int16_t x, y, w, h; };
    const Box boxes[5] = {
        {static_cast<int16_t>(Theme::MARGIN + ox), static_cast<int16_t>(82 + oy), static_cast<int16_t>(Theme::WIDTH - 2 * Theme::MARGIN - 6), 8},
        {static_cast<int16_t>(Theme::MARGIN + ox), static_cast<int16_t>(PANEL_Y + oy), static_cast<int16_t>(Theme::WIDTH - 2 * Theme::MARGIN), PANEL_H},
        {static_cast<int16_t>(28 + ox), static_cast<int16_t>(176 + oy), 23 * 8, 12},
        {static_cast<int16_t>(BAR_X - 4 + ox), static_cast<int16_t>(BAR_Y - 4 + oy), BAR_W + 8, BAR_H + 8},
        {static_cast<int16_t>(20 + ox), static_cast<int16_t>(224 + oy), 196, static_cast<int16_t>(m_drawnMessage[0] != '\0' ? 8 : 0)},
    };
    for (const Box& k : boxes) {
        if (y >= k.y && y < k.y + k.h) {
            a = k.x < 0 ? 0 : k.x;
            b = static_cast<int16_t>(k.x + k.w - 1 > Theme::WIDTH - 1 ? Theme::WIDTH - 1 : k.x + k.w - 1);
            return true;
        }
    }
    return false;
}

void EstopScreen::composeRow(int16_t y, uint8_t* row) const
{
    // What the page shows on row y: the hazard stripes and their rules, the grid, or the
    // page; the title's letters; the bands over all but the letters
    const uint16_t gridY = m_gridTop ? GRID_CEIL_Y : GRID_FLOOR_Y;
    if (y < BAND_H) {
        stripeRow(static_cast<uint16_t>(y), row);
    } else if (y >= Theme::HEIGHT - BAND_H) {
        stripeRow(static_cast<uint16_t>(y - (Theme::HEIGHT - BAND_H)), row);
    } else if (y >= gridY && y < gridY + GRID_H) {
        gridRow(static_cast<uint16_t>(y - gridY), row);
    } else {
        const bool rule = (y == BAND_H || y == BAND_H + 1 || y == Theme::HEIGHT - BAND_H - 2 || y == Theme::HEIGHT - BAND_H - 1);
        const uint16_t c = rule ? Theme::MAGENTA : Theme::BG;
        for (uint16_t x = 0; x < Theme::WIDTH; x++) {
            row[x * 2] = static_cast<uint8_t>(c >> 8);
            row[x * 2 + 1] = static_cast<uint8_t>(c & 0xFF);
        }
    }
    const Graphics::VhsBands::Row band = m_vhs.row(y);
    for (int16_t x = 0; x < Theme::WIDTH; x++) {
        uint16_t c;
        if (!titlePixel(x, y, c)) {
            if (!band.on) continue;
            c = band.pixel(x, static_cast<uint16_t>((row[x * 2] << 8) | row[x * 2 + 1]));
        }
        row[x * 2] = static_cast<uint8_t>(c >> 8);
        row[x * 2 + 1] = static_cast<uint8_t>(c & 0xFF);
    }
}

void EstopScreen::paintBandRows(Services::ICanvas& lcd, int16_t first, int16_t last)
{
    // Rows of the same keep-out go out as one window each side of it
    ScratchRow lease;                       // the Display task's shared row
    uint8_t* const row = lease.data();
    int16_t y = first;
    while (y <= last) {
        int16_t a = 0, b = -1;
        const bool kept = keepOut(y, a, b);
        int16_t end = y;
        while (end < last) {
            int16_t a2 = 0, b2 = -1;
            const bool k2 = keepOut(static_cast<int16_t>(end + 1), a2, b2);
            if (k2 != kept || a2 != a || b2 != b) break;
            end++;
        }
        const int16_t spans[2][2] = {{0, static_cast<int16_t>(kept ? a - 1 : Theme::WIDTH - 1)},
                                     {static_cast<int16_t>(kept ? b + 1 : Theme::WIDTH), static_cast<int16_t>(Theme::WIDTH - 1)}};
        for (const auto& s : spans) {
            if (s[0] > s[1]) continue;
            const uint16_t w = static_cast<uint16_t>(s[1] - s[0] + 1);
            lcd.streamBitmapStart(static_cast<uint16_t>(s[0]), static_cast<uint16_t>(y), w, static_cast<uint16_t>(end - y + 1));
            for (int16_t yy = y; yy <= end; yy++) {
                composeRow(yy, row);
                lcd.streamBitmapData(row + s[0] * 2, static_cast<size_t>(w) * 2);
            }
            lcd.streamBitmapEnd();
        }
        y = static_cast<int16_t>(end + 1);
    }
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
    m_vhs.reset();
    m_vhs.step(m_nowMs);
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
    m_vhs.step(m_nowMs);           // the bands follow the clock, whatever the frame rate
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
    // The rows the bands left and reached (the title as last drawn: prepared on its frames)
    m_vhs.dirtyRuns([&](int first, int last) {
        paintBandRows(lcd, static_cast<int16_t>(first), static_cast<int16_t>(last));
    });

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
