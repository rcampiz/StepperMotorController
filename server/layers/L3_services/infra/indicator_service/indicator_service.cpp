/**
 * @file indicator_service.cpp
 * @brief Motion indicator: eased target, one row composer, repaint of what changed only.
 */

#include "L3_services/infra/indicator_service/indicator_service.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "graphics/trig_lut/trig_lut.hpp"
#include "graphics/primitives/primitives.hpp"
#include "ui/display_scratch.hpp"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace Services {

IndicatorService g_indicatorService;

namespace {

constexpr float EASE = 0.45f;                // share of the remaining distance per 50 ms
constexpr float EASE_PERIOD_MS = 50.0f;      // ...applied for the time that actually passed: the
                                             // Display task runs ~5 frames/s while the arrow moves
                                             // (bench 2026-10-20), and per-frame easing lagged

// Arrow, unrotated, heading up (y grows downward), around the centre
constexpr float HALF_LEN = 55.0f;
constexpr float HEAD_LEN = 26.0f;
constexpr float HEAD_HALF_W = 18.0f;
constexpr float SHAFT_HALF_W = 5.0f;
constexpr float OUTLINE = 2.0f;

constexpr int SCREEN_W = Services::Canvas::WIDTH;
constexpr int SCREEN_H = Services::Canvas::HEIGHT;

// The VHS tracking bands: rows, speed (px/s, downward), top row at t = 0, tear (px either way)
constexpr int BAND_H[IndicatorService::BAND_COUNT] = {12, 6};
constexpr uint32_t BAND_SPEED[IndicatorService::BAND_COUNT] = {24, 36};   // was 55, 90: too fast (owner 2026-10-21)
constexpr uint32_t BAND_START[IndicatorService::BAND_COUNT] = {30, 200};
constexpr int BAND_TEAR[IndicatorService::BAND_COUNT] = {9, 5};
constexpr int BAND_STRIPE = 7;               // px per colour bar inside a band

// The backdrop's colours (8-bit; packed per row, the odd rows a scanline darker)
struct Rgb { int r, g, b; };
constexpr Rgb VHS_LO = {20, 0, 40};
constexpr Rgb VHS_HI = {70, 10, 100};
constexpr Rgb SPECKLE = {200, 160, 255};
constexpr Rgb BAND_CYAN = {5, 217, 232};
constexpr Rgb BAND_HOT = {255, 0, 128};
constexpr Rgb TRACK_LINE = {235, 220, 255};
constexpr Rgb NOISE = {230, 230, 255};       // not white: the arrow's outline is
constexpr int BAND_MIX = 115;                // 256ths of the bar colour over the backdrop
constexpr int SPECKLE_PER_ROW = 7;           // ~3 % of the backdrop's pixels
constexpr int NOISE_PER_ROW = 29;            // ~12 % of a band's

const char HINT_TEXT[] = "hold CENTER 2s: menu";
static_assert(sizeof HINT_TEXT - 1 == IndicatorService::HINT_CHARS, "the hint plate fits the hint");

inline Rgb mix(Rgb a, Rgb b, int q8) {
    return {a.r + (b.r - a.r) * q8 / 256, a.g + (b.g - a.g) * q8 / 256, a.b + (b.b - a.b) * q8 / 256};
}

inline uint16_t pack565(Rgb c, bool scanline) {
    if (scanline) c = {c.r * 205 / 256, c.g * 205 / 256, c.b * 205 / 256};
    return indicatorRgb(static_cast<uint8_t>(c.r), static_cast<uint8_t>(c.g), static_cast<uint8_t>(c.b));
}

/// 16 bits of noise for a pixel and a seed: integer only, a few cycles
inline uint32_t hash16(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t v = (x * 73856093u) ^ (y * 19349663u) ^ (seed * 83492791u);
    v = (v ^ (v >> 13)) * 0x5BD1E995u;
    return (v ^ (v >> 15)) & 0xFFFFu;
}

inline int floorToInt(float v) {
    int i = static_cast<int>(v);
    return (static_cast<float>(i) > v) ? i - 1 : i;
}

/// x-extent of a convex polygon on the horizontal line y (pixel centres covered: [x0, x1])
bool polySpan(const float (*v)[2], int n, float y, int& x0, int& x1) {
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < n; i++) {
        const float* a = v[i];
        const float* b = v[(i + 1) % n];
        if ((a[1] <= y && y < b[1]) || (b[1] <= y && y < a[1])) {
            float x = a[0] + (y - a[1]) * (b[0] - a[0]) / (b[1] - a[1]);
            if (x < lo) lo = x;
            if (x > hi) hi = x;
        }
    }
    if (hi < lo) return false;
    x0 = floorToInt(lo - 0.5f) + 1;          // first pixel whose centre is >= lo
    x1 = floorToInt(hi - 0.5f);              // last pixel whose centre is <= hi
    return x1 >= x0;
}

