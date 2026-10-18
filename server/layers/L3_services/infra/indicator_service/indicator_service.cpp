/**
 * @file indicator_service.cpp
 * @brief Motion indicator rendering service implementation
 *
 * Uses a composited single-pass scanline renderer for ring+arrow cases.
 * Every pixel is computed to its final color and streamed in one SPI
 * transaction, eliminating visible flicker between frames.
 */

#include "L3_services/infra/indicator_service/indicator_service.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "graphics/trig_lut/trig_lut.hpp"
#include "graphics/primitives/primitives.hpp"
#include <stdio.h>
#include <string.h>

// ============================================================================
// Triangle edge-equation helpers (file-local)
// ============================================================================

namespace {

struct TriEdges {
    int32_t a0, b0, c0;
    int32_t a1, b1, c1;
    int32_t a2, b2, c2;
    int16_t minY, maxY;
};

void initTriEdges(TriEdges& e,
    int16_t x0, int16_t y0,
    int16_t x1, int16_t y1,
    int16_t x2, int16_t y2)
{
    // Half-plane edge equations: sign(a*px + b*py + c) = which side
    e.a0 = y0 - y1; e.b0 = x1 - x0;
    e.c0 = static_cast<int32_t>(x0) * y1 - static_cast<int32_t>(x1) * y0;
    e.a1 = y1 - y2; e.b1 = x2 - x1;
    e.c1 = static_cast<int32_t>(x1) * y2 - static_cast<int32_t>(x2) * y1;
    e.a2 = y2 - y0; e.b2 = x0 - x2;
    e.c2 = static_cast<int32_t>(x2) * y0 - static_cast<int32_t>(x0) * y2;

    e.minY = y0; if (y1 < e.minY) e.minY = y1; if (y2 < e.minY) e.minY = y2;
    e.maxY = y0; if (y1 > e.maxY) e.maxY = y1; if (y2 > e.maxY) e.maxY = y2;
}

inline bool testTri(const TriEdges& e, int16_t px, int16_t py) {
    int32_t d0 = e.a0 * px + e.b0 * py + e.c0;
    int32_t d1 = e.a1 * px + e.b1 * py + e.c1;
    if ((d0 < 0) != (d1 < 0) && d0 != 0 && d1 != 0) return false; // early exit
    int32_t d2 = e.a2 * px + e.b2 * py + e.c2;
    bool has_neg = (d0 < 0) | (d1 < 0) | (d2 < 0);
    bool has_pos = (d0 > 0) | (d1 > 0) | (d2 > 0);
    return !(has_neg & has_pos);
}

} // anonymous namespace

