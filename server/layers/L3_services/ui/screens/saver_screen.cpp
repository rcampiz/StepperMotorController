/**
 * @file saver_screen.cpp
 * @brief Hotline Miami screen saver: stripe band, aberrated title card, VHS glitches.
 */

#include "ui/screens/saver_screen.hpp"
#include "ui/theme.hpp"
#include "harness/defs/harness_l3_l4.hpp"

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
// Synthwave scene
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

constexpr uint16_t SKY_TOP    = rgb(8, 2, 26);
constexpr uint16_t SKY_MID    = rgb(44, 8, 78);
constexpr uint16_t SKY_HAZE   = rgb(170, 30, 120);
constexpr uint16_t SUN_TOP    = rgb(255, 226, 96);
constexpr uint16_t SUN_BOTTOM = rgb(255, 40, 160);
constexpr uint16_t GLOW       = rgb(255, 70, 170);
constexpr uint16_t MOUNT_FILL = rgb(6, 12, 34);
constexpr uint16_t MOUNT_LINE = rgb(40, 236, 255);
constexpr uint16_t MOUNT_DIM  = rgb(18, 120, 160);
constexpr uint16_t HORIZON    = rgb(255, 110, 210);
constexpr uint16_t FLOOR_NEAR = rgb(8, 0, 20);
constexpr uint16_t FLOOR_FAR  = rgb(44, 0, 64);
constexpr uint16_t GRID       = rgb(255, 40, 210);
constexpr uint16_t STAR       = rgb(255, 255, 255);
constexpr uint16_t STAR_DIM   = rgb(150, 130, 210);

constexpr int16_t W = 240;
constexpr int16_t H = 320;
constexpr int16_t FLOOR_ROWS = H - SaverScreen::HORIZON_Y - 1;
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

} // namespace

void SaverScreen::synthNewScene()
{
    // Sky gradient: violet at the top, pink haze at the horizon
    for (int16_t y = 0; y < HORIZON_Y; y++) {
        const int t = y * 256 / HORIZON_Y;
        m_sky[y] = (t < 150) ? mix(SKY_TOP, SKY_MID, t * 256 / 150) : mix(SKY_MID, SKY_HAZE, (t - 150) * 256 / 106);
    }
    // The sun somewhere around the middle
    m_sunX = static_cast<int16_t>(W / 2 - 34 + static_cast<int16_t>(nextRandom() % 69));
    // Mountains: three hills of seeded phase, higher at the sides, a low pass behind the sun
    const int32_t p1 = static_cast<int32_t>(nextRandom() & 1023), p2 = static_cast<int32_t>(nextRandom() & 1023);
    const int32_t p3 = static_cast<int32_t>(nextRandom() & 1023);
    for (int16_t x = 0; x < W; x++) {
        int32_t h = 34 + sinApprox(x * 9 + p1, 22) + sinApprox(x * 23 + p2, 12) + sinApprox(x * 61 + p3, 5);
        const int32_t off = (x > m_sunX) ? (x - m_sunX) : (m_sunX - x);
        const int32_t near = off > 110 ? 110 : off;
        h = h * (24 + near * near * 232 / (110 * 110)) / 256;      // ~10 % behind the sun, 100 % far out
        h += static_cast<int32_t>(nextRandom() % 3);
        m_ridge[x] = static_cast<uint8_t>(h < 2 ? 2 : (h > 90 ? 90 : h));
    }
    // Stars in the upper sky, away from the sun
    for (uint8_t i = 0; i < STAR_COUNT; i++) {
        uint8_t sx, sy;
        do {
            sx = static_cast<uint8_t>(nextRandom() % W);
            sy = static_cast<uint8_t>(4 + nextRandom() % (HORIZON_Y - 70));
        } while (sx > m_sunX - SUN_R - 10 && sx < m_sunX + SUN_R + 10 && sy > HORIZON_Y - 2 * SUN_R - 30);
        m_starX[i] = sx;
        m_starY[i] = sy;
    }
}