inline float sinDeg1024(uint16_t a) { return static_cast<float>(Graphics::sin_q15(a)) / 32768.0f; }
inline float cosDeg1024(uint16_t a) { return static_cast<float>(Graphics::cos_q15(a)) / 32768.0f; }

inline float wrapDiff(float d) {             // into (-180, 180]
    while (d > 180.0f) d -= 360.0f;
    while (d <= -180.0f) d += 360.0f;
    return d;
}

inline float ease(float cur, float target, float k) {
    float d = target - cur;
    if (d < 0.5f && d > -0.5f) return target;
    return cur + d * k;
}

struct Row {                                 // one row's spans for a shape set
    int x0, x1;
    bool on;
};

} // namespace

// ------------------------------------------------------------------------------------------
uint32_t IndicatorService::pack(const Params& p) {
    uint32_t angle = p.angle_deg % 360u;
    uint32_t dir = static_cast<uint32_t>(p.rotation_dir + 1) & 0x3u;
    uint32_t sp = p.speed_pct > 100 ? 100u : p.speed_pct;
    uint32_t rt = p.rot_pct > 100 ? 100u : p.rot_pct;
    return (1u << 31) | (angle) | (dir << 9) | ((p.has_translation ? 1u : 0u) << 11) | (sp << 12) | (rt << 19);
}

void IndicatorService::init(Services::ICanvas* lcd) {
    m_lcd = lcd;
}

void IndicatorService::draw(const Params& p) {
    m_target = pack(p);
}

void IndicatorService::clear() {
    draw({0, 0, false, 0, 0});
}

void IndicatorService::invalidate() {
    m_screenValid = false;
}

