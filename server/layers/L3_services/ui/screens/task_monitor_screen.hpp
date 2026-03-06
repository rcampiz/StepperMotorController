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
 */

#ifndef TASK_MONITOR_SCREEN_HPP
#define TASK_MONITOR_SCREEN_HPP

#include "ui/screen.hpp"
#include <stdint.h>
#include <stddef.h>

namespace Harness { class ITaskStats; }

namespace UI {

class TaskMonitorScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::STATUS; }
    void render(Harness::ICanvas& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;

    bool needsFullRedraw() const override { return false; }
    void clearRedrawFlag() override { }

private:
    enum class Mode : uint8_t { STATS, TIMELINE, LEGEND };

    void renderStats(Harness::ICanvas& lcd);
    void renderTimeline(Harness::ICanvas& lcd);
    void renderLegend(Harness::ICanvas& lcd);
    InputResult handleStatsInput(JoyDirection dir);
    InputResult handleTimelineInput(JoyDirection dir);
    InputResult handleLegendInput(JoyDirection dir);

    bool m_needsRedraw = true;
    bool m_titleDrawn = false;
    Mode m_mode = Mode::STATS;

    // Timeline state (trace-based)
    bool m_timelinePaused = false;
    uint8_t m_zoomLevel = 0;        // 0=auto-fit entries, 1-8=time window (1,2,4..128ms)
    uint8_t m_expandedTask = 0xFF;  // Trace::TaskId of expanded task, 0xFF=none
    uint8_t m_taskCursor = 0;       // cursor in active lane list (paused mode)
    size_t m_lastTotal = 0;         // for new-data detection
    size_t m_scrollOffset = 0;      // pan offset from newest (0=newest visible)

    // Cached active lane mapping (updated each render)
    uint8_t m_activeTasks[5] = {};  // Trace::TaskId for each active lane
    uint8_t m_activeLaneCount = 0;

    // Cached task count for stats mode
    uint8_t m_lastTaskCount = 0;
};

} // namespace UI

#endif // TASK_MONITOR_SCREEN_HPP
