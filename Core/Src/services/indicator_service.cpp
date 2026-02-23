/**
 * @file indicator_service.cpp
 * @brief Motion indicator rendering service implementation
 */

#include "services/indicator_service.hpp"
#include "drivers/lcd_st7789.hpp"
#include "graphics/trig_lut.hpp"
#include "graphics/primitives.hpp"

namespace Services {

// Static global instance
IndicatorService g_indicatorService;

void IndicatorService::init(LCD* lcd) {
    m_lcd = lcd;
}

void IndicatorService::draw(const Params& p) {
    if (m_lcd == nullptr) return;

    // First draw: clear entire screen to black
    if (!m_screenCleared) {
        m_lcd->fillScreen(BG_COLOR);
        m_screenCleared = true;
    }

    // Clear previous indicator region
    clearPreviousBbox();

    // Idle state: just clear, don't draw anything new
    if (!p.has_translation && p.rotation_dir == 0) {
        m_hasDrawn = false;
        return;
    }

    // Begin accumulating new bounding box
    resetBbox();

    // Draw order: ring first, then chevrons, then arrow on top
    if (p.rotation_dir != 0) {
        drawRing();
        drawChevrons(p.rotation_dir);
    }

    if (p.has_translation) {
        drawArrow(p.angle_deg);
    }

    // Store the accumulated bounding box for next frame's clear
    storeBbox();
    m_hasDrawn = true;
}

void IndicatorService::clear() {
    draw({0, 0, false});
}

// ============================================================================
// Bounding box management
// ============================================================================

void IndicatorService::clearPreviousBbox() {
    if (!m_hasDrawn || m_lcd == nullptr) return;
    if (m_prevBboxW == 0 || m_prevBboxH == 0) return;

    // Clip to screen
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
        // Nothing was drawn
        m_prevBboxW = 0;
        m_prevBboxH = 0;
        return;
    }

    // Add 1px margin for rounding
    m_prevBboxX = static_cast<int16_t>(m_curMinX - 1);
    m_prevBboxY = static_cast<int16_t>(m_curMinY - 1);
    m_prevBboxW = static_cast<uint16_t>(m_curMaxX - m_curMinX + 3);
    m_prevBboxH = static_cast<uint16_t>(m_curMaxY - m_curMinY + 3);