// ------------------------------------------------------------------------------------------
void IndicatorService::tick(uint32_t nowMs) {
    const uint32_t t = m_target;
    if (m_lcd == nullptr || (t & (1u << 31)) == 0) return;      // nothing requested yet
    m_synced = false;

    // The share of the remaining distance covered: EASE per 50 ms, for the time since the last
    // tick (bounded: a long gap simply arrives)
    uint32_t dt = m_ticked ? nowMs - m_lastTickMs : static_cast<uint32_t>(EASE_PERIOD_MS);
    m_lastTickMs = nowMs;
    m_ticked = true;
    if (dt > 500u) dt = 500u;
    const float k = 1.0f - powf(1.0f - EASE, static_cast<float>(dt) / EASE_PERIOD_MS);

    const auto tAngle = static_cast<float>(t & 0x1FFu);
    const int8_t tDir = static_cast<int8_t>(((t >> 9) & 0x3u)) - 1;
    const bool tTrans = ((t >> 11) & 1u) != 0;
    const auto tSpeed = static_cast<float>(tTrans ? ((t >> 12) & 0x7Fu) : 0u);
    const auto tTurn = static_cast<float>(tDir != 0 ? ((t >> 19) & 0x7Fu) : 0u);

    // The arrow appears pointing where the base goes (it swung round from up, the angle of the
    // stopped base: owner 2026-10-20); once it shows, it eases toward a new heading
    if (tTrans && !m_arrowShown) m_angle = tAngle;

    // Ease toward the target; the angle the short way round
    float d = wrapDiff(tAngle - m_angle);
    m_angle = (d < 0.5f && d > -0.5f) ? tAngle : m_angle + d * k;
    if (m_angle >= 360.0f) m_angle -= 360.0f;
    if (m_angle < 0.0f) m_angle += 360.0f;
    m_speed = ease(m_speed, tSpeed, k);
    m_turn = ease(m_turn, tTurn, k);

    const bool opened = !m_screenValid;
    if (opened) {
        m_arrowShown = m_ringsShown = false;
        m_arrowAngle = m_arrowFill = -1;
        memset(m_ringW, 0, sizeof m_ringW);
        memset(m_ringLit, 0, sizeof m_ringLit);
        m_chevDir = 0;
        m_readout[0] = '\0';
    }

    // What the panel should show now
    const bool arrowShown = tTrans;
    const bool ringsShown = tTrans || tDir != 0;
    auto angle1024 = static_cast<int16_t>(static_cast<int32_t>(m_angle * 1024.0f / 360.0f + 0.5f) & 0x3FF);
    auto fillPx = static_cast<int16_t>(m_speed * (2.0f * HALF_LEN) / 100.0f + 0.5f);

    uint8_t widths[RING_COUNT];
    bool lit[RING_COUNT];
    const float step = 100.0f / RING_COUNT;
    for (uint8_t i = 0; i < RING_COUNT; i++) {
        float into = m_turn - step * i;                         // how far into ring i's third
        if (into >= step - 0.25f)   { widths[i] = RING_W_MAX; lit[i] = true; }
        else if (into > 0.25f)      { widths[i] = static_cast<uint8_t>(1 + (into / step) * (RING_W_MAX - 1) + 0.5f); lit[i] = true; }
        else                        { widths[i] = 1; lit[i] = false; }
    }
    const int8_t chevDir = ringsShown ? tDir : 0;

    const bool arrowDirty = arrowShown != m_arrowShown ||
                            (arrowShown && (angle1024 != m_arrowAngle || fillPx != m_arrowFill));
    bool ringsDirty = ringsShown != m_ringsShown || chevDir != m_chevDir;
    for (uint8_t i = 0; i < RING_COUNT && !ringsDirty; i++) {
        ringsDirty = ringsShown && (widths[i] != m_ringW[i] || lit[i] != m_ringLit[i]);
    }

    // The bands move on the clock, and everything painted this tick shows them where they are
    // now; a tick that repaints the dial's square right after a late one (within
    // BAND_MAX_WAIT_MS, one Display period) leaves them where they are: the arrow first
    const bool squareDirty = arrowDirty || ringsDirty;
    const bool stepBands = opened || !squareDirty || (nowMs - m_bandStepMs >= BAND_MAX_WAIT_MS);
    int16_t oldTop[BAND_COUNT];
    for (uint8_t i = 0; i < BAND_COUNT; i++) {
        oldTop[i] = m_bandTop[i];
        if (stepBands) {
            m_bandTop[i] = static_cast<int16_t>((BAND_START[i] + static_cast<uint64_t>(nowMs) * BAND_SPEED[i] / 1000u) %
                                                static_cast<uint32_t>(SCREEN_H));
        }
    }
    if (stepBands) {
        m_bandFrame++;
        m_bandStepMs = nowMs;
    }

    if (opened) {
        paintRows(0, SCREEN_H - 1, 0, SCREEN_W - 1);              // the backdrop, the dial, the plates
        m_screenValid = true;
    }

    if (squareDirty) {
        if (arrowShown) buildArrow(static_cast<uint16_t>(angle1024), static_cast<float>(fillPx));
        if (ringsShown) { buildRings(widths); buildChevrons(chevDir); }
        // The state the frame draws (the lit rings and the chevrons were the previous frame's)
        m_arrowShown = arrowShown;
        m_arrowAngle = angle1024;
        m_arrowFill = fillPx;
        m_ringsShown = ringsShown;
        memcpy(m_ringW, widths, sizeof m_ringW);
        memcpy(m_ringLit, lit, sizeof m_ringLit);
        m_chevDir = chevDir;
        // One window: the square around what changed (the arrow disc lies inside either). It was
        // a window per span, ~380 small transfers a frame, ~148 ms on the bench (2026-10-20)
        const int half = ringsDirty ? ZONE_OUT : ARROW_R;
        paintRows(CY - half, CY + half, CX - half, CX + half);
    }
    renderReadout(arrowShown ? static_cast<int16_t>(m_speed + 0.5f) : -1,
                  tDir != 0 ? static_cast<int16_t>(m_turn + 0.5f) : -1, tDir);

    // The bands: the rows each left and the rows it reached (its noise re-rolls every step)
    if (stepBands && !opened) {
        for (uint8_t i = 0; i < BAND_COUNT; i++) {
            const int moved = (m_bandTop[i] - oldTop[i] + SCREEN_H) % SCREEN_H;
            if (oldTop[i] < 0) {
                paintBandRows(m_bandTop[i], BAND_H[i]);
            } else if (moved <= BAND_H[i]) {
                paintBandRows(oldTop[i], moved + BAND_H[i]);
            } else {
                paintBandRows(oldTop[i], BAND_H[i]);
                paintBandRows(m_bandTop[i], BAND_H[i]);
            }
        }
    }
}

