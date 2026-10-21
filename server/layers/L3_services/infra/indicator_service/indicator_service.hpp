/**
 * @file indicator_service.hpp
 * @brief Motion indicator: heading arrow that fills with the speed, three concentric turn
 *        rings, direction chevrons, a readout and the way-out hint, on a VHS backdrop.
 *
 * draw() only records the target (Comms task, returns at once). tick() runs on the Display
 * task: it eases angle, speed and turn toward the target on the clock (the same speed whatever
 * the frame rate) and repaints only what changed, starting on the panel's tearing edge so the
 * write stays ahead of the scan.
 *
 * Every pixel comes from one row composer (composeRow): the VHS backdrop (a slow violet
 * banding, scanlines, speckle), the dial's dark disc, the rings, chevrons and arrow on it, and
 * the readout and hint plates. A repaint is a window streamed row after row through it: the
 * square around the arrow disc or the ring zone when those change, the readout plate when its
 * text changes, and each tick the rows of two VHS tracking bands that roll down the screen
 * (the rows they left and the rows they reached, ~6k px a tick). The bands pass behind the
 * dial and the plates; their colours, the tear and the noise are integer maths per pixel.
 * The arrow keeps its frame rate: a tick that repaints the dial's square moves the bands only
 * if they have waited BAND_MAX_WAIT_MS (they follow the clock, so they jump further), and the
 * bands alone skip the wait for the tearing edge (a torn VHS band looks like one).
 * (Owner 2026-10-21: a Hotline Miami / synthwave look, VHS lines that move.)
 *
 * No RTOS headers. LCD injected at init time.
 */

#ifndef INDICATOR_SERVICE_HPP
#define INDICATOR_SERVICE_HPP

#include "L3_services/boundary/iindicator_renderer.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include <stdint.h>

namespace Services {

/// RGB565 from 8-bit channels
constexpr uint16_t indicatorRgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

class IndicatorService : public Services::IIndicatorRenderer {
public:
    using Params = Services::IndicatorParams;

    void init(Services::ICanvas* lcd);

    /// New target (any task); the Display task animates toward it
    void draw(const Params& p) override;

    /// One animation step and the repaint it needs (Display task, remote mode); nowMs: the clock
    void tick(uint32_t nowMs) override;

    /// The screen was drawn by someone else: the next tick starts from a clear panel
    void invalidate() override;

    /// Hide the indicator (target with no translation and no turn)
    void clear();

    // --- geometry (public for the host tests) ---------------------------------------------
    static constexpr int16_t CX = Services::Canvas::WIDTH / 2;
    static constexpr int16_t CY = Services::Canvas::HEIGHT / 2;
    static constexpr uint8_t RING_COUNT = 3;       ///< one per third of the turn magnitude
    static constexpr int16_t RING_OUT_R = 95;      ///< outer edge of the outer ring
    static constexpr int16_t RING_PITCH = 6;       ///< outer edge to outer edge
    static constexpr int16_t RING_W_MAX = 4;       ///< a ring's width when its step is complete
    /// Chevrons span all the rings: the base covers the band from the inner ring's inside edge
    /// to the outer ring's outside edge with a little to spare, centred on the band (they were
    /// 14 px on the outer ring alone and looked tiny, owner 2026-10-21)
    static constexpr int16_t RING_IN_R = RING_OUT_R - (RING_COUNT - 1) * RING_PITCH - RING_W_MAX;
    static constexpr int16_t CHEV_BASE = (RING_OUT_R - RING_IN_R) + 6;      ///< radial width
    static constexpr int16_t CHEV_LEN = 20;                                 ///< tangential length
    static_assert((RING_OUT_R + RING_IN_R) / 2 + CHEV_BASE / 2 <= 102 &&
                  (RING_OUT_R + RING_IN_R) / 2 - CHEV_BASE / 2 >= 75, "chevrons inside the ring zone");
    static constexpr int16_t ZONE_OUT = 102;       ///< ring zone: rings and chevrons
    static constexpr int16_t ZONE_IN = 75;
    static constexpr int16_t ARROW_R = 72;         ///< arrow disc (inside ZONE_IN)
    static constexpr int16_t DISC_R = ZONE_OUT + 2;   ///< the dial's dark disc; its last 2 px a rim
    static constexpr int16_t GHOST_PX = 2;         ///< the arrow's RGB split: cyan left, red right

    // The text plates: the medium font (8 x 12) on a dark plate the bands pass behind
    static constexpr int16_t TEXT_W = Services::Canvas::MED_CHAR_W;
    static constexpr int16_t TEXT_H = Services::Canvas::MED_CHAR_H;
    static constexpr int16_t READOUT_CHARS = 19;   ///< "V  60%   R  66% CW " (fixed width)
    static constexpr int16_t READOUT_Y = CY + ZONE_OUT + 6;
    static constexpr int16_t READOUT_X = CX - READOUT_CHARS * TEXT_W / 2;
    static constexpr int16_t READOUT_L = READOUT_X - 4, READOUT_R = READOUT_X + READOUT_CHARS * TEXT_W + 3;
    static constexpr int16_t READOUT_T = READOUT_Y - 2, READOUT_B = READOUT_Y + TEXT_H + 1;
    static_assert(READOUT_T > CY + DISC_R, "the readout plate clears the disc");
    /// The way-out hint in the medium font: the 6 x 8 one was hard to read on the panel
    /// (owner 2026-10-21). Painted with the screen, so its size costs no frame time
    static constexpr int16_t HINT_CHARS = 20;      ///< "hold CENTER 2s: menu"
    static constexpr int16_t HINT_Y = Services::Canvas::HEIGHT - TEXT_H - 4;
    static constexpr int16_t HINT_X = CX - HINT_CHARS * TEXT_W / 2;
    static constexpr int16_t HINT_L = HINT_X - 4, HINT_R = HINT_X + HINT_CHARS * TEXT_W + 3;
    static constexpr int16_t HINT_T = HINT_Y - 2, HINT_B = HINT_Y + TEXT_H + 1;
    static_assert(HINT_T > READOUT_B, "the plates do not overlap");

