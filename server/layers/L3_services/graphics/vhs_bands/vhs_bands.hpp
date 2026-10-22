/**
 * @file vhs_bands.hpp
 * @brief VHS tracking bands: two bands of colour bars that roll down the panel, each with a
 *        bright tracking line on top, a tear (each row slides sideways) and noise that re-rolls
 *        every step. Shared by the motion indicator, the saver and the E-STOP page.
 *
 * The bands follow a clock (step(nowMs)), so they move at the same speed whatever the frame
 * rate. A screen composes its rows as usual and lays the band over the pixels it wants under
 * it (row(y).apply(), or .pixel() one at a time): the bars blend into what is there, the
 * tracking line and the noise cover it. After a step, dirtyRuns() gives the rows each band
 * left and the rows it reached; the screen repaints those through its own composer. Every
 * pixel is a pure function of the row, the step and the screen's own pixel, so overlapping
 * repaints agree. Integer maths only; the noise is so many dots a row at hashed columns.
 *
 * Header only, no hardware: the host tests compose rows with it directly.
 */

#ifndef VHS_BANDS_HPP
#define VHS_BANDS_HPP

#include <stdint.h>
#include <string.h>

namespace Graphics {

class VhsBands {
public:
    static constexpr int SCREEN_W = 240;             ///< the panel's width and height
    static constexpr int SCREEN_H = 320;
    static constexpr int COUNT = 2;

    /// A band's rows, speed (px/s, downward), top row at t = 0 and tear (px either way). The
    /// owner found 55 / 90 px/s too fast (2026-10-21)
    static int height(int i) { return i == 0 ? 12 : 6; }
    static uint32_t speed(int i) { return i == 0 ? 24u : 36u; }
    static uint32_t start(int i) { return i == 0 ? 30u : 200u; }
    static int tear(int i) { return i == 0 ? 9 : 5; }

    /// Nothing drawn yet: the next dirtyRuns() paints the bands where they are
    void reset() {
        for (int i = 0; i < COUNT; i++) m_top[i] = m_prev[i] = -1;
    }

    /// The bands' speed, % of speed() (a screen's own pace: the saver's are quicker)
    void setSpeedPercent(uint16_t pct) { m_speedPct = pct; }

    /// Move the bands to the clock (ms); the noise and the tear re-roll
    void step(uint32_t nowMs) {
        for (int i = 0; i < COUNT; i++) {
            m_prev[i] = m_top[i];
            m_top[i] = static_cast<int16_t>((start(i) + static_cast<uint64_t>(nowMs) * speed(i) * m_speedPct / 100000u) %
                                            static_cast<uint32_t>(SCREEN_H));
        }
        m_frame++;
    }

    /// A band's top row (-1 before the first step)
    int16_t top(int i) const { return m_top[i]; }

    /// A band covers row y now
    bool covers(int y) const {
        for (int i = 0; i < COUNT; i++) {
            if (m_top[i] >= 0 && (y - m_top[i] + SCREEN_H) % SCREEN_H < height(i)) return true;
        }
        return false;
    }

    /// The rows to repaint after a step: each band's old rows and new ones, as runs that do not
    /// wrap; paint(firstRow, lastRow) for each
    template <typename F>
    void dirtyRuns(F paint) const {
        for (int i = 0; i < COUNT; i++) {
            const int h = height(i);
            const int moved = (m_top[i] - m_prev[i] + SCREEN_H) % SCREEN_H;
            if (m_prev[i] < 0) {
                run(m_top[i], h, paint);
            } else if (moved <= h) {
                run(m_prev[i], moved + h, paint);
            } else {
                run(m_prev[i], h, paint);
                run(m_top[i], h, paint);
            }
        }
    }

    /// One row's band, worked out once and laid over that row's pixels
    struct Row {
        bool on = false;
        bool line = false;                           // the tracking line: covers what is there
        int shift = 0;                               // the tear, and the bars' phase
        uint16_t bar[2] = {};                        // hot pink and cyan, scanline-dimmed on odd rows
        uint16_t trackColour = 0, noiseColour = 0;
        uint8_t noise[SCREEN_W / 8] = {};            // the noise dots' columns, a bit each

        bool noisy(int x) const { return (noise[x >> 3] & (1u << (x & 7))) != 0; }

