/**
 * @file indicator_service.cpp
 * @brief Motion indicator rendering service implementation
 *
 * Uses a composited single-pass scanline renderer for ring+arrow cases.
 * Every pixel is computed to its final color and streamed in one SPI
 * transaction, eliminating visible flicker between frames.
 */

#include "services/indicator_service.hpp"
#include "drivers/lcd_st7789.hpp"
#include "graphics/trig_lut.hpp"
#include "graphics/primitives.hpp"

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

void IndicatorService::init(LCD* lcd) {
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

void IndicatorService::clear() {
    draw({0, 0, false});
}

// ============================================================================
// Composited single-pass renderer
// ============================================================================

void IndicatorService::drawComposited(const Params& p) {
    bool hasRing = (p.rotation_dir != 0);
    bool hasArrow = p.has_translation;

    // --- Check if anything actually changed ---
    bool ringChanged = (p.rotation_dir != m_prevParams.rotation_dir);
    bool arrowChanged = (p.angle_deg != m_prevParams.angle_deg) ||
                        (p.has_translation != m_prevParams.has_translation);
    bool needFullRedraw = ringChanged || !m_compositedValid;

    if (!needFullRedraw && !arrowChanged) {
        return;  // Nothing changed — skip render entirely
    }

    // --- Precompute arrow triangle edge equations ---
    int numArrow = 0;
    TriEdges arrowE[3];
    int16_t newVerts[7][2];
    int newNumVerts = 0;

    if (hasArrow) {
        uint16_t angle = Graphics::deg_to_angle1024(p.angle_deg);

        int16_t tipX = 0, tipY = -ARROW_HALF_LEN;
        int16_t headBaseY = static_cast<int16_t>(-ARROW_HALF_LEN + ARROW_HEAD_LEN);
        int16_t headRX = ARROW_HEAD_HALF_W, headRY = headBaseY;
        int16_t headLX = static_cast<int16_t>(-ARROW_HEAD_HALF_W), headLY = headBaseY;
        int16_t sTRX = ARROW_SHAFT_HALF_W, sTRY = headBaseY;
        int16_t sTLX = static_cast<int16_t>(-ARROW_SHAFT_HALF_W), sTLY = headBaseY;
        int16_t sBRX = ARROW_SHAFT_HALF_W, sBRY = ARROW_HALF_LEN;
        int16_t sBLX = static_cast<int16_t>(-ARROW_SHAFT_HALF_W), sBLY = ARROW_HALF_LEN;

        int16_t rt[2], rhr[2], rhl[2], rstr[2], rstl[2], rsbr[2], rsbl[2];
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
        int16_t midR = static_cast<int16_t>((RING_OUTER_R + RING_INNER_R) / 2);

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

            int16_t tx = static_cast<int16_t>(cx + tipDx);
            int16_t ty = static_cast<int16_t>(cy + tipDy);
            int16_t b1x = static_cast<int16_t>(cx + baseDx);
            int16_t b1y = static_cast<int16_t>(cy + baseDy);
            int16_t b2x = static_cast<int16_t>(cx - baseDx);
            int16_t b2y = static_cast<int16_t>(cy - baseDy);

            initTriEdges(chevE[i], tx, ty, b1x, b1y, b2x, b2y);
        }
        numChev = 4;
    }

    // --- Determine render region (full window or dirty rect) ---
    int16_t halfW = hasRing ? COMPOSITED_HALF_W : ARROW_COMPOSITED_HALF_W;
    int16_t rMinX, rMinY, rMaxX, rMaxY;

    if (needFullRedraw) {
        // Full composited render
        rMinX = -halfW; rMinY = -halfW;
        rMaxX = halfW;  rMaxY = halfW;
        m_compositedValid = true;
    } else {
        // Dirty rectangle: union of old + new arrow bounding boxes
        rMinX = halfW; rMinY = halfW;
        rMaxX = -halfW; rMaxY = -halfW;

        // Expand by new arrow vertices
        for (int i = 0; i < newNumVerts; i++) {
            int16_t dx = static_cast<int16_t>(newVerts[i][0] - SCREEN_CX);
            int16_t dy = static_cast<int16_t>(newVerts[i][1] - SCREEN_CY);
            if (dx < rMinX) rMinX = dx;
            if (dy < rMinY) rMinY = dy;
            if (dx > rMaxX) rMaxX = dx;
            if (dy > rMaxY) rMaxY = dy;
        }

        // Expand by previous arrow vertices
        for (int i = 0; i < m_prevNumArrowVerts; i++) {
            int16_t dx = static_cast<int16_t>(m_prevArrowVerts[i][0] - SCREEN_CX);
            int16_t dy = static_cast<int16_t>(m_prevArrowVerts[i][1] - SCREEN_CY);
            if (dx < rMinX) rMinX = dx;
            if (dy < rMinY) rMinY = dy;
            if (dx > rMaxX) rMaxX = dx;
            if (dy > rMaxY) rMaxY = dy;
        }

        // Margin for rasterization edges
        rMinX -= 2; rMinY -= 2;
        rMaxX += 2; rMaxY += 2;

        // Clamp to composited window
        if (rMinX < -halfW) rMinX = -halfW;
        if (rMinY < -halfW) rMinY = -halfW;
        if (rMaxX > halfW)  rMaxX = halfW;
        if (rMaxY > halfW)  rMaxY = halfW;

        if (rMinX > rMaxX || rMinY > rMaxY) return;  // No dirty area
    }

    // Save arrow vertices for next frame's dirty rect
    for (int i = 0; i < newNumVerts; i++) {
        m_prevArrowVerts[i][0] = newVerts[i][0];
        m_prevArrowVerts[i][1] = newVerts[i][1];
    }
    m_prevNumArrowVerts = newNumVerts;

    // --- Stream scanlines for the render region ---
    int32_t outerR2 = static_cast<int32_t>(RING_OUTER_R) * RING_OUTER_R;
    int32_t innerR2 = static_cast<int32_t>(RING_INNER_R) * RING_INNER_R;

    uint16_t ww = static_cast<uint16_t>(rMaxX - rMinX + 1);
    uint16_t wh = static_cast<uint16_t>(rMaxY - rMinY + 1);
    uint16_t wx = static_cast<uint16_t>(SCREEN_CX + rMinX);
    uint16_t wy = static_cast<uint16_t>(SCREEN_CY + rMinY);

    static uint8_t scanline[2 * (2 * COMPOSITED_HALF_W + 1)];

    m_lcd->streamBitmapStart(wx, wy, ww, wh);

    for (int16_t dy = rMinY; dy <= rMaxY; dy++) {
        int16_t screenY = static_cast<int16_t>(SCREEN_CY + dy);
        int32_t dy2 = static_cast<int32_t>(dy) * dy;

        for (int16_t dx = rMinX; dx <= rMaxX; dx++) {
            int16_t screenX = static_cast<int16_t>(SCREEN_CX + dx);
            int32_t dist2 = static_cast<int32_t>(dx) * dx + dy2;

            uint16_t color = BG_COLOR;

            if (hasRing && dist2 <= outerR2 && dist2 >= innerR2) {
                // Ring band
                color = CIRCLE_COLOR;
            } else {
                bool isInterior = (!hasRing || dist2 < innerR2);

                // Check arrow (highest z-order, interior only)
                if (isInterior) {
                    for (int i = 0; i < numArrow; i++) {
                        if (screenY >= arrowE[i].minY && screenY <= arrowE[i].maxY &&
                            testTri(arrowE[i], screenX, screenY)) {
                            color = ARROW_COLOR;
                            break;
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

    if (x + w > LCD::WIDTH)  w = LCD::WIDTH - x;
    if (y + h > LCD::HEIGHT) h = LCD::HEIGHT - y;

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
    if (m_prevBboxX + m_prevBboxW > LCD::WIDTH) {
        m_prevBboxW = static_cast<uint16_t>(LCD::WIDTH - m_prevBboxX);
    }
    if (m_prevBboxY + m_prevBboxH > LCD::HEIGHT) {
        m_prevBboxH = static_cast<uint16_t>(LCD::HEIGHT - m_prevBboxY);
    }
}

// ============================================================================
// Arrow rendering (used for arrow-only case, no ring)
// ============================================================================

void IndicatorService::drawArrow(uint16_t angle_deg) {
    uint16_t angle = Graphics::deg_to_angle1024(angle_deg);

    int16_t tipX = 0, tipY = -ARROW_HALF_LEN;
    int16_t headBaseY = static_cast<int16_t>(-ARROW_HALF_LEN + ARROW_HEAD_LEN);
    int16_t headRX = ARROW_HEAD_HALF_W, headRY = headBaseY;
    int16_t headLX = static_cast<int16_t>(-ARROW_HEAD_HALF_W), headLY = headBaseY;
    int16_t shaftTRX = ARROW_SHAFT_HALF_W, shaftTRY = headBaseY;
    int16_t shaftTLX = static_cast<int16_t>(-ARROW_SHAFT_HALF_W), shaftTLY = headBaseY;
    int16_t shaftBRX = ARROW_SHAFT_HALF_W, shaftBRY = ARROW_HALF_LEN;
    int16_t shaftBLX = static_cast<int16_t>(-ARROW_SHAFT_HALF_W), shaftBLY = ARROW_HALF_LEN;

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
    int16_t midR = static_cast<int16_t>((RING_OUTER_R + RING_INNER_R) / 2);

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

        int16_t tx = static_cast<int16_t>(cx + tipDx);
        int16_t ty = static_cast<int16_t>(cy + tipDy);
        int16_t b1x = static_cast<int16_t>(cx + baseDx);
        int16_t b1y = static_cast<int16_t>(cy + baseDy);
        int16_t b2x = static_cast<int16_t>(cx - baseDx);
        int16_t b2y = static_cast<int16_t>(cy - baseDy);

        expandBbox(tx, ty);
        expandBbox(b1x, b1y);
        expandBbox(b2x, b2y);

        Graphics::fillTriangle(*m_lcd, tx, ty, b1x, b1y, b2x, b2y, CHEVRON_COLOR);
    }
}

} // namespace Services