uint16_t SaverScreen::synthUpper(int16_t x, int16_t y) const
{
    if (y == HORIZON_Y) return HORIZON;
    // Mountains in front of the sun: wireframe over a dark fill
    const int16_t top = static_cast<int16_t>(HORIZON_Y - m_ridge[x]);
    if (y >= top) {
        const int16_t prevTop = static_cast<int16_t>(HORIZON_Y - m_ridge[x > 0 ? x - 1 : x]);
        const int16_t nextTop = static_cast<int16_t>(HORIZON_Y - m_ridge[x < W - 1 ? x + 1 : x]);
        const int16_t lo = top < prevTop ? (top < nextTop ? top : nextTop) : (prevTop < nextTop ? prevTop : nextTop);
        const int16_t hi = top > prevTop ? (top > nextTop ? top : nextTop) : (prevTop > nextTop ? prevTop : nextTop);
        if (y <= hi && y >= lo && y <= top + 1) return MOUNT_LINE;   // the ridge, gap-free on slopes
        const int16_t h = m_ridge[x];
        const int16_t depth = static_cast<int16_t>(y - top);          // 0 at the ridge
        // Contours at a quarter, half and three quarters of the local height
        for (int k = 1; k <= 3; k++) {
            if (depth == h * k / 4) return MOUNT_DIM;
        }
        if ((x % 10) == 0) return MOUNT_DIM;                          // the mesh's verticals
        return MOUNT_FILL;
    }
    // Sun with a glow, banded in its lower half
    const int16_t sunCY = static_cast<int16_t>(HORIZON_Y - SUN_R / 2);
    const int32_t dx = x - m_sunX, dy = y - sunCY;
    const int32_t d2 = dx * dx + dy * dy;
    const uint16_t sky = m_sky[y];
    if (d2 <= static_cast<int32_t>(SUN_R) * SUN_R) {
        const int16_t rel = static_cast<int16_t>(y - (sunCY - SUN_R));          // 0 at the top
        const uint16_t c = mix(SUN_TOP, SUN_BOTTOM, rel * 256 / (2 * SUN_R));
        const int16_t bandStart = static_cast<int16_t>(sunCY - SUN_R / 5);
        if (y >= bandStart) {
            const int16_t below = static_cast<int16_t>(y - bandStart);
            const int gap = 1 + below / 10;                                     // bands widen downward
            if (((y + m_stripe) % 9) < gap) return sky;
        }
        return c;
    }
    const int32_t glowR = SUN_R + 7;
    if (d2 <= glowR * glowR) {
        const int32_t edge = (d2 - static_cast<int32_t>(SUN_R) * SUN_R) * 256 / (glowR * glowR - SUN_R * SUN_R);
        return mix(GLOW, sky, static_cast<int>(edge));
    }
    return sky;
}

void SaverScreen::synthFullPaint(Services::ICanvas& lcd)
{
    static uint8_t row[W * 2];
    lcd.waitForVSync();
    lcd.streamBitmapStart(0, 0, W, HORIZON_Y + 1);
    for (int16_t y = 0; y <= HORIZON_Y; y++) {
        for (int16_t x = 0; x < W; x++) {
            const uint16_t c = synthUpper(x, y);
            row[x * 2] = static_cast<uint8_t>(c >> 8);
            row[x * 2 + 1] = static_cast<uint8_t>(c & 0xFF);
        }
        lcd.streamBitmapData(row, sizeof row);
    }
    lcd.streamBitmapEnd();
    for (uint8_t i = 0; i < STAR_COUNT; i++) {
        lcd.drawPixel(m_starX[i], m_starY[i], (i % 3) ? STAR_DIM : STAR);
    }
    synthFloor(lcd);
}