namespace Services {

// Static global instance
IndicatorService g_indicatorService;

void IndicatorService::init(Services::ICanvas* lcd) {
    m_lcd = lcd;

}

// ============================================================================
// Main draw entry point
// ============================================================================

void IndicatorService::draw(const Params& p) {
    if (m_lcd == nullptr) return;

    // First draw: clear entire screen to black
    if (!m_screenCleared) {
        m_lcd->fillScreen(BG_COLOR);
        m_screenCleared = true;
    }

    bool curHasRing = (p.rotation_dir != 0);
    bool curHasArrow = p.has_translation;

    if (curHasRing || curHasArrow) {
        // Composited renderer for all visible content (ring+arrow or arrow-only).
        // On mode transition (ring ↔ arrow-only), clear old area and invalidate.
        if (m_hasDrawn && curHasRing != m_prevHadRing) {
            clearPreviousBbox();
            m_compositedValid = false;
        }
        drawComposited(p);

        // Store bbox matching the composited window for future clears
        int16_t halfW = curHasRing ? COMPOSITED_HALF_W : ARROW_COMPOSITED_HALF_W;
        m_prevBboxX = static_cast<int16_t>(SCREEN_CX - halfW);
        m_prevBboxY = static_cast<int16_t>(SCREEN_CY - halfW);
        m_prevBboxW = static_cast<uint16_t>(2 * halfW + 1);
        m_prevBboxH = static_cast<uint16_t>(2 * halfW + 1);
        m_hasDrawn = true;
    } else {
        // Idle — clear previous content
        clearPreviousBbox();
        m_compositedValid = false;
        m_hasDrawn = false;
    }

    m_prevHadRing = curHasRing;
    m_prevParams = p;
}

void IndicatorService::invalidate() {
    m_screenCleared = false;
    m_compositedValid = false;
    m_hasDrawn = false;
    m_readout[0] = '\0';
}

void IndicatorService::clear() {
    draw({0, 0, false, 100, 100});
}

// ============================================================================
// Composited single-pass renderer
// ============================================================================

void IndicatorService::drawComposited(const Params& p) {
    bool hasRing = (p.rotation_dir != 0);
    bool hasArrow = p.has_translation;

    // --- Check if anything actually changed ---
    bool ringChanged = (p.rotation_dir != m_prevParams.rotation_dir) ||
                       (p.rot_pct != m_prevParams.rot_pct);
    bool arrowChanged = (p.angle_deg != m_prevParams.angle_deg) ||
                        (p.has_translation != m_prevParams.has_translation) ||
                        (p.speed_pct != m_prevParams.speed_pct);
    bool needFullRedraw = ringChanged || !m_compositedValid;

    if (!needFullRedraw && !arrowChanged) {
        return;  // Nothing changed — skip render entirely
    }
    // The arrow keeps its length; it fills from the tail up to the speed magnitude
    const int16_t halfLen = ARROW_HALF_LEN;
    uint8_t speedPct = p.speed_pct > 100 ? 100 : p.speed_pct;
    int32_t fillLen = static_cast<int32_t>(2 * ARROW_HALF_LEN) * speedPct / 100;   // px from the tail
    int16_t axisX = 0, axisY = 0, tailX = 0, tailY = 0;                          // set with the arrow
    // Turn magnitude as concentric rings from the outside in, each worth 100 / RING_COUNT
    // percent: rings below the level are lit at full width, the ring at the level grows
    // in width through its step until the next ring starts, the rest are dim hairlines.
    uint8_t rotPct = p.rot_pct > 100 ? 100 : p.rot_pct;
    const uint16_t step = 100 / RING_COUNT;
    auto fullRings = static_cast<uint8_t>(rotPct / step);          // lit at full width
    uint16_t partial = rotPct - fullRings * step;                  // 0..step-1 into the next

    // --- Precompute arrow triangle edge equations ---
    int numArrow = 0;
    TriEdges arrowE[3];
    int16_t newVerts[7][2];
    int newNumVerts = 0;

    if (hasArrow) {
        uint16_t angle = Graphics::deg_to_angle1024(p.angle_deg);

        int16_t tipX = 0, tipY = static_cast<int16_t>(-halfLen);
        auto headBaseY = static_cast<int16_t>(-halfLen + ARROW_HEAD_LEN);
        int16_t headRX = ARROW_HEAD_HALF_W, headRY = headBaseY;
        auto headLX = static_cast<int16_t>(-ARROW_HEAD_HALF_W), headLY = headBaseY;
        int16_t sTRX = ARROW_SHAFT_HALF_W, sTRY = headBaseY;
        auto sTLX = static_cast<int16_t>(-ARROW_SHAFT_HALF_W), sTLY = headBaseY;
        int16_t sBRX = ARROW_SHAFT_HALF_W, sBRY = halfLen;
        auto sBLX = static_cast<int16_t>(-ARROW_SHAFT_HALF_W), sBLY = halfLen;

        int16_t rt[2], rhr[2], rhl[2], rstr[2], rstl[2], rsbr[2], rsbl[2];
        // Axis from the tail to the tip (Q15 unit vector) and the tail on screen
        axisX = Graphics::sin_q15(angle);
        axisY = static_cast<int16_t>(-Graphics::cos_q15(angle));
        {
            int16_t tx, ty;
            Graphics::rotate_point(0, halfLen, angle, tx, ty);
            tailX = static_cast<int16_t>(tx + SCREEN_CX);
            tailY = static_cast<int16_t>(ty + SCREEN_CY);
        }
        Graphics::rotate_point(tipX, tipY, angle, rt[0], rt[1]);
        Graphics::rotate_point(headRX, headRY, angle, rhr[0], rhr[1]);
        Graphics::rotate_point(headLX, headLY, angle, rhl[0], rhl[1]);
        Graphics::rotate_point(sTRX, sTRY, angle, rstr[0], rstr[1]);
        Graphics::rotate_point(sTLX, sTLY, angle, rstl[0], rstl[1]);
        Graphics::rotate_point(sBRX, sBRY, angle, rsbr[0], rsbr[1]);
        Graphics::rotate_point(sBLX, sBLY, angle, rsbl[0], rsbl[1]);

        // Offset to screen center
        rt[0] += SCREEN_CX;  rt[1] += SCREEN_CY;
        rhr[0] += SCREEN_CX; rhr[1] += SCREEN_CY;
        rhl[0] += SCREEN_CX; rhl[1] += SCREEN_CY;
        rstr[0] += SCREEN_CX; rstr[1] += SCREEN_CY;
        rstl[0] += SCREEN_CX; rstl[1] += SCREEN_CY;
        rsbr[0] += SCREEN_CX; rsbr[1] += SCREEN_CY;
        rsbl[0] += SCREEN_CX; rsbl[1] += SCREEN_CY;

        // Store vertices for dirty rect computation
        newVerts[0][0] = rt[0];   newVerts[0][1] = rt[1];
        newVerts[1][0] = rhr[0];  newVerts[1][1] = rhr[1];
        newVerts[2][0] = rhl[0];  newVerts[2][1] = rhl[1];
        newVerts[3][0] = rstr[0]; newVerts[3][1] = rstr[1];
        newVerts[4][0] = rstl[0]; newVerts[4][1] = rstl[1];
        newVerts[5][0] = rsbr[0]; newVerts[5][1] = rsbr[1];
        newVerts[6][0] = rsbl[0]; newVerts[6][1] = rsbl[1];
        newNumVerts = 7;

        // Arrowhead
        initTriEdges(arrowE[0], rt[0], rt[1], rhr[0], rhr[1], rhl[0], rhl[1]);
        // Shaft right half
        initTriEdges(arrowE[1], rstr[0], rstr[1], rsbr[0], rsbr[1], rsbl[0], rsbl[1]);
        // Shaft left half
        initTriEdges(arrowE[2], rstr[0], rstr[1], rsbl[0], rsbl[1], rstl[0], rstl[1]);
        numArrow = 3;
    }

    // --- Precompute chevron triangle edge equations ---
    int numChev = 0;
    TriEdges chevE[4];

    if (hasRing) {
        static constexpr uint16_t chevAngles[4] = {0, 90, 180, 270};
        auto midR = static_cast<int16_t>(RING_OUTER_R - RING_W_MAX / 2);   // on the outer ring

        for (int i = 0; i < 4; i++) {
            uint16_t posAngle = Graphics::deg_to_angle1024(chevAngles[i]);

            int16_t cx, cy;
            Graphics::rotate_point(0, static_cast<int16_t>(-midR), posAngle, cx, cy);
            cx += SCREEN_CX; cy += SCREEN_CY;

            uint16_t tangentAngle;
            if (p.rotation_dir > 0) {
                tangentAngle = Graphics::deg_to_angle1024(
                    static_cast<uint16_t>((chevAngles[i] + 90) % 360));
            } else {
                tangentAngle = Graphics::deg_to_angle1024(
                    static_cast<uint16_t>((chevAngles[i] + 270) % 360));
            }

            int16_t tipDx, tipDy;
            Graphics::rotate_point(0, static_cast<int16_t>(-CHEVRON_SIZE),
                                   tangentAngle, tipDx, tipDy);

            int16_t baseDx, baseDy;
            Graphics::rotate_point(0, static_cast<int16_t>(-CHEVRON_SIZE / 2),
                                   posAngle, baseDx, baseDy);

            auto tx = static_cast<int16_t>(cx + tipDx);
            auto ty = static_cast<int16_t>(cy + tipDy);
            auto b1x = static_cast<int16_t>(cx + baseDx);
            auto b1y = static_cast<int16_t>(cy + baseDy);
            auto b2x = static_cast<int16_t>(cx - baseDx);
            auto b2y = static_cast<int16_t>(cy - baseDy);

            initTriEdges(chevE[i], tx, ty, b1x, b1y, b2x, b2y);
        }
        numChev = 4;
    }

    // --- Determine render region (full window or dirty rect) ---
    int16_t halfW = hasRing ? COMPOSITED_HALF_W : ARROW_COMPOSITED_HALF_W;
    int16_t rMinX, rMinY, rMaxX, rMaxY;

    if (needFullRedraw) {
        // Full composited render (ring toggle or first draw)
        rMinX = -halfW; rMinY = -halfW;
        rMaxX = halfW;  rMaxY = halfW;
        m_compositedValid = true;
    } else {
        // Fixed-size arrow region — consistent frame time eliminates jitter.
        // The arrow's max vertex radius is ~55px; ARROW_COMPOSITED_HALF_W (57)
        // always covers the full arrow at any rotation with margin.
        int16_t arrowHalf = ARROW_COMPOSITED_HALF_W;
        rMinX = -arrowHalf; rMinY = -arrowHalf;
        rMaxX = arrowHalf;  rMaxY = arrowHalf;
    }

    // Save arrow vertices for next frame's dirty rect
    for (int i = 0; i < newNumVerts; i++) {
        m_prevArrowVerts[i][0] = newVerts[i][0];
        m_prevArrowVerts[i][1] = newVerts[i][1];
    }
    m_prevNumArrowVerts = newNumVerts;

    // --- Precompute combined Y-range of all arrow triangles for early-out ---
    int16_t arrowYmin = 32767, arrowYmax = -32768;
    for (int i = 0; i < numArrow; i++) {
        if (arrowE[i].minY < arrowYmin) arrowYmin = arrowE[i].minY;
        if (arrowE[i].maxY > arrowYmax) arrowYmax = arrowE[i].maxY;
    }

    // --- Stream scanlines for the render region ---
    auto outerR2 = static_cast<int32_t>(RING_OUTER_R) * RING_OUTER_R;
    auto innerR2 = static_cast<int32_t>(RING_INNER_R) * RING_INNER_R;    // inside every ring
    int32_t ringOut2[RING_COUNT], ringIn2[RING_COUNT];
    bool ringLit[RING_COUNT];
    for (uint8_t i = 0; i < RING_COUNT; i++) {
        int32_t ro = RING_OUTER_R - i * RING_PITCH;
        int32_t w;
        if (i < fullRings)        { w = RING_W_MAX; ringLit[i] = true; }
        else if (i == fullRings)  { w = 1 + static_cast<int32_t>(partial) * (RING_W_MAX - 1) / (step - 1);
                                    ringLit[i] = partial > 0 || rotPct >= 100; }
        else                      { w = 1; ringLit[i] = false; }
        if (rotPct >= 100) { w = RING_W_MAX; ringLit[i] = true; }
        int32_t ri = ro - w;
        ringOut2[i] = ro * ro;
        ringIn2[i] = ri * ri;
    }

    auto ww = static_cast<uint16_t>(rMaxX - rMinX + 1);
    auto wh = static_cast<uint16_t>(rMaxY - rMinY + 1);
    auto wx = static_cast<uint16_t>(SCREEN_CX + rMinX);
    auto wy = static_cast<uint16_t>(SCREEN_CY + rMinY);

    static uint8_t scanline[2 * (2 * COMPOSITED_HALF_W + 1)];

    // Sync to V-blank to avoid tearing
    m_lcd->waitForVSync();

    m_lcd->streamBitmapStart(wx, wy, ww, wh);

    for (int16_t dy = rMinY; dy <= rMaxY; dy++) {
        auto screenY = static_cast<int16_t>(SCREEN_CY + dy);
        auto dy2 = static_cast<int32_t>(dy) * dy;

        // Y-range early-out: skip arrow triangle tests for scanlines
        // entirely above or below all arrow geometry
        bool arrowOnScanline = (numArrow > 0 &&
                                screenY >= arrowYmin && screenY <= arrowYmax);

        for (int16_t dx = rMinX; dx <= rMaxX; dx++) {
            auto screenX = static_cast<int16_t>(SCREEN_CX + dx);
            auto dist2 = static_cast<int32_t>(dx) * dx + dy2;

            uint16_t color = BG_COLOR;

            if (hasRing && dist2 <= outerR2 && dist2 >= innerR2) {
                // The ring zone: the direction chevrons first, then which ring, if any, and
                // whether it is lit
                for (uint8_t i = 0; i < numChev; i++) {
                    if (screenY >= chevE[i].minY && screenY <= chevE[i].maxY &&
                        testTri(chevE[i], screenX, screenY)) {
                        color = CHEVRON_COLOR;
                        break;
                    }
                }
                for (uint8_t i = 0; color == BG_COLOR && i < RING_COUNT; i++) {
                    if (dist2 <= ringOut2[i] && dist2 >= ringIn2[i]) {
                        color = ringLit[i] ? ARC_COLOR : CIRCLE_COLOR;
                        break;
                    }
                }
            } else {
                bool isInterior = (!hasRing || dist2 < innerR2);

                // Check arrow (highest z-order, interior only): a magenta outline, the
                // inside filled cyan from the tail up to the speed magnitude
                if (arrowOnScanline && isInterior) {
                    auto inArrow = [&](int16_t x, int16_t y) {
                        for (int i = 0; i < numArrow; i++) {
                            if (y >= arrowE[i].minY && y <= arrowE[i].maxY && testTri(arrowE[i], x, y)) return true;
                        }
                        return false;
                    };
                    if (inArrow(screenX, screenY)) {
                        bool edge = !inArrow(static_cast<int16_t>(screenX - 2), screenY) ||
                                    !inArrow(static_cast<int16_t>(screenX + 2), screenY) ||
                                    !inArrow(screenX, static_cast<int16_t>(screenY - 2)) ||
                                    !inArrow(screenX, static_cast<int16_t>(screenY + 2));
                        if (edge) {
                            color = ARROW_COLOR;
                        } else {
                            int32_t along = (static_cast<int32_t>(screenX - tailX) * axisX +
                                             static_cast<int32_t>(screenY - tailY) * axisY) >> 15;
                            color = (along <= fillLen) ? ARC_COLOR : BG_COLOR;
                        }
                    }
                }

                // Check chevrons (inner + outer protrusions)
                if (color == BG_COLOR && hasRing) {
                    for (int i = 0; i < numChev; i++) {
                        if (screenY >= chevE[i].minY && screenY <= chevE[i].maxY &&
                            testTri(chevE[i], screenX, screenY)) {
                            color = CHEVRON_COLOR;
                            break;
                        }
                    }
                }
            }

            int idx = (dx - rMinX) * 2;
            scanline[idx]     = static_cast<uint8_t>(color >> 8);
            scanline[idx + 1] = static_cast<uint8_t>(color & 0xFF);
        }

        m_lcd->streamBitmapData(scanline, ww * 2);
    }

    m_lcd->streamBitmapEnd();
    drawReadout(p);
}

void IndicatorService::drawReadout(const Params& p) {
    // Magnitudes under the ring, repainted only when the text changes
    char buf[24];
    const char* turn = p.rotation_dir > 0 ? "CW" : (p.rotation_dir < 0 ? "CCW" : "--");
    snprintf(buf, sizeof buf, "V %3u%%   R %3u%% %s", static_cast<unsigned>(p.has_translation ? p.speed_pct : 0),
             static_cast<unsigned>(p.rotation_dir != 0 ? p.rot_pct : 0), turn);
    if (strcmp(buf, m_readout) == 0) return;
    snprintf(m_readout, sizeof m_readout, "%s", buf);
    auto y = static_cast<uint16_t>(SCREEN_CY + RING_OUTER_R + 10);
    m_lcd->blitTextLine(static_cast<uint16_t>(SCREEN_CX - 66), y, 132, 8, buf, ARC_COLOR, BG_COLOR);
}

// ============================================================================
// Bounding box management
// ============================================================================

void IndicatorService::clearPreviousBbox() {
    if (!m_hasDrawn || m_lcd == nullptr) return;
    if (m_prevBboxW == 0 || m_prevBboxH == 0) return;

    uint16_t x = (m_prevBboxX < 0) ? 0 : static_cast<uint16_t>(m_prevBboxX);
    uint16_t y = (m_prevBboxY < 0) ? 0 : static_cast<uint16_t>(m_prevBboxY);
    uint16_t w = m_prevBboxW;
    uint16_t h = m_prevBboxH;

    if (x + w > Services::Canvas::WIDTH)  w = Services::Canvas::WIDTH - x;
    if (y + h > Services::Canvas::HEIGHT) h = Services::Canvas::HEIGHT - y;

    m_lcd->fillRect(x, y, w, h, BG_COLOR);
}

void IndicatorService::resetBbox() {
    m_curMinX = 32767;
    m_curMinY = 32767;
    m_curMaxX = -32768;
    m_curMaxY = -32768;
}

void IndicatorService::expandBbox(int16_t x, int16_t y) {
    if (x < m_curMinX) m_curMinX = x;
    if (y < m_curMinY) m_curMinY = y;
    if (x > m_curMaxX) m_curMaxX = x;
    if (y > m_curMaxY) m_curMaxY = y;
}

void IndicatorService::storeBbox() {
    if (m_curMinX > m_curMaxX) {
        m_prevBboxW = 0;
        m_prevBboxH = 0;
        return;
    }

    m_prevBboxX = static_cast<int16_t>(m_curMinX - 1);
    m_prevBboxY = static_cast<int16_t>(m_curMinY - 1);
    m_prevBboxW = static_cast<uint16_t>(m_curMaxX - m_curMinX + 3);
    m_prevBboxH = static_cast<uint16_t>(m_curMaxY - m_curMinY + 3);

    if (m_prevBboxX < 0) {
        m_prevBboxW = static_cast<uint16_t>(m_prevBboxW + m_prevBboxX);
        m_prevBboxX = 0;
    }
    if (m_prevBboxY < 0) {
        m_prevBboxH = static_cast<uint16_t>(m_prevBboxH + m_prevBboxY);
        m_prevBboxY = 0;
    }
    if (m_prevBboxX + m_prevBboxW > Services::Canvas::WIDTH) {
        m_prevBboxW = static_cast<uint16_t>(Services::Canvas::WIDTH - m_prevBboxX);
    }
    if (m_prevBboxY + m_prevBboxH > Services::Canvas::HEIGHT) {
        m_prevBboxH = static_cast<uint16_t>(Services::Canvas::HEIGHT - m_prevBboxY);
    }
}

// ============================================================================
// Arrow rendering (used for arrow-only case, no ring)
// ============================================================================

void IndicatorService::drawArrow(uint16_t angle_deg) {
    uint16_t angle = Graphics::deg_to_angle1024(angle_deg);

    int16_t tipX = 0, tipY = -ARROW_HALF_LEN;
    auto headBaseY = static_cast<int16_t>(-ARROW_HALF_LEN + ARROW_HEAD_LEN);
    int16_t headRX = ARROW_HEAD_HALF_W, headRY = headBaseY;
    auto headLX = static_cast<int16_t>(-ARROW_HEAD_HALF_W), headLY = headBaseY;
    int16_t shaftTRX = ARROW_SHAFT_HALF_W, shaftTRY = headBaseY;
    auto shaftTLX = static_cast<int16_t>(-ARROW_SHAFT_HALF_W), shaftTLY = headBaseY;
    int16_t shaftBRX = ARROW_SHAFT_HALF_W, shaftBRY = ARROW_HALF_LEN;
    auto shaftBLX = static_cast<int16_t>(-ARROW_SHAFT_HALF_W), shaftBLY = ARROW_HALF_LEN;

    int16_t rtipX, rtipY;
    int16_t rheadRX, rheadRY, rheadLX, rheadLY;
    int16_t rsTRX, rsTRY, rsTLX, rsTLY;
    int16_t rsBRX, rsBRY, rsBLX, rsBLY;

    Graphics::rotate_point(tipX, tipY, angle, rtipX, rtipY);
    Graphics::rotate_point(headRX, headRY, angle, rheadRX, rheadRY);
    Graphics::rotate_point(headLX, headLY, angle, rheadLX, rheadLY);
    Graphics::rotate_point(shaftTRX, shaftTRY, angle, rsTRX, rsTRY);
    Graphics::rotate_point(shaftTLX, shaftTLY, angle, rsTLX, rsTLY);
    Graphics::rotate_point(shaftBRX, shaftBRY, angle, rsBRX, rsBRY);
    Graphics::rotate_point(shaftBLX, shaftBLY, angle, rsBLX, rsBLY);

    rtipX += SCREEN_CX;    rtipY += SCREEN_CY;
    rheadRX += SCREEN_CX;  rheadRY += SCREEN_CY;
    rheadLX += SCREEN_CX;  rheadLY += SCREEN_CY;
    rsTRX += SCREEN_CX;    rsTRY += SCREEN_CY;
    rsTLX += SCREEN_CX;    rsTLY += SCREEN_CY;
    rsBRX += SCREEN_CX;    rsBRY += SCREEN_CY;
    rsBLX += SCREEN_CX;    rsBLY += SCREEN_CY;

    expandBbox(rtipX, rtipY);
    expandBbox(rheadRX, rheadRY);
    expandBbox(rheadLX, rheadLY);
    expandBbox(rsBRX, rsBRY);
    expandBbox(rsBLX, rsBLY);

    Graphics::fillTriangle(*m_lcd,
        rtipX, rtipY, rheadRX, rheadRY, rheadLX, rheadLY, ARROW_COLOR);
    Graphics::fillTriangle(*m_lcd,
        rsTRX, rsTRY, rsBRX, rsBRY, rsBLX, rsBLY, ARROW_COLOR);
    Graphics::fillTriangle(*m_lcd,
        rsTRX, rsTRY, rsBLX, rsBLY, rsTLX, rsTLY, ARROW_COLOR);
}

// ============================================================================
// Legacy individual draw methods (kept for arrow-only fallback)
// ============================================================================

void IndicatorService::drawRing() {
    Graphics::fillRing(*m_lcd, SCREEN_CX, SCREEN_CY, RING_OUTER_R, RING_INNER_R,
                        CIRCLE_COLOR);
    expandBbox(static_cast<int16_t>(SCREEN_CX - RING_OUTER_R),
               static_cast<int16_t>(SCREEN_CY - RING_OUTER_R));
    expandBbox(static_cast<int16_t>(SCREEN_CX + RING_OUTER_R),
               static_cast<int16_t>(SCREEN_CY + RING_OUTER_R));
}

void IndicatorService::drawChevrons(int8_t direction) {
    static constexpr uint16_t chevronAngles[4] = {0, 90, 180, 270};
    auto midR = static_cast<int16_t>((RING_OUTER_R + RING_INNER_R) / 2);

    for (int i = 0; i < 4; i++) {
        uint16_t posAngle = Graphics::deg_to_angle1024(chevronAngles[i]);
        int16_t cx, cy;
        Graphics::rotate_point(0, static_cast<int16_t>(-midR), posAngle, cx, cy);
        cx += SCREEN_CX; cy += SCREEN_CY;

        uint16_t tangentAngle;
        if (direction > 0) {
            tangentAngle = Graphics::deg_to_angle1024(
                static_cast<uint16_t>((chevronAngles[i] + 90) % 360));
        } else {
            tangentAngle = Graphics::deg_to_angle1024(
                static_cast<uint16_t>((chevronAngles[i] + 270) % 360));
        }

        int16_t tipDx, tipDy;
        Graphics::rotate_point(0, static_cast<int16_t>(-CHEVRON_SIZE), tangentAngle,
                               tipDx, tipDy);
        int16_t baseDx, baseDy;
        Graphics::rotate_point(0, static_cast<int16_t>(-CHEVRON_SIZE / 2), posAngle,
                               baseDx, baseDy);

        auto tx = static_cast<int16_t>(cx + tipDx);
        auto ty = static_cast<int16_t>(cy + tipDy);
        auto b1x = static_cast<int16_t>(cx + baseDx);
        auto b1y = static_cast<int16_t>(cy + baseDy);
        auto b2x = static_cast<int16_t>(cx - baseDx);
        auto b2y = static_cast<int16_t>(cy - baseDy);

        expandBbox(tx, ty);
        expandBbox(b1x, b1y);
        expandBbox(b2x, b2y);

        Graphics::fillTriangle(*m_lcd, tx, ty, b1x, b1y, b2x, b2y, CHEVRON_COLOR);
    }
}

} // namespace Services