// ------------------------------------------------------------------------------------------
void IndicatorService::buildArrow(uint16_t a, float fillLen) {
    const float s = sinDeg1024(a), c = cosDeg1024(a);
    auto rot = [&](float x, float y, float* out) {             // clockwise in screen coordinates
        out[0] = CX + x * c - y * s;
        out[1] = CY + x * s + y * c;
    };
    const float baseY = -HALF_LEN + HEAD_LEN;
    rot(0.0f, -HALF_LEN, m_outerHead[0]);
    rot(HEAD_HALF_W, baseY, m_outerHead[1]);
    rot(-HEAD_HALF_W, baseY, m_outerHead[2]);
    rot(-SHAFT_HALF_W, baseY - 2.0f, m_outerShaft[0]);
    rot(SHAFT_HALF_W, baseY - 2.0f, m_outerShaft[1]);
    rot(SHAFT_HALF_W, HALF_LEN, m_outerShaft[2]);
    rot(-SHAFT_HALF_W, HALF_LEN, m_outerShaft[3]);
    // Inner shapes: the outline is what lies between outer and inner
    rot(0.0f, -HALF_LEN + 2.5f * OUTLINE, m_innerHead[0]);
    rot(HEAD_HALF_W - 2.5f * OUTLINE, baseY - OUTLINE, m_innerHead[1]);
    rot(-(HEAD_HALF_W - 2.5f * OUTLINE), baseY - OUTLINE, m_innerHead[2]);
    rot(-(SHAFT_HALF_W - OUTLINE), baseY - OUTLINE - 2.0f, m_innerShaft[0]);
    rot(SHAFT_HALF_W - OUTLINE, baseY - OUTLINE - 2.0f, m_innerShaft[1]);
    rot(SHAFT_HALF_W - OUTLINE, HALF_LEN - OUTLINE, m_innerShaft[2]);
    rot(-(SHAFT_HALF_W - OUTLINE), HALF_LEN - OUTLINE, m_innerShaft[3]);
    // Fill axis: from the tail toward the tip
    m_tailX = CX - HALF_LEN * s;                                // rot(0, HALF_LEN)
    m_tailY = CY + HALF_LEN * c;
    m_axisX = s;                                                 // rot of (0,-1)
    m_axisY = -c;
    m_fillLen = fillLen;
}

void IndicatorService::buildRings(const uint8_t* widths) {
    for (uint8_t i = 0; i < RING_COUNT; i++) {
        int32_t ro = RING_OUT_R - i * RING_PITCH;
        int32_t ri = ro - widths[i];
        m_ringOut2[i] = ro * ro;
        m_ringIn2[i] = ri * ri;
        m_ringGlow2[i] = (widths[i] >= 3) ? (ri + 1) * (ri + 1) : ri * ri;   // a 1 px line once wide
    }
}

