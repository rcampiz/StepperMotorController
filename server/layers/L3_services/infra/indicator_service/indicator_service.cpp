/**
 * @file indicator_service.cpp
 * @brief Motion indicator: eased target, span rows, repaint of what changed only.
 */

#include "L3_services/infra/indicator_service/indicator_service.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "graphics/trig_lut/trig_lut.hpp"
#include "graphics/primitives/primitives.hpp"
#include <stdio.h>
#include <string.h>

namespace Services {

IndicatorService g_indicatorService;

namespace {

constexpr float EASE = 0.45f;                // share of the remaining distance per 50 ms tick

// Arrow, unrotated, heading up (y grows downward), around the centre
constexpr float HALF_LEN = 55.0f;
constexpr float HEAD_LEN = 26.0f;
constexpr float HEAD_HALF_W = 18.0f;
constexpr float SHAFT_HALF_W = 5.0f;
constexpr float OUTLINE = 2.0f;

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

inline float ease(float cur, float target) {
    float d = target - cur;
    if (d < 0.5f && d > -0.5f) return target;
    return cur + d * EASE;
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
void IndicatorService::tick() {
    const uint32_t t = m_target;
    if (m_lcd == nullptr || (t & (1u << 31)) == 0) return;      // nothing requested yet

    const auto tAngle = static_cast<float>(t & 0x1FFu);
    const int8_t tDir = static_cast<int8_t>(((t >> 9) & 0x3u)) - 1;
    const bool tTrans = ((t >> 11) & 1u) != 0;
    const auto tSpeed = static_cast<float>(tTrans ? ((t >> 12) & 0x7Fu) : 0u);
    const auto tTurn = static_cast<float>(tDir != 0 ? ((t >> 19) & 0x7Fu) : 0u);

    // Ease toward the target; the angle the short way round
    float d = wrapDiff(tAngle - m_angle);
    m_angle = (d < 0.5f && d > -0.5f) ? tAngle : m_angle + d * EASE;
    if (m_angle >= 360.0f) m_angle -= 360.0f;
    if (m_angle < 0.0f) m_angle += 360.0f;
    m_speed = ease(m_speed, tSpeed);
    m_turn = ease(m_turn, tTurn);

    if (!m_screenValid) {
        m_lcd->waitForVSync();
        m_lcd->fillScreen(BG_COLOR);
        m_lcd->blitTextLine(CX - 60, HINT_Y, 120, 8, "hold CENTER 2s: menu", HINT_COLOR, BG_COLOR);
        m_arrowShown = m_ringsShown = false;
        m_arrowAngle = m_arrowFill = -1;
        memset(m_ringW, 0, sizeof m_ringW);
        memset(m_ringLit, 0, sizeof m_ringLit);
        m_chevDir = 0;
        m_readout[0] = '\0';
        m_screenValid = true;
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

    if (arrowDirty || ringsDirty) {
        if (arrowShown) buildArrow(static_cast<uint16_t>(angle1024), static_cast<float>(fillPx));
        if (ringsShown) { buildRings(widths); buildChevrons(chevDir); }
        renderFrame(ringsDirty, arrowDirty, ringsShown, arrowShown);
        m_arrowShown = arrowShown;
        m_arrowAngle = angle1024;
        m_arrowFill = fillPx;
        m_ringsShown = ringsShown;
        memcpy(m_ringW, widths, sizeof m_ringW);
        memcpy(m_ringLit, lit, sizeof m_ringLit);
        m_chevDir = chevDir;
    }
    renderReadout(arrowShown ? static_cast<int16_t>(m_speed + 0.5f) : -1,
                  tDir != 0 ? static_cast<int16_t>(m_turn + 0.5f) : -1, tDir);
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
    }
}

void IndicatorService::buildChevrons(int8_t dir) {
    if (dir == 0) return;
    const float mid = RING_OUT_R - RING_W_MAX / 2.0f;
    for (uint8_t k = 0; k < 4; k++) {
        const uint16_t a = static_cast<uint16_t>(k * 256);      // 0, 90, 180, 270 degrees
        const float s = sinDeg1024(a), c = cosDeg1024(a);
        const float cx = CX + mid * s, cy = CY - mid * c;       // on the ring, clockwise from up
        const float rx = s, ry = -c;                            // radial, outward
        const float tx = (dir > 0) ? c : -c, ty = (dir > 0) ? s : -s;   // tangent, turn direction
        m_chev[k][0][0] = cx + tx * CHEV;          m_chev[k][0][1] = cy + ty * CHEV;
        m_chev[k][1][0] = cx + rx * (CHEV / 2.0f); m_chev[k][1][1] = cy + ry * (CHEV / 2.0f);
        m_chev[k][2][0] = cx - rx * (CHEV / 2.0f); m_chev[k][2][1] = cy - ry * (CHEV / 2.0f);
    }
}

// ------------------------------------------------------------------------------------------
void IndicatorService::renderFrame(bool rings, bool arrow, bool ringsShown, bool arrowShown) {
    static uint8_t buf[Services::Canvas::WIDTH * 2];
    auto put = [&](int x0, int x1, uint16_t y) {
        if (x1 < x0) return;
        m_lcd->streamBitmapStart(static_cast<uint16_t>(x0), y, static_cast<uint16_t>(x1 - x0 + 1), 1);
        m_lcd->streamBitmapData(buf, static_cast<size_t>(x1 - x0 + 1) * 2);
        m_lcd->streamBitmapEnd();
    };
    auto set = [&](int i, uint16_t colour) {
        buf[i * 2] = static_cast<uint8_t>(colour >> 8);
        buf[i * 2 + 1] = static_cast<uint8_t>(colour & 0xFF);
    };

    const int top = CY - (rings ? ZONE_OUT : ARROW_R);
    const int bottom = CY + (rings ? ZONE_OUT : ARROW_R);
    m_lcd->waitForVSync();                                     // start on the tearing edge
    for (int y = top; y <= bottom; y++) {
        const int dy = y - CY;
        const int32_t dy2 = static_cast<int32_t>(dy) * dy;
        const float yc = static_cast<float>(y) + 0.5f;

        // --- ring zone: left and right bands, or one span above and below the inner edge
        if (rings && dy2 <= static_cast<int32_t>(ZONE_OUT) * ZONE_OUT) {
            const int xo = Graphics::isqrt16(static_cast<int32_t>(ZONE_OUT) * ZONE_OUT - dy2);
            int xi = -1;
            if (dy2 < static_cast<int32_t>(ZONE_IN) * ZONE_IN) {
                xi = Graphics::isqrt16(static_cast<int32_t>(ZONE_IN) * ZONE_IN - dy2);
            }
            Row chev[4] = {};
            if (ringsShown && m_chevDir != 0) {
                for (int k = 0; k < 4; k++) chev[k].on = polySpan(m_chev[k], 3, yc, chev[k].x0, chev[k].x1);
            }
            auto ringColour = [&](int x) -> uint16_t {
                if (!ringsShown) return BG_COLOR;
                for (int k = 0; k < 4; k++) {
                    if (chev[k].on && x >= chev[k].x0 && x <= chev[k].x1) return CHEVRON_COLOR;
                }
                const int dx = x - CX;
                const int32_t d2 = static_cast<int32_t>(dx) * dx + dy2;
                for (uint8_t i = 0; i < RING_COUNT; i++) {
                    if (d2 <= m_ringOut2[i] && d2 > m_ringIn2[i]) return m_ringLit[i] ? RING_LIT : RING_UNLIT;
                }
                return BG_COLOR;
            };
            auto band = [&](int x0, int x1) {
                for (int x = x0; x <= x1; x++) set(x - x0, ringColour(x));
                put(x0, x1, static_cast<uint16_t>(y));
            };
            if (xi < 0) {
                band(CX - xo, CX + xo);
            } else {
                band(CX - xo, CX - xi - 1);
                band(CX + xi + 1, CX + xo);
            }
        }

        // --- arrow disc
        if (arrow && dy2 <= static_cast<int32_t>(ARROW_R) * ARROW_R) {
            const int xa = Graphics::isqrt16(static_cast<int32_t>(ARROW_R) * ARROW_R - dy2);
            Row oh = {}, os = {}, ih = {}, is = {};
            if (arrowShown) {
                oh.on = polySpan(m_outerHead, 3, yc, oh.x0, oh.x1);
                os.on = polySpan(m_outerShaft, 4, yc, os.x0, os.x1);
                ih.on = polySpan(m_innerHead, 3, yc, ih.x0, ih.x1);
                is.on = polySpan(m_innerShaft, 4, yc, is.x0, is.x1);
            }
            const float rowAlong = (yc - m_tailY) * m_axisY;
            const int x0 = CX - xa, x1 = CX + xa;
            for (int x = x0; x <= x1; x++) {
                uint16_t c = BG_COLOR;
                if ((ih.on && x >= ih.x0 && x <= ih.x1) || (is.on && x >= is.x0 && x <= is.x1)) {
                    const float along = (static_cast<float>(x) + 0.5f - m_tailX) * m_axisX + rowAlong;
                    c = (along <= m_fillLen && m_fillLen > 0.5f) ? FILL_COLOR : BG_COLOR;
                } else if ((oh.on && x >= oh.x0 && x <= oh.x1) || (os.on && x >= os.x0 && x <= os.x1)) {
                    c = ARROW_COLOR;
                }
                set(x - x0, c);
            }
            put(x0, x1, static_cast<uint16_t>(y));
        }
    }
}

void IndicatorService::renderReadout(int16_t speedPct, int16_t turnPct, int8_t dir) {
    char buf[48];
    char v[16], r[24];
    if (speedPct >= 0) snprintf(v, sizeof v, "V %3d%%", speedPct); else snprintf(v, sizeof v, "V   --");
    if (turnPct >= 0) snprintf(r, sizeof r, "R %3d%% %s", turnPct, dir > 0 ? "CW " : "CCW");
    else snprintf(r, sizeof r, "R   --    ");
    snprintf(buf, sizeof buf, "%s   %s", v, r);
    if (strcmp(buf, m_readout) == 0) return;
    snprintf(m_readout, sizeof m_readout, "%s", buf);
    m_lcd->blitTextLine(CX - 66, READOUT_Y, 132, 8, buf, RING_LIT, BG_COLOR);
}

} // namespace Services
