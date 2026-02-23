/**
 * @file indicator_service.hpp
 * @brief Motion indicator rendering service
 *
 * Renders a directional arrow and/or rotation circle indicator on the LCD.
 * Supports partial screen updates (only redraws bounding box of previous frame).
 *
 * Layering: Parser -> DisplayTask API -> IndicatorService -> Graphics -> LCD driver
 *
 * No RTOS headers. No FreeRTOS types. LCD injected at init time.
 */

#ifndef INDICATOR_SERVICE_HPP
#define INDICATOR_SERVICE_HPP

#include <stdint.h>

// Forward declaration -- no driver header in the service interface
class LCD;

namespace Services {

class IndicatorService {
public:
    struct Params {
        uint16_t angle_deg;     // 0-359 (0 = up/north, clockwise)
        int8_t   rotation_dir;  // -1 (CCW), 0 (none), 1 (CW)
        bool     has_translation;
    };

    /**
     * @brief Inject LCD reference. Called once from composition root.
     * @param lcd Pointer to initialized LCD instance (not owned).
     */
    void init(LCD* lcd);

    /**
     * @brief Draw/update the motion indicator with partial screen update.
     *
     * Clears previous bounding box, draws new indicator, stores new bbox.
     * Idle params (rotation_dir=0, has_translation=false) clear the indicator.
     *
     * @param p Indicator parameters
     */
    void draw(const Params& p);

    /**
     * @brief Clear the indicator area.
     *
     * Equivalent to draw({0, 0, false}).
     */
    void clear();

private:
    LCD* m_lcd = nullptr;

    // Previous bounding box for partial screen clear
    int16_t  m_prevBboxX = 0;
    int16_t  m_prevBboxY = 0;
    uint16_t m_prevBboxW = 0;
    uint16_t m_prevBboxH = 0;
    bool     m_hasDrawn = false;
    bool     m_screenCleared = false;  // First-draw full screen clear

    // Current bounding box being accumulated during draw
    int16_t  m_curMinX = 0;
    int16_t  m_curMinY = 0;
    int16_t  m_curMaxX = 0;
    int16_t  m_curMaxY = 0;

    // Indicator geometry constants
    static constexpr int16_t SCREEN_CX = 120;  // LCD::WIDTH / 2
    static constexpr int16_t SCREEN_CY = 160;  // LCD::HEIGHT / 2

    // Arrow: sized to fit inside the circle ring
    static constexpr int16_t ARROW_HALF_LEN = 55;    // Half of total arrow length
    static constexpr int16_t ARROW_HEAD_LEN = 28;    // Arrowhead triangle height
    static constexpr int16_t ARROW_HEAD_HALF_W = 20;  // Arrowhead half-width at base
    static constexpr int16_t ARROW_SHAFT_HALF_W = 5;  // Shaft half-width (thin)

    // Circle ring: bold, drawn via fillRing (direct scanline)
    static constexpr int16_t RING_OUTER_R = 95;   // Outer radius of ring
    static constexpr int16_t RING_INNER_R = 87;   // Inner radius (8px ring thickness)

    // Chevrons: bold triangular markers on circumference
    static constexpr int16_t CHEVRON_SIZE = 22;    // Chevron triangle size

    static constexpr uint16_t ARROW_COLOR   = 0xFFFF;  // White
    static constexpr uint16_t CIRCLE_COLOR  = 0x07E0;  // Green
    static constexpr uint16_t CHEVRON_COLOR = 0x07E0;  // Green
    static constexpr uint16_t BG_COLOR      = 0x0000;  // Black

    void clearPreviousBbox();
    void resetBbox();
    void expandBbox(int16_t x, int16_t y);
    void storeBbox();

    void drawArrow(uint16_t angle_deg);
    void drawRing();
    void drawChevrons(int8_t direction);
};

// Static global instance (no heap allocation)
extern IndicatorService g_indicatorService;

} // namespace Services

#endif // INDICATOR_SERVICE_HPP