void IndicatorService::buildChevrons(int8_t dir) {
    if (dir == 0) return;
    const float mid = (RING_OUT_R + RING_IN_R) / 2.0f;              // the middle of the ring band
    for (uint8_t k = 0; k < 4; k++) {
        const uint16_t a = static_cast<uint16_t>(k * 256);      // 0, 90, 180, 270 degrees
        const float s = sinDeg1024(a), c = cosDeg1024(a);
        const float cx = CX + mid * s, cy = CY - mid * c;       // on the ring, clockwise from up
        const float rx = s, ry = -c;                            // radial, outward
        const float tx = (dir > 0) ? c : -c, ty = (dir > 0) ? s : -s;   // tangent, turn direction
        m_chev[k][0][0] = cx + tx * CHEV_LEN;           m_chev[k][0][1] = cy + ty * CHEV_LEN;
        m_chev[k][1][0] = cx + rx * (CHEV_BASE / 2.0f); m_chev[k][1][1] = cy + ry * (CHEV_BASE / 2.0f);
        m_chev[k][2][0] = cx - rx * (CHEV_BASE / 2.0f); m_chev[k][2][1] = cy - ry * (CHEV_BASE / 2.0f);
    }
}

// ------------------------------------------------------------------------------------------
void IndicatorService::sync() {
    if (!m_synced) {
        m_lcd->waitForVSync();                                  // the tick's first write starts on the tearing edge
        m_synced = true;
    }
}