    static constexpr uint16_t DIAL_BG        = indicatorRgb(14, 2, 30);     // the disc and the plates
    static constexpr uint16_t DISC_RIM       = indicatorRgb(115, 18, 52);   // hot pink, dimmed
    static constexpr uint16_t RING_LIT       = indicatorRgb(255, 41, 117);  // hot pink
    static constexpr uint16_t RING_GLOW      = indicatorRgb(255, 230, 80);  // a lit ring's inside line
    static constexpr uint16_t RING_UNLIT     = indicatorRgb(60, 0, 70);
    static constexpr uint16_t CHEVRON_COLOR  = indicatorRgb(255, 211, 25);  // yellow
    static constexpr uint16_t ARROW_COLOR    = 0xFFFF;                      // white outline
    static constexpr uint16_t FILL_COLOR     = indicatorRgb(255, 240, 250); // the speed fill, warm white
    static constexpr uint16_t GHOST_LEFT     = indicatorRgb(5, 217, 232);   // cyan
    static constexpr uint16_t GHOST_RIGHT    = indicatorRgb(255, 0, 90);    // red
    static constexpr uint16_t PLATE_RIM      = indicatorRgb(153, 24, 70);
    static constexpr uint16_t READOUT_COLOR  = indicatorRgb(255, 240, 250);
    static constexpr uint16_t READOUT_SHADOW = RING_LIT;                    // one pixel down and right
    static constexpr uint16_t HINT_COLOR     = 0xDF3E;                      // Theme::TEXT

    static constexpr uint8_t BAND_COUNT = 2;       ///< VHS tracking bands (the table is in the .cpp)
    static constexpr uint32_t BAND_MAX_WAIT_MS = 100;   ///< the longest the bands hold for the arrow

    /// The eased state, for tests
    float currentAngle() const { return m_angle; }
    float currentSpeed() const { return m_speed; }
    float currentTurn() const { return m_turn; }
    /// A band's top row as drawn (-1 before the first frame), for tests
    int16_t bandTop(uint8_t i) const { return m_bandTop[i]; }

private:
    Services::ICanvas* m_lcd = nullptr;

    // Target, packed so a 32-bit store from the Comms task is atomic on the M4
    volatile uint32_t m_target = 0;          // 0 = nothing requested yet
    static uint32_t pack(const Params& p);

    // Eased state
    uint32_t m_lastTickMs = 0;
    bool m_ticked = false;
    float m_angle = 0.0f;                    // degrees, clockwise from up
    float m_speed = 0.0f;                    // 0..100
    float m_turn = 0.0f;                     // 0..100

    // What is on the panel
    bool m_screenValid = false;
    bool m_synced = false;                   // this tick already waited for the tearing edge
    bool m_arrowShown = false;
    int16_t m_arrowAngle = -1;               // 1024ths, as drawn
    int16_t m_arrowFill = -1;                // px of fill, as drawn
    bool m_ringsShown = false;
    uint8_t m_ringW[RING_COUNT] = {};        // px, as drawn
    bool m_ringLit[RING_COUNT] = {};
    int8_t m_chevDir = 0;
    char m_readout[48] = {};
    int16_t m_bandTop[BAND_COUNT] = {-1, -1};
    uint16_t m_bandFrame = 0;                // seeds the tear and the noise
    uint32_t m_bandStepMs = 0;               // when the bands last moved

    // Per-frame geometry (screen coordinates)
    float m_outerHead[3][2];
    float m_outerShaft[4][2];
    float m_innerHead[3][2];
    float m_innerShaft[4][2];
    float m_tailX = 0, m_tailY = 0, m_axisX = 0, m_axisY = 0, m_fillLen = 0;
    float m_chev[4][3][2];
    int32_t m_ringOut2[RING_COUNT] = {};
    int32_t m_ringIn2[RING_COUNT] = {};
    int32_t m_ringGlow2[RING_COUNT] = {};    // a lit ring's inside line: d2 up to this

    void buildArrow(uint16_t angle1024, float fillLen);
    void buildRings(const uint8_t* widths);
    void buildChevrons(int8_t dir);
    void sync();
    void composeRow(int y, int x0, int x1, uint8_t* buf) const;
    void paintRows(int top, int bottom, int x0, int x1, bool onTearingEdge = true);
    void paintBandRows(int top, int count);
    void renderReadout(int16_t speedPct, int16_t turnPct, int8_t dir);
};

extern IndicatorService g_indicatorService;

} // namespace Services

#endif // INDICATOR_SERVICE_HPP
