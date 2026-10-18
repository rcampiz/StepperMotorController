/**
 * @file task_monitor_screen.hpp
 * @brief Task monitor screen — live view of scheduler task statistics
 *
 * Three view modes accessible via RIGHT button:
 *
 * STATS mode — table of task name, state, priority, stack HWM, CPU%
 *   LEFT: Exit screen
 *   RIGHT: Switch to Timeline mode
 *
 * TIMELINE mode — trace-based task activity graph with service coloring
 *   Shows activity per task from the trace ring buffer (not sampled).
 *   Blocks colored by serviceId show what each task was doing.
 *   Live: UP/DOWN = zoom, CENTER = pause, LEFT = back to Stats, RIGHT = Legend
 *   Paused: UP/DOWN = cursor, CENTER = resume, LEFT/RIGHT = pan
 *
 * LEGEND mode — color key for services, tasks, and tick markers
 *   LEFT: Back to Timeline
 *
 * Reads task data through ITaskStats platform interface (RTOS-agnostic).
 *
 * Drawn under the shared chrome (title tab, status line, footer) between Theme::CONTENT_Y and
 * Theme::CONTENT_BOTTOM. Repaints are diffs: the table repaints the cells whose value changed,
 * the timeline repaints per column only the lanes whose blocks changed (graph-screen style).
 */

#ifndef TASK_MONITOR_SCREEN_HPP
#define TASK_MONITOR_SCREEN_HPP

#include "ui/screen.hpp"
#include <stdint.h>
#include <stddef.h>

namespace Platform { class ITaskStats; }

namespace UI {

class TaskMonitorScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::STATUS; }
    void render(Services::ICanvas& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;

    bool needsFullRedraw() const override { return m_needsRedraw; }
    void clearRedrawFlag() override { m_needsRedraw = false; }
    bool usesChrome() const override { return true; }
    const char* title() const override;
    const char* centerHint() const override;

    // Timeline plot geometry (public for the host snapshot test)
    static constexpr uint16_t PLOT_X = 46;
    static constexpr uint16_t PLOT_Y = 66;
    static constexpr uint16_t PLOT_W = 188;
    static constexpr uint16_t PLOT_H = 191;

private:
    enum class Mode : uint8_t { STATS, TIMELINE, LEGEND };

    void renderStats(Services::ICanvas& lcd);
    void renderTimeline(Services::ICanvas& lcd);
    void renderLegend(Services::ICanvas& lcd);
    InputResult handleStatsInput(JoyDirection dir);
    InputResult handleTimelineInput(JoyDirection dir);
    InputResult handleLegendInput(JoyDirection dir);
    void switchMode(Mode m);

    // Timeline helpers
    struct Band {                 ///< one horizontal lane of the plot
        uint8_t y, h;             ///< rows, relative to PLOT_Y (separator row, fill, gap row)
        uint8_t task;             ///< Trace::TaskId the band belongs to
        uint8_t svc;              ///< 0xFF: task lane; else the service of an expanded sub-lane
        uint8_t shift;            ///< bit offset of the band's field in a column code
        uint8_t lane;             ///< index into m_activeTasks
    };
    static constexpr uint8_t MAX_BANDS = 12;   ///< 4 compressed lanes + 8 service sub-lanes
    struct Layout {
        Band bands[MAX_BANDS];
        uint8_t count;
        uint8_t bottom;           ///< row of the closing separator
    };
    void buildLayout(Layout& out, bool isExpanded, uint8_t expandedLaneIdx,
                     const uint8_t* subSvcIds, uint8_t subCount) const;
    void drawLabels(Services::ICanvas& lcd, const Layout& lay);
    void paintColumn(Services::ICanvas& lcd, uint16_t x, uint32_t code, uint16_t from, uint16_t to);

    bool m_needsRedraw = true;    ///< the manager clears and frames the page (activation, view switch)
    bool m_full = true;           ///< this view's static parts are to be drawn
    bool m_dirty = true;          ///< timeline: zoom/cursor/pan/pause changed, recompute now
    Mode m_mode = Mode::STATS;

    // Timeline state (trace-based)
    bool m_timelinePaused = false;
    uint8_t m_zoomLevel = 0;        // 0=auto-fit entries, 1-8=time window (1,2,4..128ms)
    uint8_t m_expandedTask = 0xFF;  // Trace::TaskId of expanded task, 0xFF=none
    uint8_t m_taskCursor = 0;       // cursor in active lane list (paused mode)
    size_t m_lastTotal = 0;         // for new-data detection
    size_t m_scrollOffset = 0;      // pan offset from newest (0=newest visible)
    uint8_t m_frameSkip = 0;        // live redraws throttled to ~5 Hz

    // Cached active lane mapping (updated each render)
    uint8_t m_activeTasks[5] = {};  // Trace::TaskId for each active lane
    uint8_t m_activeLaneCount = 0;

    // Timeline: what is on the panel
    Layout m_layout = {};           // the band layout the plot and labels were drawn with
    bool m_labelsValid = false;
    uint8_t m_labelCursor = 0xFF;   // cursor lane the labels show (0xFF = none)
    uint32_t m_colDrawn[PLOT_W] = {};   // column codes on the panel (bit 31 = unknown)
    uint32_t m_colNew[PLOT_W] = {};
    uint8_t m_colBuf[PLOT_H * 2] = {};
    char m_headText[40] = {};
    char m_axisText[2][40] = {};
    int8_t m_hintShown = -1;        // 0 live hint, 1 paused hint, -1 none yet

    // Stats: what is on the panel
    struct RowCache {
        char name[12];
        uint8_t state, priority, cpu;
        uint16_t stack;
        bool valid;
    };
    RowCache m_rows[8] = {};
    char m_heapText[16] = {};
    char m_upText[16] = {};

    // Cached task count for stats mode
    uint8_t m_lastTaskCount = 0;
};

} // namespace UI

#endif // TASK_MONITOR_SCREEN_HPP