void IndicatorService::composeRow(int y, int x0, int x1, uint8_t* buf) const {
    // The panel takes each pixel high byte first: one 16-bit store of the swapped colour (the
    // buffer is 4-aligned and a row is a whole number of pixels)
    uint16_t* const px = reinterpret_cast<uint16_t*>(buf) - x0;            // px[x]
    auto swapped = [](uint16_t c) { return static_cast<uint16_t>((c >> 8) | (c << 8)); };
    auto set = [&](int x, uint16_t colour) { px[x] = swapped(colour); };
    auto fill = [&](int a, int b, uint16_t colour) {
        if (a < x0) a = x0;
        if (b > x1) b = x1;
        const uint16_t v = swapped(colour);
        for (int x = a; x <= b; x++) px[x] = v;
    };
    const int dy = y - CY;
    const int32_t dy2 = static_cast<int32_t>(dy) * dy;

    // The inset this row: the disc, or a plate (they never share a row); the bands pass behind
    const int32_t disc2 = static_cast<int32_t>(DISC_R) * DISC_R;
    const int32_t zoneOut2 = static_cast<int32_t>(ZONE_OUT) * ZONE_OUT;
    int ia = SCREEN_W, ib = SCREEN_W - 1;                       // none
    if (dy2 <= disc2) {
        const int xe = Graphics::isqrt16(disc2 - dy2);
        ia = CX - xe;
        ib = CX + xe;
    } else if (y >= READOUT_T && y <= READOUT_B) {
        ia = READOUT_L; ib = READOUT_R;
    } else if (y >= HINT_T && y <= HINT_B) {
        ia = HINT_L; ib = HINT_R;
    }

    // --- the backdrop, either side of the inset ---------------------------------------------
    const bool scan = (y & 1) != 0;
    const int wave = (Graphics::sin_q15(static_cast<uint16_t>((y * 18) & 0x3FF)) + 32768) * 154 / 65536;  // 0..0.6
    const Rgb base = mix(VHS_LO, VHS_HI, wave);
    const uint16_t cBase = pack565(base, scan);
    const uint16_t cSpeck = pack565(SPECKLE, scan);
    int band = -1, r = 0;
    for (int i = 0; i < BAND_COUNT && band < 0; i++) {
        if (m_bandTop[i] < 0) continue;
        const int rr = (y - m_bandTop[i] + SCREEN_H) % SCREEN_H;
        if (rr < BAND_H[i]) { band = i; r = rr; }
    }
    auto backdrop = [&](int a, int b) {
        if (a < x0) a = x0;
        if (b > x1) b = x1;
        if (a > b) return;
        // The speckle and the noise: so many dots a row at hashed columns (a hash a dot, not a
        // hash a pixel); a pure function of the pixel, so overlapping repaints agree
        auto scatter = [&](int count, uint32_t seed, uint16_t colour) {
            const uint16_t v = swapped(colour);
            for (int k = 0; k < count; k++) {
                const int x = static_cast<int>(hash16(static_cast<uint32_t>(k), static_cast<uint32_t>(y), seed) % SCREEN_W);
                if (x >= a && x <= b) px[x] = v;
            }
        };
        if (band < 0) {
            fill(a, b, cBase);
            scatter(SPECKLE_PER_ROW, 0, cSpeck);
            return;
        }
        if (r == 0) {                                           // the bright tracking line
            fill(a, b, pack565(TRACK_LINE, scan));
            return;
        }
        const uint16_t sBar[2] = {swapped(pack565(mix(base, BAND_HOT, BAND_MIX), scan)),
                                  swapped(pack565(mix(base, BAND_CYAN, BAND_MIX), scan))};
        const int tear = BAND_TEAR[band];
        const int shift = static_cast<int>(hash16(static_cast<uint32_t>(r), m_bandFrame, static_cast<uint32_t>(band + 1)) %
                                           static_cast<uint32_t>(2 * tear + 1)) - tear + 10 * BAND_STRIPE;
        int bar = ((a + shift) / BAND_STRIPE) & 1;              // the colour bars, a run at a time
        int x = a;
        int run = BAND_STRIPE - (a + shift) % BAND_STRIPE;
        while (x <= b) {
            const int end = (x + run - 1 < b) ? x + run - 1 : b;
            for (const uint16_t v = sBar[bar]; x <= end; x++) px[x] = v;
            bar ^= 1;
            run = BAND_STRIPE;
        }
        scatter(NOISE_PER_ROW, m_bandFrame + 7u, pack565(NOISE, scan));
    };
    backdrop(x0, ia - 1);
    backdrop(ib + 1, x1);
    if (ia > ib || ib < x0 || ia > x1) return;

    // --- a plate: a dark panel with text in the medium font ---------------------------------
    auto text = [&](const char* s, int n, int tx, int ty, uint16_t fg, bool shadow) {
        const int gy = y - ty;
        for (int c = 0; c < n && s[c] != '\0'; c++) {
            const uint8_t* g = m_lcd->mediumGlyphRows(s[c]);
            if (g == nullptr) continue;
            const uint8_t cur = (gy >= 0 && gy < TEXT_H) ? g[gy] : 0;
            const uint8_t under = (shadow && gy >= 1 && gy <= TEXT_H) ? static_cast<uint8_t>(g[gy - 1] >> 1) : 0;
            for (int b = 0; b < TEXT_W; b++) {
                const int x = tx + c * TEXT_W + b;
                if (x < x0 || x > x1) continue;
                const uint8_t m = static_cast<uint8_t>(0x80u >> b);
                if (cur & m) set(x, fg);
                else if (under & m) set(x, READOUT_SHADOW);
            }
        }
    };
    if (dy2 > disc2) {
        if (y >= READOUT_T && y <= READOUT_B) {
            const bool edge = (y == READOUT_T || y == READOUT_B);
            fill(ia, ib, edge ? PLATE_RIM : DIAL_BG);
            if (!edge) { fill(ia, ia, PLATE_RIM); fill(ib, ib, PLATE_RIM); }
            text(m_readout, READOUT_CHARS, READOUT_X, READOUT_Y, READOUT_COLOR, true);
        } else {
            fill(ia, ib, DIAL_BG);
            text(HINT_TEXT, HINT_CHARS, HINT_X, HINT_Y, HINT_COLOR, false);
        }
        return;
    }

    // --- the dial: the dark disc and its rim --------------------------------------------------
    if (dy2 <= zoneOut2) {
        const int xi = Graphics::isqrt16(zoneOut2 - dy2);
        fill(ia, CX - xi - 1, DISC_RIM);
        fill(CX - xi, CX + xi, DIAL_BG);
        fill(CX + xi + 1, ib, DISC_RIM);
    } else {
        fill(ia, ib, DISC_RIM);
    }

    // The rings and the chevrons, in the ring zone outside ZONE_IN
    const int32_t arrow2 = static_cast<int32_t>(ARROW_R) * ARROW_R;
    const int32_t zoneIn2 = static_cast<int32_t>(ZONE_IN) * ZONE_IN;
    const float yc = static_cast<float>(y) + 0.5f;
    if (m_ringsShown && dy2 <= zoneOut2) {
        Row chev[4] = {};
        if (m_chevDir != 0) {
            for (int k = 0; k < 4; k++) chev[k].on = polySpan(m_chev[k], 3, yc, chev[k].x0, chev[k].x1);
        }
        const int xo = Graphics::isqrt16(zoneOut2 - dy2);
        const int xin = (dy2 < zoneIn2) ? Graphics::isqrt16(zoneIn2 - dy2) : -1;
        const int bands[2][2] = {{CX - xo, xin < 0 ? CX + xo : CX - xin - 1}, {xin < 0 ? 1 : CX + xin + 1, xin < 0 ? 0 : CX + xo}};
        for (const auto& bnd : bands) {
            const int lo = bnd[0] < x0 ? x0 : bnd[0];
            const int hi = bnd[1] > x1 ? x1 : bnd[1];
            for (int x = lo; x <= hi; x++) {
                const int dx = x - CX;
                const int32_t d2 = static_cast<int32_t>(dx) * dx + dy2;
                if (d2 <= arrow2) continue;                     // the arrow disc's own pixels
                bool onChev = false;
                for (int k = 0; k < 4 && !onChev; k++) onChev = chev[k].on && x >= chev[k].x0 && x <= chev[k].x1;
                if (onChev) { set(x, CHEVRON_COLOR); continue; }
                for (uint8_t i = 0; i < RING_COUNT; i++) {
                    if (d2 <= m_ringOut2[i] && d2 > m_ringIn2[i]) {
                        set(x, !m_ringLit[i] ? RING_UNLIT : (d2 <= m_ringGlow2[i] ? RING_GLOW : RING_LIT));
                        break;
                    }
                }
            }
        }
    }

    // The arrow: the RGB split behind, the outline, then inside it the speed fill
    if (m_arrowShown && dy2 <= arrow2) {
        Row oh = {}, os = {}, ih = {}, is = {};
        oh.on = polySpan(m_outerHead, 3, yc, oh.x0, oh.x1);
        os.on = polySpan(m_outerShaft, 4, yc, os.x0, os.x1);
        ih.on = polySpan(m_innerHead, 3, yc, ih.x0, ih.x1);
        is.on = polySpan(m_innerShaft, 4, yc, is.x0, is.x1);
        const Row* outline[2] = {&oh, &os};
        const Row* inside[2] = {&ih, &is};
        for (const Row* o : outline) if (o->on) fill(o->x0 - GHOST_PX, o->x1 - GHOST_PX, GHOST_LEFT);
        for (const Row* o : outline) if (o->on) fill(o->x0 + GHOST_PX, o->x1 + GHOST_PX, GHOST_RIGHT);
        for (const Row* o : outline) if (o->on) fill(o->x0, o->x1, ARROW_COLOR);
        const float rowAlong = (yc - m_tailY) * m_axisY;
        for (const Row* o : inside) {
            if (!o->on) continue;
            const int lo = o->x0 < x0 ? x0 : o->x0;
            const int hi = o->x1 > x1 ? x1 : o->x1;
            for (int x = lo; x <= hi; x++) {
                const float along = (static_cast<float>(x) + 0.5f - m_tailX) * m_axisX + rowAlong;
                set(x, (along <= m_fillLen && m_fillLen > 0.5f) ? FILL_COLOR : DIAL_BG);
            }
        }
    }
}

