/**
 * @file indicator_service.hpp
 * @brief Motion indicator: heading arrow that fills with the speed, three concentric turn
 *        rings, direction chevrons, a readout and the way-out hint.
 *
 * draw() only records the target (Comms task, returns at once). tick() runs on the Display
 * task: it eases angle, speed and turn toward the target on the clock (the same speed whatever
 * the frame rate) and repaints only what changed, starting on the panel's tearing edge so the
 * write stays ahead of the scan. A repaint is one window, the square around the arrow disc or
 * around the ring zone, streamed row after row; the shapes' edges are worked out once per row
 * and every pixel is a couple of compares.
 *
 * No RTOS headers. LCD injected at init time.
 */

#ifndef INDICATOR_SERVICE_HPP
#define INDICATOR_SERVICE_HPP

#include "L3_services/boundary/iindicator_renderer.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include <stdint.h>

namespace Services {

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
    static constexpr int16_t CHEV = 14;            ///< chevron length and width
    static constexpr int16_t ZONE_OUT = 102;       ///< ring zone: rings and chevrons
    static constexpr int16_t ZONE_IN = 75;
    static constexpr int16_t ARROW_R = 72;         ///< arrow disc (inside ZONE_IN)
    static constexpr uint16_t READOUT_Y = CY + ZONE_OUT + 6;
    static constexpr uint16_t HINT_Y = Services::Canvas::HEIGHT - 12;

    static constexpr uint16_t ARROW_COLOR   = 0xF8DB;  // Theme::MAGENTA outline
    static constexpr uint16_t FILL_COLOR    = 0x079F;  // Theme::CYAN speed fill
    static constexpr uint16_t RING_LIT      = 0x079F;  // Theme::CYAN
    static constexpr uint16_t RING_UNLIT    = 0x3B31;  // Theme::GRID_MAJ hairline
    static constexpr uint16_t CHEVRON_COLOR = 0xF8DB;  // Theme::MAGENTA
    static constexpr uint16_t BG_COLOR      = 0x0842;  // Theme::BG
    static constexpr uint16_t HINT_COLOR    = 0x4A8D;  // Theme::DIM

    /// The eased state, for tests
    float currentAngle() const { return m_angle; }
    float currentSpeed() const { return m_speed; }
    float currentTurn() const { return m_turn; }

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
    bool m_arrowShown = false;
    int16_t m_arrowAngle = -1;               // 1024ths, as drawn
    int16_t m_arrowFill = -1;                // px of fill, as drawn
    bool m_ringsShown = false;
    uint8_t m_ringW[RING_COUNT] = {};        // px, as drawn
    bool m_ringLit[RING_COUNT] = {};
    int8_t m_chevDir = 0;
    char m_readout[48] = {};

    // Per-frame geometry (screen coordinates)
    float m_outerHead[3][2];
    float m_outerShaft[4][2];
    float m_innerHead[3][2];
    float m_innerShaft[4][2];
    float m_tailX = 0, m_tailY = 0, m_axisX = 0, m_axisY = 0, m_fillLen = 0;
    float m_chev[4][3][2];
    int32_t m_ringOut2[RING_COUNT] = {};
    int32_t m_ringIn2[RING_COUNT] = {};

    void buildArrow(uint16_t angle1024, float fillLen);
    void buildRings(const uint8_t* widths);
    void buildChevrons(int8_t dir);
    void renderFrame(bool rings, bool arrow, bool ringsShown, bool arrowShown);
    void renderReadout(int16_t speedPct, int16_t turnPct, int8_t dir);
};

extern IndicatorService g_indicatorService;

} // namespace Services

#endif // INDICATOR_SERVICE_HPP
