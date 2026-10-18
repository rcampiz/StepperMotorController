/**
 * @file trace_screen.hpp
 * @brief Trace monitor screen — live view of interface boundary events
 *
 * A chrome screen (title tab, status line and footer drawn by the ScreenManager); the
 * views draw between Theme::CONTENT_Y and Theme::CONTENT_BOTTOM and repaint only what
 * changed. Four view modes accessible via RIGHT button:
 *
 * TRACE view — scrollable text list of trace entries
 *   UP/DOWN: Scroll while paused (UP toward older, DOWN toward newest)
 *   CENTER: Pause / resume the live feed (resume jumps to newest)
 *   RIGHT: Next view (Legend)
 *   LEFT: Exit
 *
 * LEGEND view — color key + boundary filter toggles
 *   UP/DOWN: Navigate filter list
 *   CENTER: Toggle boundary filter on/off
 *   RIGHT: Next view (Timeline)
 *   LEFT: Back to Trace
 *
 * TIMELINE view — zoomable timeline (Magic Trace / SystemView style)
 *   Shows colored activity blocks per layer lane on a time axis.
 *   UP/DOWN: Zoom in/out (auto, then 3px to 24px per block)
 *   CENTER: Pause/resume toggle
 *   LEFT/RIGHT live: Legend / Services view; paused: pan older / newer
 *
 * SERVICES view — service-grouped timeline with boundary coloring
 *   One lane per service (Motion, Config, Safety, etc.)
 *   Blocks colored by boundary type showing driver chain visibility.
 *   UP/DOWN: Zoom in/out
 *   CENTER: Pause/resume toggle
 *   LEFT live: back to Timeline; paused: LEFT/RIGHT pan
 */

#ifndef TRACE_SCREEN_HPP
#define TRACE_SCREEN_HPP

#include "ui/screen.hpp"
#include <stdint.h>
#include <stddef.h>

namespace UI {

class TraceScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::TERMINAL; }
    void render(Services::ICanvas& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;

    // Full clear + chrome only on activation and view switch; everything else is diffed
    bool needsFullRedraw() const override { return m_needsRedraw; }
    void clearRedrawFlag() override { m_needsRedraw = false; }
    bool usesChrome() const override { return true; }
    const char* title() const override;
    const char* centerHint() const override;

private:
    enum class Mode : uint8_t { TRACE, LEGEND, GRAPH, SERVICE };

    void switchMode(Mode m);
    void renderTrace(Services::ICanvas& lcd);
    void renderLegend(Services::ICanvas& lcd);
    void renderLanes(Services::ICanvas& lcd, bool service);
    InputResult handleTraceInput(JoyDirection dir);
    InputResult handleLegendInput(JoyDirection dir);
    InputResult handleGraphInput(JoyDirection dir);
    InputResult handleServiceInput(JoyDirection dir);

    /// One small-font line, blitted only when its text or colours differ from `cache`
    static void cachedLine(Services::ICanvas& lcd, uint16_t x, uint16_t y, uint16_t w,
                           uint16_t h, const char* text, const uint16_t* colors,
                           uint16_t fg, uint32_t& cache);

    bool m_needsRedraw = true;    // ScreenManager clears the page and redraws the chrome
    bool m_full = true;           // static parts of the current view need painting
    bool m_dirty = true;          // input changed what the view shows
    Mode m_mode = Mode::TRACE;
    size_t m_lastTotal = 0;       // Last seen Trace::getTotal() value
    size_t m_pauseTotal = 0;      // Trace::getTotal() when pause started
    size_t m_scrollOffset = 0;    // Offset from bottom (0 = newest visible)
    bool m_autoScroll = true;
    uint16_t m_filterMask = 0x07FF;  // Bitmask of enabled Harness::ITrace::Boundary values
    uint8_t m_legendCursor = 0;   // Selected row in legend mode
    size_t m_matchCount = 0;      // Cached count of filtered entries
    uint8_t m_zoomLevel = 0;      // Graph zoom: 0=auto, 1-7 = fixed block widths
    uint8_t m_frameSkip = 0;      // live timeline throttle (~5 Hz at the 20 Hz display tick)

    static constexpr uint8_t VISIBLE_LINES = 20;
    static constexpr uint16_t LINE_HEIGHT = 11;
    static constexpr uint8_t LEGEND_ITEMS = 12;  // Boundary filters + the Svc info row

    // Timeline constants
    static constexpr uint8_t GRAPH_LANES = 6;
    static constexpr uint8_t ZOOM_LEVELS = 7;
    static constexpr uint8_t MAX_SERVICE_LANES = 8;
    static constexpr uint8_t MAX_SLOTS = 70;     // timeline width / narrowest block

    // Diff caches: what is on the panel now
    uint32_t m_headerDrawn = 0;
    uint32_t m_hintDrawn = 0;
    uint32_t m_axisDrawn = 0;
    uint32_t m_lineDrawn[VISIBLE_LINES] = {};
    uint32_t m_barDrawn = 0;                     // scrollbar thumb (top, length, colour)
    uint8_t  m_rowDrawn[LEGEND_ITEMS] = {};      // legend row state (0xFF = not drawn)
    uint16_t m_slotDrawn[MAX_SLOTS] = {};        // timeline block key per slot
    uint16_t m_drawnBlockW = 0;                  // block width the lanes were laid out for
};

} // namespace UI

#endif // TRACE_SCREEN_HPP