    // Clamp to screen
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
// Arrow rendering
// ============================================================================

void IndicatorService::drawArrow(uint16_t angle_deg) {
    uint16_t angle = Graphics::deg_to_angle1024(angle_deg);

    // Arrow shape: distinct arrowhead triangle + thin rectangular shaft
    //
    //       *           <- tip (0, -ARROW_HALF_LEN)
    //      / \
    //     /   \
    //    *-----*        <- head base (±ARROW_HEAD_HALF_W, -ARROW_HALF_LEN + ARROW_HEAD_LEN)
    //      | |
    //      | |          <- shaft (±ARROW_SHAFT_HALF_W)
    //      | |
    //      *-*          <- shaft bottom (±ARROW_SHAFT_HALF_W, +ARROW_HALF_LEN)

    // Tip
    int16_t tipX = 0, tipY = -ARROW_HALF_LEN;

    // Head base corners (wide)
    int16_t headBaseY = static_cast<int16_t>(-ARROW_HALF_LEN + ARROW_HEAD_LEN);
    int16_t headRX = ARROW_HEAD_HALF_W,                          headRY = headBaseY;
    int16_t headLX = static_cast<int16_t>(-ARROW_HEAD_HALF_W),   headLY = headBaseY;

    // Shaft top corners (narrow, same Y as head base)
    int16_t shaftTRX = ARROW_SHAFT_HALF_W,                         shaftTRY = headBaseY;
    int16_t shaftTLX = static_cast<int16_t>(-ARROW_SHAFT_HALF_W),  shaftTLY = headBaseY;

    // Shaft bottom corners
    int16_t shaftBRX = ARROW_SHAFT_HALF_W,                         shaftBRY = ARROW_HALF_LEN;
    int16_t shaftBLX = static_cast<int16_t>(-ARROW_SHAFT_HALF_W),  shaftBLY = ARROW_HALF_LEN;

    // Rotate all vertices
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

    // Offset to screen center
    rtipX += SCREEN_CX;    rtipY += SCREEN_CY;
    rheadRX += SCREEN_CX;  rheadRY += SCREEN_CY;
    rheadLX += SCREEN_CX;  rheadLY += SCREEN_CY;
    rsTRX += SCREEN_CX;    rsTRY += SCREEN_CY;
    rsTLX += SCREEN_CX;    rsTLY += SCREEN_CY;
    rsBRX += SCREEN_CX;    rsBRY += SCREEN_CY;
    rsBLX += SCREEN_CX;    rsBLY += SCREEN_CY;

    // Expand bounding box
    expandBbox(rtipX, rtipY);
    expandBbox(rheadRX, rheadRY);
    expandBbox(rheadLX, rheadLY);
    expandBbox(rsBRX, rsBRY);
    expandBbox(rsBLX, rsBLY);

    // Triangle 1: Arrowhead (tip, head-right, head-left)
    Graphics::fillTriangle(*m_lcd,
        rtipX, rtipY, rheadRX, rheadRY, rheadLX, rheadLY,
        ARROW_COLOR);

    // Triangle 2: Shaft right half (shaft-top-right, shaft-bottom-right, shaft-bottom-left)
    Graphics::fillTriangle(*m_lcd,
        rsTRX, rsTRY, rsBRX, rsBRY, rsBLX, rsBLY,
        ARROW_COLOR);

    // Triangle 3: Shaft left half (shaft-top-right, shaft-bottom-left, shaft-top-left)
    Graphics::fillTriangle(*m_lcd,
        rsTRX, rsTRY, rsBLX, rsBLY, rsTLX, rsTLY,
        ARROW_COLOR);
}

// ============================================================================
// Rotation ring (direct scanline — no green flash)
// ============================================================================

void IndicatorService::drawRing() {
    // Draw only the ring band pixels directly. No full-circle fill/clear,
    // so there is no visible flash during rendering.
    Graphics::fillRing(*m_lcd, SCREEN_CX, SCREEN_CY, RING_OUTER_R, RING_INNER_R,
                        CIRCLE_COLOR);

    // Expand bbox for ring
    expandBbox(static_cast<int16_t>(SCREEN_CX - RING_OUTER_R),
               static_cast<int16_t>(SCREEN_CY - RING_OUTER_R));
    expandBbox(static_cast<int16_t>(SCREEN_CX + RING_OUTER_R),
               static_cast<int16_t>(SCREEN_CY + RING_OUTER_R));
}

// ============================================================================
// Chevrons (bold triangular markers on circumference)
// ============================================================================

void IndicatorService::drawChevrons(int8_t direction) {
    // Place 4 chevrons at 0, 90, 180, 270 degrees on the ring.
    // Each is a bold filled triangle pointing in the rotation direction.
    // Positioned at the mid-radius of the ring for visibility.

    static constexpr uint16_t chevronAngles[4] = {0, 90, 180, 270};
    int16_t midR = static_cast<int16_t>((RING_OUTER_R + RING_INNER_R) / 2);

    for (int i = 0; i < 4; i++) {
        uint16_t posAngle = Graphics::deg_to_angle1024(chevronAngles[i]);

        // Center of chevron on ring mid-radius
        int16_t cx, cy;
        Graphics::rotate_point(0, static_cast<int16_t>(-midR), posAngle, cx, cy);
        cx += SCREEN_CX;
        cy += SCREEN_CY;

        // Tangent direction for CW/CCW
        uint16_t tangentAngle;
        if (direction > 0) {
            tangentAngle = Graphics::deg_to_angle1024(
                static_cast<uint16_t>((chevronAngles[i] + 90) % 360));
        } else {
            tangentAngle = Graphics::deg_to_angle1024(
                static_cast<uint16_t>((chevronAngles[i] + 270) % 360));
        }

        // Chevron tip: offset in tangent direction
        int16_t tipDx, tipDy;
        Graphics::rotate_point(0, static_cast<int16_t>(-CHEVRON_SIZE), tangentAngle,
                               tipDx, tipDy);

        // Base: perpendicular to tangent (radial direction)
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
