/**
 * @file trace_screen.hpp
 * @brief Trace monitor screen — live view of interface boundary events
 *
 * Four view modes accessible via RIGHT button:
 *
 * TRACE mode — scrollable text list of trace entries
 *   UP/DOWN: Scroll (UP pauses live feed, DOWN toward newest)
 *   CENTER: Jump to newest, resume live feed
 *   RIGHT: Next mode (Legend)
 *   LEFT: Exit
 *
 * LEGEND mode — color key + boundary filter toggles
 *   UP/DOWN: Navigate filter list
 *   CENTER: Toggle boundary filter on/off
 *   RIGHT: Next mode (Graph)
 *   LEFT: Back to Trace
 *
 * GRAPH mode — zoomable timeline view (Magic Trace / SystemView style)
 *   Shows colored activity blocks per boundary on a time axis.
 *   UP/DOWN: Zoom in/out (3px to 24px per block)
 *   CENTER: Pause/resume toggle
 *   RIGHT: Next mode (Service)
 *   LEFT: Back to Legend
 *
 * SERVICE mode — service-grouped timeline with boundary coloring
 *   Dynamic lanes per active service (Motion, Config, Safety, etc.)
 *   Blocks colored by boundary type showing driver chain visibility.
 *   UP/DOWN: Zoom in/out
 *   CENTER: Pause/resume toggle
 *   LEFT: Back to Graph
 */

#ifndef TRACE_SCREEN_HPP
#define TRACE_SCREEN_HPP

#include "ui/screen.hpp"
#include <stdint.h>
#include <stddef.h>

class LCD;

namespace UI {

class TraceScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::TERMINAL; }
    void render(LCD& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;

    // Trace screen manages its own redraws via blitTextLine (no full-screen clear needed)
    bool needsFullRedraw() const override { return false; }
    void clearRedrawFlag() override { }

private:
    enum class Mode : uint8_t { TRACE, LEGEND, GRAPH, SERVICE };

    void renderTrace(LCD& lcd);
    void renderLegend(LCD& lcd);
    void renderGraph(LCD& lcd);
    void renderService(LCD& lcd);
    InputResult handleTraceInput(JoyDirection dir);
    InputResult handleLegendInput(JoyDirection dir);
    InputResult handleGraphInput(JoyDirection dir);
    InputResult handleServiceInput(JoyDirection dir);

    bool m_needsRedraw = true;
    bool m_titleDrawn = false;
    Mode m_mode = Mode::TRACE;
    size_t m_lastTotal = 0;       // Last seen Trace::getTotal() value
    size_t m_pauseTotal = 0;      // Trace::getTotal() when pause started
    size_t m_scrollOffset = 0;    // Offset from bottom (0 = newest visible)
    bool m_autoScroll = true;
    uint8_t m_filterMask = 0xFF;  // Bitmask of enabled ITrace::Boundary values
    uint8_t m_legendCursor = 0;   // Selected row in legend mode (0-6)
    size_t m_matchCount = 0;      // Cached count of filtered entries
    uint8_t m_zoomLevel = 0;      // Graph zoom: 0=auto, 1-7 = fixed block widths

    static constexpr uint8_t VISIBLE_LINES = 24;
    static constexpr uint16_t LINE_HEIGHT = 12;
    static constexpr uint8_t LEGEND_ITEMS = 7;  // Number of boundary filters

    // Graph mode constants
    static constexpr uint8_t GRAPH_LANES = 6;
    static constexpr uint16_t LANE_LABEL_W = 30;  // px for "Xpt" label (scale 3 = 8px/ch)
    static constexpr uint16_t TIMELINE_X = LANE_LABEL_W + 4;  // timeline start x
    static constexpr uint8_t ZOOM_LEVELS = 7;

    // Service mode constants
    static constexpr uint8_t MAX_SERVICE_LANES = 9;  // Trace::SVC_COUNT

    // Service mode layout cache (reduce flicker by only clearing on layout change)
    uint16_t m_lastSvcMask = 0;   // previous active service bitmask
    uint8_t  m_lastLaneCount = 0; // previous number of active lanes
};

} // namespace UI

#endif // TRACE_SCREEN_HPP
