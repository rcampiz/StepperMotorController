/**
 * @file primitives.hpp
 * @brief Stateless graphics primitives for LCD rendering
 *
 * Provides filled triangle (scanline), circle outline (midpoint/Bresenham),
 * and filled circle. All functions take Harness::ICanvas& as first parameter.
 *
 * No RTOS. No global state. Pure utility library.
 * Dependency direction: graphics -> drivers (allowed).
 */

#ifndef GRAPHICS_PRIMITIVES_HPP
#define GRAPHICS_PRIMITIVES_HPP

#include "harness/pins/icanvas.hpp"
#include <stdint.h>

namespace Graphics {

/**
 * @brief Swap two int16_t values
 */
inline void swap16(int16_t& a, int16_t& b) {
    int16_t t = a;
    a = b;
    b = t;
}

/**
 * @brief Filled triangle via scanline algorithm
 *
 * Sorts vertices by Y, walks edges top-to-bottom, fills horizontal spans
 * using lcd.fillRect(). Efficient because fillRect uses a single setWindow
 * call per scanline row.
 *
 * Handles degenerate triangles (collinear points) gracefully.
 */
inline void fillTriangle(Harness::ICanvas& lcd,
    int16_t x0, int16_t y0,
    int16_t x1, int16_t y1,
    int16_t x2, int16_t y2,
    uint16_t color)
{
    // Sort vertices by Y coordinate (y0 <= y1 <= y2)
    if (y0 > y1) { swap16(y0, y1); swap16(x0, x1); }
    if (y1 > y2) { swap16(y1, y2); swap16(x1, x2); }
    if (y0 > y1) { swap16(y0, y1); swap16(x0, x1); }

    // Degenerate: all on same row
    if (y0 == y2) {
        int16_t minX = x0, maxX = x0;
        if (x1 < minX) minX = x1;
        if (x2 < minX) minX = x2;
        if (x1 > maxX) maxX = x1;
        if (x2 > maxX) maxX = x2;
        // Clip to screen
        if (minX < 0) minX = 0;
        if (maxX >= Harness::Canvas::WIDTH) maxX = static_cast<int16_t>(Harness::Canvas::WIDTH - 1);
        if (minX <= maxX && y0 >= 0 && y0 < Harness::Canvas::HEIGHT) {
            lcd.fillRect(static_cast<uint16_t>(minX), static_cast<uint16_t>(y0),
                         static_cast<uint16_t>(maxX - minX + 1), 1, color);
        }
        return;
    }

    // Fixed-point edge walking (16.16 for subpixel accuracy)
    // Upper half: y0 to y1
    int32_t dy01 = y1 - y0;
    int32_t dy02 = y2 - y0;
    int32_t dy12 = y2 - y1;

    for (int16_t y = y0; y <= y2; y++) {
        if (y < 0) continue;
        if (y >= Harness::Canvas::HEIGHT) break;

        int16_t xa, xb;

        // Long edge (y0 to y2) is always active
        if (dy02 != 0) {
            xa = static_cast<int16_t>(x0 + static_cast<int32_t>(x2 - x0) * (y - y0) / dy02);
        } else {
            xa = x0;
        }

        // Short edge: y0-y1 for upper half, y1-y2 for lower half
        if (y < y1) {
            if (dy01 != 0) {
                xb = static_cast<int16_t>(x0 + static_cast<int32_t>(x1 - x0) * (y - y0) / dy01);
            } else {
                xb = x0;
            }
        } else {
            if (dy12 != 0) {
                xb = static_cast<int16_t>(x1 + static_cast<int32_t>(x2 - x1) * (y - y1) / dy12);
            } else {
                xb = x1;
            }
        }

        // Ensure xa <= xb
        if (xa > xb) swap16(xa, xb);

        // Clip to screen
        if (xa < 0) xa = 0;
        if (xb >= Harness::Canvas::WIDTH) xb = static_cast<int16_t>(Harness::Canvas::WIDTH - 1);
        if (xa > xb) continue;

        lcd.fillRect(static_cast<uint16_t>(xa), static_cast<uint16_t>(y),
                     static_cast<uint16_t>(xb - xa + 1), 1, color);
    }
}

/**
 * @brief Circle outline via midpoint/Bresenham algorithm (8-way symmetry)
 *
 * Draws a 1px circle outline. For thicker outlines, call with r and r-1.
 */
inline void drawCircleOutline(Harness::ICanvas& lcd,
    int16_t cx, int16_t cy, int16_t r,
    uint16_t color)
{
    if (r <= 0) return;

    int16_t x = 0;
    int16_t y = r;
    auto d = static_cast<int16_t>(1 - r);

    while (x <= y) {
        // 8-way symmetry
        lcd.drawPixel(static_cast<uint16_t>(cx + x), static_cast<uint16_t>(cy + y), color);
        lcd.drawPixel(static_cast<uint16_t>(cx - x), static_cast<uint16_t>(cy + y), color);
        lcd.drawPixel(static_cast<uint16_t>(cx + x), static_cast<uint16_t>(cy - y), color);
        lcd.drawPixel(static_cast<uint16_t>(cx - x), static_cast<uint16_t>(cy - y), color);
        lcd.drawPixel(static_cast<uint16_t>(cx + y), static_cast<uint16_t>(cy + x), color);
        lcd.drawPixel(static_cast<uint16_t>(cx - y), static_cast<uint16_t>(cy + x), color);
        lcd.drawPixel(static_cast<uint16_t>(cx + y), static_cast<uint16_t>(cy - x), color);
        lcd.drawPixel(static_cast<uint16_t>(cx - y), static_cast<uint16_t>(cy - x), color);

        if (d < 0) {
            d = static_cast<int16_t>(d + 2 * x + 3);
        } else {
            d = static_cast<int16_t>(d + 2 * (x - y) + 5);
            y--;
        }
        x++;
    }
}

/**
 * @brief Filled circle via midpoint algorithm with horizontal spans
 *
 * Uses fillRect for each horizontal scanline pair (efficient bulk writes).
 */
inline void fillCircle(Harness::ICanvas& lcd,
    int16_t cx, int16_t cy, int16_t r,
    uint16_t color)
{
    if (r <= 0) {
        lcd.drawPixel(static_cast<uint16_t>(cx), static_cast<uint16_t>(cy), color);
        return;
    }

    int16_t x = 0;
    int16_t y = r;
    auto d = static_cast<int16_t>(1 - r);

    // Draw initial horizontal line and top/bottom points
    lcd.fillRect(static_cast<uint16_t>(cx - r), static_cast<uint16_t>(cy),
                 static_cast<uint16_t>(2 * r + 1), 1, color);

    while (x < y) {
        if (d < 0) {
            d = static_cast<int16_t>(d + 2 * x + 3);
        } else {
            // y decreased — draw the horizontal spans at old y
            auto spanX = static_cast<int16_t>(cx - x);
            auto spanW = static_cast<uint16_t>(2 * x + 1);

            if (cy + y >= 0 && cy + y < Harness::Canvas::HEIGHT && spanX < Harness::Canvas::WIDTH) {
                uint16_t sx = (spanX < 0) ? 0 : static_cast<uint16_t>(spanX);
                lcd.fillRect(sx, static_cast<uint16_t>(cy + y), spanW, 1, color);
            }
            if (cy - y >= 0 && cy - y < Harness::Canvas::HEIGHT && spanX < Harness::Canvas::WIDTH) {
                uint16_t sx = (spanX < 0) ? 0 : static_cast<uint16_t>(spanX);
                lcd.fillRect(sx, static_cast<uint16_t>(cy - y), spanW, 1, color);
            }

            d = static_cast<int16_t>(d + 2 * (x - y) + 5);
            y--;
        }
        x++;

        // Draw horizontal spans at x, y positions
        {
            auto spanX = static_cast<int16_t>(cx - y);
            auto spanW = static_cast<uint16_t>(2 * y + 1);

            if (cy + x >= 0 && cy + x < Harness::Canvas::HEIGHT && spanX < Harness::Canvas::WIDTH) {
                uint16_t sx = (spanX < 0) ? 0 : static_cast<uint16_t>(spanX);
                lcd.fillRect(sx, static_cast<uint16_t>(cy + x), spanW, 1, color);
            }
            if (cy - x >= 0 && cy - x < Harness::Canvas::HEIGHT && spanX < Harness::Canvas::WIDTH) {
                uint16_t sx = (spanX < 0) ? 0 : static_cast<uint16_t>(spanX);
                lcd.fillRect(sx, static_cast<uint16_t>(cy - x), spanW, 1, color);
            }
        }
    }
}

/**
 * @brief Integer square root via bit-by-bit method
 *
 * Returns floor(sqrt(n)). Handles values up to 32767^2.
 * 8 iterations for radii up to 255.
 */
inline int16_t isqrt16(int32_t n) {
    if (n <= 0) return 0;
    int16_t x = 0;
    int16_t bit = 1 << 7;
    while (bit > 0) {
        auto t = static_cast<int16_t>(x | bit);
        if (static_cast<int32_t>(t) * t <= n) x = t;
        bit >>= 1;
    }
    return x;
}

/**
 * @brief Filled ring (annulus) via scanline
 *
 * Draws only the ring band pixels between outerR and innerR.
 * For each scanline row, computes outer and inner circle x-extents
 * and draws only the two side bands. No full-circle fill/clear needed,
 * so there is no visual flash during rendering.
 *
 * Total pixel count = pi * (outerR^2 - innerR^2), much less than
 * two full fillCircle calls.
 */
inline void fillRing(Harness::ICanvas& lcd,
    int16_t cx, int16_t cy, int16_t outerR, int16_t innerR,
    uint16_t color)
{
    if (outerR <= 0) return;
    if (innerR < 0) innerR = 0;

    auto outerR2 = static_cast<int32_t>(outerR) * outerR;
    auto innerR2 = static_cast<int32_t>(innerR) * innerR;

    for (int16_t dy = -outerR; dy <= outerR; dy++) {
        auto screenY = static_cast<int16_t>(cy + dy);
        if (screenY < 0) continue;
        if (screenY >= Harness::Canvas::HEIGHT) break;

        auto dy2 = static_cast<int32_t>(dy) * dy;
        int16_t outerX = isqrt16(outerR2 - dy2);

        if (dy > -innerR && dy < innerR) {
            // Row intersects inner circle — draw two side bands
            int16_t innerX = isqrt16(innerR2 - dy2);

            // Left band: cx - outerX to cx - innerX - 1
            auto lStart = static_cast<int16_t>(cx - outerX);
            int16_t lEnd   = static_cast<int16_t>(cx - innerX - 1);
            if (lStart < 0) lStart = 0;
            if (lEnd >= Harness::Canvas::WIDTH) lEnd = static_cast<int16_t>(Harness::Canvas::WIDTH - 1);
            if (lStart <= lEnd) {
                lcd.fillRect(static_cast<uint16_t>(lStart), static_cast<uint16_t>(screenY),
                             static_cast<uint16_t>(lEnd - lStart + 1), 1, color);
            }

            // Right band: cx + innerX + 1 to cx + outerX
            auto rStart = static_cast<int16_t>(cx + innerX + 1);
            int16_t rEnd   = static_cast<int16_t>(cx + outerX);
            if (rStart < 0) rStart = 0;
            if (rEnd >= Harness::Canvas::WIDTH) rEnd = static_cast<int16_t>(Harness::Canvas::WIDTH - 1);
            if (rStart <= rEnd) {
                lcd.fillRect(static_cast<uint16_t>(rStart), static_cast<uint16_t>(screenY),
                             static_cast<uint16_t>(rEnd - rStart + 1), 1, color);
            }
        } else {
            // Row outside inner circle — draw full span
            auto spanStart = static_cast<int16_t>(cx - outerX);
            int16_t spanEnd   = static_cast<int16_t>(cx + outerX);
            if (spanStart < 0) spanStart = 0;
            if (spanEnd >= Harness::Canvas::WIDTH) spanEnd = static_cast<int16_t>(Harness::Canvas::WIDTH - 1);
            if (spanStart <= spanEnd) {
                lcd.fillRect(static_cast<uint16_t>(spanStart), static_cast<uint16_t>(screenY),
                             static_cast<uint16_t>(spanEnd - spanStart + 1), 1, color);
            }
        }
    }
}

} // namespace Graphics

#endif // GRAPHICS_PRIMITIVES_HPP