void IndicatorService::paintRows(int top, int bottom, int x0, int x1, bool onTearingEdge) {
    if (top < 0) top = 0;
    if (bottom > SCREEN_H - 1) bottom = SCREEN_H - 1;
    if (x0 < 0) x0 = 0;
    if (x1 > SCREEN_W - 1) x1 = SCREEN_W - 1;
    if (top > bottom || x0 > x1) return;
    // Many rows per stream call: the rows are built in the shared title-mask buffer (no screen
    // with a title mask is up in remote mode; they rebuild after), each call costs its own lock
    // and DMA set-up
    (void)UI::SharedTitleMask::claim(UI::SharedTitleMask::Owner::INDICATOR_ROWS);
    uint8_t* const block = UI::SharedTitleMask::data();
    const int w = x1 - x0 + 1;
    const int rowBytes = w * 2;
    const int rowsPerBlock = static_cast<int>(UI::SharedTitleMask::BYTES) / rowBytes;
    static_assert(static_cast<int>(UI::SharedTitleMask::BYTES) >= SCREEN_W * 2, "a row fits the block");

    if (onTearingEdge) sync();
    m_lcd->streamBitmapStart(static_cast<uint16_t>(x0), static_cast<uint16_t>(top),
                             static_cast<uint16_t>(w), static_cast<uint16_t>(bottom - top + 1));
    int inBlock = 0;
    for (int y = top; y <= bottom; y++) {
        composeRow(y, x0, x1, block + inBlock * rowBytes);
        if (++inBlock == rowsPerBlock) {
            m_lcd->streamBitmapData(block, static_cast<size_t>(inBlock) * rowBytes);
            inBlock = 0;
        }
    }
    if (inBlock > 0) m_lcd->streamBitmapData(block, static_cast<size_t>(inBlock) * rowBytes);
    m_lcd->streamBitmapEnd();
}