        /// The pixel at column x, over `under` (RGB565)
        uint16_t pixel(int x, uint16_t under) const {
            if (!on) return under;
            if (line) return trackColour;
            if (noisy(x)) return noiseColour;
            return mix(under, bar[((x + shift) / BAR_W) & 1], BAR_MIX);
        }

        /// Lay the band over columns [a, b] of a row of big-endian RGB565 bytes whose first
        /// pixel is column x0
        void apply(uint8_t* buf, int x0, int a, int b) const {
            if (!on) return;
            if (a < 0) a = 0;
            if (b > SCREEN_W - 1) b = SCREEN_W - 1;
            for (int x = a; x <= b; x++) {
                uint8_t* p = buf + (x - x0) * 2;
                const uint16_t c = pixel(x, static_cast<uint16_t>((p[0] << 8) | p[1]));
                p[0] = static_cast<uint8_t>(c >> 8);
                p[1] = static_cast<uint8_t>(c & 0xFF);
            }
        }
    };

    Row row(int y) const {
        Row r;
        int band = -1, rr = 0;
        for (int i = 0; i < COUNT && band < 0; i++) {
            if (m_top[i] < 0) continue;
            const int d = (y - m_top[i] + SCREEN_H) % SCREEN_H;
            if (d < height(i)) { band = i; rr = d; }
        }
        if (band < 0) return r;
        r.on = true;
        const bool scan = (y & 1) != 0;
        r.trackColour = dimmed(rgb(235, 220, 255), scan);
        if (rr == 0) { r.line = true; return r; }
        r.bar[0] = dimmed(rgb(255, 0, 128), scan);
        r.bar[1] = dimmed(rgb(5, 217, 232), scan);
        r.noiseColour = dimmed(rgb(230, 230, 255), scan);
        const int t = tear(band);
        r.shift = static_cast<int>(hash16(static_cast<uint32_t>(rr), m_frame, static_cast<uint32_t>(band + 1)) %
                                   static_cast<uint32_t>(2 * t + 1)) - t + 10 * BAR_W;
        for (int k = 0; k < NOISE_PER_ROW; k++) {
            const uint32_t x = hash16(static_cast<uint32_t>(k), static_cast<uint32_t>(y), m_frame + 7u) % SCREEN_W;
            r.noise[x >> 3] = static_cast<uint8_t>(r.noise[x >> 3] | (1u << (x & 7)));
        }
        return r;
    }

    /// 16 bits of noise for a pixel and a seed (also the backdrops' speckle)
    static uint32_t hash16(uint32_t x, uint32_t y, uint32_t seed) {
        uint32_t v = (x * 73856093u) ^ (y * 19349663u) ^ (seed * 83492791u);
        v = (v ^ (v >> 13)) * 0x5BD1E995u;
        return (v ^ (v >> 15)) & 0xFFFFu;
    }

    static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
        return static_cast<uint16_t>(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
    }

    /// a toward b by t/256, per channel
    static uint16_t mix(uint16_t a, uint16_t b, int t) {
        const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
        const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
        return static_cast<uint16_t>(((ar + (br - ar) * t / 256) << 11) | ((ag + (bg - ag) * t / 256) << 5) |
                                     (ab + (bb - ab) * t / 256));
    }

private:
    static constexpr int BAR_W = 7;                  // px per colour bar
    static constexpr int BAR_MIX = 115;              // 256ths of the bar over what is there
    static constexpr int NOISE_PER_ROW = 29;         // ~12 % of a band's pixels

    int16_t m_top[COUNT] = {-1, -1};
    int16_t m_prev[COUNT] = {-1, -1};
    uint16_t m_frame = 0;
    uint16_t m_speedPct = 100;

    static uint16_t dimmed(uint16_t c, bool scan) { return scan ? mix(0, c, 205) : c; }

    template <typename F>
    static void run(int first, int count, F paint) {
        if (first + count <= SCREEN_H) {
            paint(first, first + count - 1);
        } else {                                     // wraps past the bottom
            paint(first, SCREEN_H - 1);
            paint(0, first + count - 1 - SCREEN_H);
        }
    }
};

} // namespace Graphics

#endif // VHS_BANDS_HPP