void SaverScreen::synthFloor(Services::ICanvas& lcd)
{
    static uint8_t row[W * 2];
    lcd.streamBitmapStart(0, HORIZON_Y + 1, W, FLOOR_ROWS);
    for (int16_t r = 1; r <= FLOOR_ROWS; r++) {
        // Background: lighter toward the horizon
        const uint16_t bg = mix(FLOOR_FAR, FLOOR_NEAR, r * 256 / FLOOR_ROWS);
        const int fade = 80 + r * 176 / FLOOR_ROWS;                             // far lines dimmer
        const uint16_t line = mix(bg, GRID, fade);
        // Horizontal line on this row? Lines sit at depth n - phase
        const float depth = GRID_K / static_cast<float>(r);
        const int n = static_cast<int>(depth + m_phase + 0.5f);
        const float lineRow = GRID_K / (static_cast<float>(n) - m_phase);
        const float off = lineRow - static_cast<float>(r);
        const bool crowded = r < MIN_LINE_ROW;                               // lines < 2 px apart
        const bool horizontal = !crowded && (off > -0.5f && off <= 0.5f) && n > 0;
        for (int16_t x = 0; x < W; x++) {
            const uint16_t c = horizontal ? line : bg;
            row[x * 2] = static_cast<uint8_t>(c >> 8);
            row[x * 2 + 1] = static_cast<uint8_t>(c & 0xFF);
        }
        const float span = GRID_STEP * static_cast<float>(r) / static_cast<float>(FLOOR_ROWS);
        if (!horizontal && span >= 7.0f) {
            // Verticals converge on the vanishing point at the horizon's centre
            for (int j = -14; j <= 14; j++) {
                const int x = static_cast<int>(W / 2 + j * span + (j >= 0 ? 0.5f : -0.5f));
                if (x >= 0 && x < W) {
                    row[x * 2] = static_cast<uint8_t>(line >> 8);
                    row[x * 2 + 1] = static_cast<uint8_t>(line & 0xFF);
                }
            }
        }
        lcd.streamBitmapData(row, sizeof row);
    }
    lcd.streamBitmapEnd();
}

void SaverScreen::synthSunBox(Services::ICanvas& lcd)
{
    static uint8_t row[W * 2];
    const int16_t sunCY = static_cast<int16_t>(HORIZON_Y - SUN_R / 2);
    const int16_t y0 = static_cast<int16_t>(sunCY - SUN_R / 5);              // the banded part only
    int16_t x0 = static_cast<int16_t>(m_sunX - SUN_R);
    int16_t x1 = static_cast<int16_t>(m_sunX + SUN_R);
    if (x0 < 0) x0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    const uint16_t w = static_cast<uint16_t>(x1 - x0 + 1);
    lcd.streamBitmapStart(static_cast<uint16_t>(x0), static_cast<uint16_t>(y0), w,
                          static_cast<uint16_t>(HORIZON_Y - y0));
    for (int16_t y = y0; y < HORIZON_Y; y++) {
        for (int16_t x = x0; x <= x1; x++) {
            const uint16_t c = synthUpper(x, y);
            row[(x - x0) * 2] = static_cast<uint8_t>(c >> 8);
            row[(x - x0) * 2 + 1] = static_cast<uint8_t>(c & 0xFF);
        }
        lcd.streamBitmapData(row, static_cast<size_t>(w) * 2);
    }
    lcd.streamBitmapEnd();
}

void SaverScreen::synthTwinkle(Services::ICanvas& lcd)
{
    for (int k = 0; k < 3; k++) {
        const uint8_t i = static_cast<uint8_t>(nextRandom() % STAR_COUNT);
        const uint32_t r = nextRandom() % 4;
        const uint16_t c = (r == 0) ? m_sky[m_starY[i]] : (r == 1 ? STAR : (r == 2 ? STAR_DIM : GLOW));
        lcd.drawPixel(m_starX[i], m_starY[i], c);
    }
}

void SaverScreen::renderSynthwave(Services::ICanvas& lcd)
{
    if (!m_synthReady || m_frame >= SCENE_FRAMES) {
        synthNewScene();
        synthFullPaint(lcd);
        m_synthReady = true;
        m_frame = 0;
        return;
    }
    m_frame++;
    m_phase += 0.05f;                          // the floor comes toward the viewer
    if (m_phase >= 1.0f) m_phase -= 1.0f;
    lcd.waitForVSync();
    if ((m_frame & 1) == 0) {
        m_stripe = static_cast<uint8_t>((m_stripe + 8) % 9);   // bands slide down one row
        synthSunBox(lcd);
    }
    synthFloor(lcd);
    if ((m_frame % 6) == 0) synthTwinkle(lcd);
}

InputResult SaverScreen::handleInput(JoyDirection dir, bool pressed)
{
    (void)dir;
    return pressed ? InputResult::EXIT_SCREEN : InputResult::HANDLED;
}

} // namespace UI