void IndicatorService::paintBandRows(int top, int count) {
    // No wait for the tearing edge (~8 ms spun on average): a band caught mid-scan looks torn,
    // which is what a tracking band looks like anyway
    if (top + count <= SCREEN_H) {
        paintRows(top, top + count - 1, 0, SCREEN_W - 1, false);
    } else {                                                    // wraps past the bottom
        paintRows(top, SCREEN_H - 1, 0, SCREEN_W - 1, false);
        paintRows(0, top + count - 1 - SCREEN_H, 0, SCREEN_W - 1, false);
    }
}

void IndicatorService::renderReadout(int16_t speedPct, int16_t turnPct, int8_t dir) {
    char buf[48];
    char v[16], r[24];
    if (speedPct >= 0) snprintf(v, sizeof v, "V %3d%%", speedPct); else snprintf(v, sizeof v, "V   --");
    if (turnPct >= 0) snprintf(r, sizeof r, "R %3d%% %s", turnPct, dir > 0 ? "CW " : "CCW");
    else snprintf(r, sizeof r, "R   --    ");
    snprintf(buf, sizeof buf, "%s   %s", v, r);
    // Only the characters that changed (a digit or two while the speed eases), with their
    // shadow row; the plate's rim and margin never change
    const int wasLen = static_cast<int>(strlen(m_readout)), nowLen = static_cast<int>(strlen(buf));
    int first = -1, last = -1;
    for (int i = 0; i < READOUT_CHARS; i++) {
        const char was = (i < wasLen) ? m_readout[i] : ' ';     // a fresh plate shows blanks
        const char now = (i < nowLen) ? buf[i] : ' ';
        if (was != now) {
            if (first < 0) first = i;
            last = i;
        }
    }
    if (first < 0) return;
    snprintf(m_readout, sizeof m_readout, "%s", buf);
    paintRows(READOUT_Y, READOUT_Y + TEXT_H, READOUT_X + first * TEXT_W, READOUT_X + (last + 1) * TEXT_W - 1);
}

} // namespace Services
