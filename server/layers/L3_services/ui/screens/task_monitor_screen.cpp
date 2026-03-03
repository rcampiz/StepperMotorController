/**
 * @file task_monitor_screen.cpp
 * @brief Task monitor screen implementation
 *
 * Two view modes:
 *
 * STATS — table showing each task's name, state, priority, stack, CPU%
 *   Data comes from ITaskStats platform interface (no FreeRTOS dependency)
 *
 * TIMELINE — trace-based task activity graph with service coloring
 *   Reads trace ring buffer entries, groups by taskId, colors by serviceId.
 *   Normal: one lane per active task, blocks colored by service
 *   Expanded: selected task splits into service sub-lanes
 */

#include "ui/screens/task_monitor_screen.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include "F_platform/hal/itask_stats.hpp"
#include "F_platform/rtos/platform_init.hpp"
#include "F_platform/types/telemetry.hpp"
#include "L3_services/infra/trace.hpp"
#include <stdio.h>
#include <string.h>

namespace UI {

static constexpr uint16_t MARGIN = 4;
static constexpr uint16_t TITLE_H = 16;
static constexpr uint16_t LINE_H = 15;
static constexpr uint16_t LABEL_COLOR = 0x07FF;  // Cyan
static constexpr uint16_t WHITE = 0xFFFF;
static constexpr uint16_t BG_COLOR = 0x0000;
static constexpr uint16_t PAUSE_COLOR = 0xFFE0;  // Yellow
static constexpr uint16_t DIM_GRAY = 0x4208;
static constexpr uint16_t LINE_W = LCD::WIDTH - (2 * MARGIN);

// State character: R=Running, r=Ready, B=Blocked, S=Suspended, D=Deleted
static char stateChar(uint8_t state) {
    switch (state) {
        case 0:  return 'R';
        case 1:  return 'r';
        case 2:  return 'B';
        case 3:  return 'S';
        case 4:  return 'D';
        default: return '?';
    }
}

// Per-task colors (index = Trace::TaskId)
static constexpr uint16_t TASK_COLORS[] = {
    0x8410,  // 0: unknown — gray
    0xF800,  // 1: motor   — red
    0x07FF,  // 2: encoder — cyan
    0x07E0,  // 3: comms   — green
    0xFFE0,  // 4: display — yellow
    0xF81F,  // 5: timer   — magenta
};

// Dim a color to ~25% brightness for lane backgrounds
static uint16_t dimColor(uint16_t c) {
    uint16_t r = (c >> 11) & 0x1F;
    uint16_t g = (c >> 5)  & 0x3F;
    uint16_t b =  c        & 0x1F;
    return ((r / 4) << 11) | ((g / 4) << 5) | (b / 4);
}

// Time-based zoom: window durations in ms (index = zoom level)
// 0=auto-fit (entry mode), 1-8 = time-based windows
static constexpr uint16_t TIME_WINDOWS[] = {0, 1, 2, 4, 8, 16, 32, 64, 128};
static constexpr uint8_t ZOOM_LEVELS = 8;

// Task lane info for trace-based timeline
struct TaskLaneInfo {
    uint8_t taskId;
    const char* label;
    uint16_t color;
};

static constexpr TaskLaneInfo TASK_LANES[] = {
    { 1, "Motor", 0xF800 },  // TASK_MOTOR   — red
    { 2, "Encdr", 0x07FF },  // TASK_ENCODER — cyan
    { 3, "Comms", 0x07E0 },  // TASK_COMMS   — green
    { 4, "Displ", 0xFFE0 },  // TASK_DISPLAY — yellow
    { 5, "Timer", 0xF81F },  // TASK_TIMER   — magenta
};

static constexpr uint8_t TASK_LANE_COUNT =
    sizeof(TASK_LANES) / sizeof(TASK_LANES[0]);

// Service color for blocks within task lanes
static uint16_t serviceColor(uint8_t svcId) {
    static constexpr uint16_t COLORS[] = {
        0x8410,  // 0: none     — mid gray
        0xF800,  // 1: motion   — red
        0x5DDF,  // 2: config   — light blue
        0xFFE0,  // 3: safety   — yellow
        0x07FF,  // 4: encoder  — cyan
        0xF81F,  // 5: sysid    — magenta
        0x07E0,  // 6: comms    — green
        0xC618,  // 7: UI       — light gray
        0xFD20,  // 8: dispatch — orange
    };
    if (svcId > 8) { return COLORS[0]; }
    return COLORS[svcId];
}

// Service label for expanded sub-lanes
static const char* serviceLabel(uint8_t svcId) {
    switch (svcId) {
        case 1: return "Mot";
        case 2: return "Cfg";
        case 3: return "Saf";
        case 4: return "Enc";
        case 5: return "SId";
        case 6: return "Com";
        case 7: return "UI";
        case 8: return "Dsp";
        default: return "?";
    }
}

// Find TaskLaneInfo by taskId
static const TaskLaneInfo* findTaskLane(uint8_t taskId) {
    for (uint8_t t = 0; t < TASK_LANE_COUNT; t++) {
        if (TASK_LANES[t].taskId == taskId) { return &TASK_LANES[t]; }
    }
    return nullptr;
}

void TaskMonitorScreen::onActivate()
{
    m_needsRedraw = true;
    m_titleDrawn = false;
    m_mode = Mode::STATS;
    m_timelinePaused = false;
    m_zoomLevel = 0;
    m_expandedTask = 0xFF;
    m_taskCursor = 0;
    m_lastTotal = 0;
    m_scrollOffset = 0;
    m_activeLaneCount = 0;

    ITaskStats* stats = Platform::resources().taskStats;
    if (stats != nullptr) {
        TaskStatsSnapshot snap;
        stats->getSnapshot(snap);
        m_lastTaskCount = snap.count;
    }
}

void TaskMonitorScreen::render(LCD& lcd)
{
    if (!m_titleDrawn) {
        lcd.fillScreen(BG_COLOR);
    }

    switch (m_mode) {
        case Mode::TIMELINE: renderTimeline(lcd); break;
        case Mode::LEGEND:   renderLegend(lcd);   break;
        default:             renderStats(lcd);    break;
    }
    m_needsRedraw = false;
}

// =============================================================================
// STATS mode — task table
// =============================================================================

void TaskMonitorScreen::renderStats(LCD& lcd)
{
    if (!m_titleDrawn) {
        m_titleDrawn = true;
    }

    ITaskStats* stats = Platform::resources().taskStats;
    if (stats == nullptr) {
        lcd.blitTextLine(MARGIN, MARGIN, LINE_W, 8,
                         "No task stats", WHITE, BG_COLOR);
        return;
    }

    TaskStatsSnapshot snap;
    stats->getSnapshot(snap);
    m_lastTaskCount = snap.count;

    uint16_t y = MARGIN;
    char buf[40];

    // Title
    lcd.blitTextLine(MARGIN, y, LINE_W, 8,
                     "Task Monitor", LABEL_COLOR, BG_COLOR);
    y += TITLE_H;
    lcd.drawHLine(MARGIN, y, LINE_W, LABEL_COLOR);
    y += 4;

    // Header row
    lcd.blitTextLine(MARGIN, y, LINE_W, LINE_H,
                     "Name     St Pr Stk  CPU", LABEL_COLOR, BG_COLOR);
    y += LINE_H;

    // Task rows
    for (uint8_t i = 0; i < snap.count && i < 8; i++) {
        const TaskStat& t = snap.tasks[i];

        char name[9];
        strncpy(name, t.name, 8);
        name[8] = '\0';
        size_t len = strlen(name);
        while (len < 8) { name[len++] = ' '; }
        name[8] = '\0';

        snprintf(buf, sizeof(buf), "%s %c  %u  %3u  %2u%%",
                 name,
                 stateChar(t.state),
                 (unsigned)t.priority,
                 (unsigned)t.stackHWM,
                 (unsigned)t.cpuPercent);

        uint16_t color = (i < 6) ? TASK_COLORS[i] : WHITE;
        lcd.blitTextLine(MARGIN, y, LINE_W, LINE_H,
                         buf, color, BG_COLOR);
        y += LINE_H;
    }

    // Blank remaining rows
    while (y + LINE_H <= 260) {
        lcd.blitTextLine(MARGIN, y, LINE_W, LINE_H, "", WHITE, BG_COLOR);
        y += LINE_H;
    }

    // Separator
    y += 2;
    lcd.drawHLine(MARGIN, y, LINE_W, DIM_GRAY);
    y += 4;

    // System info
    Comms::TelemetrySnapshot telem = Comms::g_telemetry.getSnapshot();
    uint32_t secs = telem.system.uptimeTicks / 1000;
    uint32_t mins = secs / 60;
    secs %= 60;
    uint32_t hrs = mins / 60;
    mins %= 60;

    snprintf(buf, sizeof(buf), "Heap:%lu B  Up:%luh%lum%lus",
             static_cast<unsigned long>(telem.system.freeHeap),
             static_cast<unsigned long>(hrs),
             static_cast<unsigned long>(mins),
             static_cast<unsigned long>(secs));
    lcd.blitTextLine(MARGIN, y, LINE_W, LINE_H, buf, WHITE, BG_COLOR);
    y += LINE_H;

    // Controls
    lcd.blitTextLine(MARGIN, y, LINE_W, LINE_H,
                     "L:back  R:timeline", LABEL_COLOR, BG_COLOR);
}

// =============================================================================
// TIMELINE mode — trace-based task activity graph
// =============================================================================

void TaskMonitorScreen::renderTimeline(LCD& lcd)
{
    if (!m_titleDrawn) {
        m_titleDrawn = true;
    }

    size_t total = Trace::getTotal();
    size_t count = Trace::getCount();
    bool newData = (total != m_lastTotal);
    if (newData) {
        m_lastTotal = total;
    }

    bool redrawTitle = false;
    bool redrawGraph = false;
    if (m_needsRedraw) { redrawTitle = true; redrawGraph = true; }
    if (newData) {
        redrawTitle = true;
        if (!m_timelinePaused && !redrawGraph) {
            // Throttle graph redraws to ~5Hz to reduce flicker
            static uint8_t frameSkip = 0;
            if (++frameSkip >= 4) {
                redrawGraph = true;
                frameSkip = 0;
            }
        }
    }
    if (!redrawTitle && !redrawGraph) { return; }

    // Layout constants
    static constexpr uint16_t LANES_Y = MARGIN + TITLE_H + 4;
    static constexpr uint16_t FOOTER_SPACE = 22;
    uint16_t availH = LCD::HEIGHT - LANES_Y - FOOTER_SPACE;
    uint16_t timelineX = 38;
    uint16_t timelineRight = LCD::WIDTH - MARGIN;
    uint16_t timelineW = timelineRight - timelineX;

    // --- Always show all task lanes (even without trace activity) ---
    Trace::Entry entry;
    m_activeLaneCount = TASK_LANE_COUNT;
    for (uint8_t t = 0; t < TASK_LANE_COUNT; t++) {
        m_activeTasks[t] = TASK_LANES[t].taskId;
    }

    // Clamp cursor
    if (m_activeLaneCount > 0 && m_taskCursor >= m_activeLaneCount) {
        m_taskCursor = m_activeLaneCount - 1;
    }

    // --- Title ---
    if (redrawTitle) {
        char title[40];
        if (m_timelinePaused) {
            if (m_zoomLevel > 0) {
                snprintf(title, sizeof(title), "PAUSED  %ums window",
                         (unsigned)TIME_WINDOWS[m_zoomLevel]);
            } else {
                snprintf(title, sizeof(title), "PAUSED  %u entries", (unsigned)count);
            }
            lcd.blitTextLine(MARGIN, MARGIN, LINE_W, 8, title, PAUSE_COLOR, BG_COLOR);
        } else {
            if (m_zoomLevel > 0) {
                snprintf(title, sizeof(title), "Task Activity  %ums window",
                         (unsigned)TIME_WINDOWS[m_zoomLevel]);
            } else {
                snprintf(title, sizeof(title), "Task Activity  %u entries", (unsigned)count);
            }
            lcd.blitTextLine(MARGIN, MARGIN, LINE_W, 8, title, LABEL_COLOR, BG_COLOR);
        }
        lcd.drawHLine(MARGIN, MARGIN + TITLE_H, LINE_W, LABEL_COLOR);
    }

    if (!redrawGraph) { return; }

    if (m_activeLaneCount == 0) {
        lcd.blitTextLine(MARGIN, LANES_Y + 8, LINE_W, LINE_H,
                         " No task activity", DIM_GRAY, BG_COLOR);
        return;
    }

    // --- Check if expanded mode is valid ---
    bool isExpanded = false;
    uint8_t expandedLaneIdx = 0xFF;
    if (m_expandedTask != 0xFF) {
        for (uint8_t i = 0; i < m_activeLaneCount; i++) {
            if (m_activeTasks[i] == m_expandedTask) {
                expandedLaneIdx = i;
                isExpanded = true;
                break;
            }
        }
        if (!isExpanded) {
            m_expandedTask = 0xFF;
        }
    }

    // Expanded: find service sub-lanes within the expanded task
    uint8_t subSvcIds[8] = {};
    uint8_t subCount = 0;
    if (isExpanded) {
        uint16_t subSvcMask = 0;
        for (size_t i = 0; i < count; i++) {
            if (Trace::getEntry(i, entry) && entry.taskId == m_expandedTask
                && entry.serviceId > 0 && entry.serviceId <= 8) {
                subSvcMask |= (1 << entry.serviceId);
            }
        }
        for (uint8_t s = 1; s <= 8; s++) {
            if (subSvcMask & (1 << s)) {
                subSvcIds[subCount++] = s;
            }
        }
        if (subCount == 0) {
            isExpanded = false;
            m_expandedTask = 0xFF;
        }
    }

    // --- Compute lane layout & draw lanes ---
    // Clear label column (prevents stale text when active tasks change)
    lcd.fillRect(MARGIN, LANES_Y, timelineX - MARGIN, LCD::HEIGHT - LANES_Y, BG_COLOR);

    // Mapping arrays for plotting phase
    int8_t taskToLane[6];
    memset(taskToLane, -1, sizeof(taskToLane));
    int8_t svcToSubLane[9];
    memset(svcToSubLane, -1, sizeof(svcToSubLane));
    uint16_t laneYs[5] = {};     // y-position per active lane
    uint16_t laneHs[5] = {};     // height per active lane
    uint16_t expandedY = 0;      // y-start of expanded sub-lanes
    uint16_t subLaneH = 0;       // height of each sub-lane
    uint16_t lanesBottom = 0;

    if (!isExpanded) {
        // --- NORMAL: equal-height task lanes ---
        uint16_t laneH = availH / m_activeLaneCount;
        if (laneH > 52) { laneH = 52; }
        if (laneH < 16) { laneH = 16; }

        for (uint8_t i = 0; i < m_activeLaneCount; i++) {
            uint8_t taskId = m_activeTasks[i];
            const TaskLaneInfo* tl = findTaskLane(taskId);
            uint16_t color = tl ? tl->color : WHITE;
            const char* name = tl ? tl->label : "?";

            uint16_t ly = LANES_Y + i * laneH;
            laneYs[i] = ly;
            laneHs[i] = laneH;
            taskToLane[taskId] = static_cast<int8_t>(i);

            lcd.fillRect(timelineX, ly + 1, timelineW, laneH - 2, dimColor(color));

            char labelBuf[7];
            if (m_timelinePaused && i == m_taskCursor) {
                snprintf(labelBuf, sizeof(labelBuf), ">%s", name);
            } else {
                snprintf(labelBuf, sizeof(labelBuf), " %s", name);
            }
            lcd.blitTextLine(MARGIN, ly, timelineX - MARGIN - 2, laneH,
                             labelBuf, color, BG_COLOR);
            lcd.drawHLine(timelineX, ly, timelineW, DIM_GRAY);
        }
        lanesBottom = LANES_Y + m_activeLaneCount * laneH;
        lcd.drawHLine(timelineX, lanesBottom, timelineW, DIM_GRAY);
    } else {
        // --- EXPANDED: compressed lanes + service sub-lanes ---
        static constexpr uint16_t COMPRESSED_H = 10;
        uint16_t compressedTotal = COMPRESSED_H * (m_activeLaneCount - 1);
        uint16_t expandedTotal = availH - compressedTotal;
        subLaneH = expandedTotal / subCount;
        if (subLaneH > 30) { subLaneH = 30; }
        if (subLaneH < 12) { subLaneH = 12; }

        // Build sub-lane map
        for (uint8_t s = 0; s < subCount; s++) {
            svcToSubLane[subSvcIds[s]] = static_cast<int8_t>(s);
        }

        uint16_t y = LANES_Y;
        for (uint8_t i = 0; i < m_activeLaneCount; i++) {
            uint8_t taskId = m_activeTasks[i];
            const TaskLaneInfo* tl = findTaskLane(taskId);
            uint16_t color = tl ? tl->color : WHITE;
            const char* name = tl ? tl->label : "?";

            if (i == expandedLaneIdx) {
                // Expanded task: draw service sub-lanes
                expandedY = y;
                laneYs[i] = y;
                laneHs[i] = subCount * subLaneH;

                // Task label on first sub-lane
                char labelBuf[7];
                snprintf(labelBuf, sizeof(labelBuf), "v%s", name);
                lcd.blitTextLine(MARGIN, y, timelineX - MARGIN - 2, subLaneH,
                                 labelBuf, color, BG_COLOR);

                for (uint8_t s = 0; s < subCount; s++) {
                    uint16_t sly = y + s * subLaneH;
                    uint16_t svcColor = serviceColor(subSvcIds[s]);
                    lcd.fillRect(timelineX, sly + 1, timelineW, subLaneH - 2,
                                 dimColor(svcColor));
                    lcd.drawHLine(timelineX, sly, timelineW, DIM_GRAY);

                    // Service label (indented) for rows after the first
                    if (s > 0) {
                        lcd.blitTextLine(MARGIN + 8, sly,
                                         timelineX - MARGIN - 10, subLaneH,
                                         serviceLabel(subSvcIds[s]),
                                         svcColor, BG_COLOR);
                    }
                }
                y += subCount * subLaneH;
            } else {
                // Compressed lane
                laneYs[i] = y;
                laneHs[i] = COMPRESSED_H;
                taskToLane[taskId] = static_cast<int8_t>(i);

                lcd.fillRect(timelineX, y + 1, timelineW, COMPRESSED_H - 2,
                             dimColor(color));

                char labelBuf[7];
                if (m_timelinePaused && i == m_taskCursor) {
                    snprintf(labelBuf, sizeof(labelBuf), ">%s", name);
                } else {
                    snprintf(labelBuf, sizeof(labelBuf), " %s", name);
                }
                lcd.blitTextLine(MARGIN, y, timelineX - MARGIN - 2, COMPRESSED_H,
                                 labelBuf, color, BG_COLOR);
                lcd.drawHLine(timelineX, y, timelineW, DIM_GRAY);
                y += COMPRESSED_H;
            }
        }
        lanesBottom = y;
        lcd.drawHLine(timelineX, lanesBottom, timelineW, DIM_GRAY);
    }

    // Clear timeline area below lanes to screen bottom
    if (lanesBottom < LCD::HEIGHT) {
        lcd.fillRect(timelineX, lanesBottom, timelineW, LCD::HEIGHT - lanesBottom, BG_COLOR);
    }

    // =================================================================
    // Plot entries + tick markers (two rendering paths)
    // =================================================================

    // Tick marker colors (shared)
    static constexpr uint16_t TICK_GREEN  = 0x0320;  // Dark green
    static constexpr uint16_t TICK_YELLOW = 0x8400;  // Dark yellow
    static constexpr uint16_t TICK_RED    = 0x8000;  // Dark red
    uint16_t lanesTop = laneYs[0];
    uint16_t totalLaneH = lanesBottom - lanesTop;

    if (m_zoomLevel == 0) {
        // --- ENTRY MODE: auto-fit, one column per entry ---
        uint16_t blockW = 3;
        if (count > 0) {
            blockW = timelineW / count;
            if (blockW < 2) blockW = 2;
            if (blockW > 6) blockW = 6;
        }
        uint16_t blockDraw = (blockW > 2) ? (blockW - 1) : blockW;
        uint16_t visibleEntries = timelineW / blockW;

        // Visible range (newest at right, pan offset)
        size_t startIdx = 0;
        if (count > visibleEntries) {
            if (count > m_scrollOffset + visibleEntries) {
                startIdx = count - visibleEntries - m_scrollOffset;
            }
        }

        // Plot entry blocks
        for (size_t i = startIdx; i < count; i++) {
            if (!Trace::getEntry(i, entry)) continue;
            if (entry.taskId == 0 || entry.taskId >= 6) continue;

            uint16_t x = timelineX + static_cast<uint16_t>(i - startIdx) * blockW;
            if (x + blockDraw > timelineRight) break;

            if (isExpanded && entry.taskId == m_expandedTask) {
                if (entry.serviceId > 0 && entry.serviceId <= 8) {
                    int8_t sub = svcToSubLane[entry.serviceId];
                    if (sub >= 0) {
                        uint16_t sly = expandedY + static_cast<uint16_t>(sub) * subLaneH + 1;
                        lcd.fillRect(x, sly, blockDraw, subLaneH - 2,
                                     serviceColor(entry.serviceId));
                    }
                }
            } else {
                int8_t lane = taskToLane[entry.taskId];
                if (lane >= 0) {
                    uint16_t ly = laneYs[lane] + 1;
                    uint16_t lh = laneHs[lane] - 2;
                    if (lh > 0) {
                        lcd.fillRect(x, ly, blockDraw, lh,
                                     serviceColor(entry.serviceId));
                    }
                }
            }
        }

        // Tick markers (at ms boundaries between consecutive entries)
        {
            uint32_t prevTick = 0;
            bool havePrev = false;
            for (size_t i = startIdx; i < count; i++) {
                Trace::Entry te;
                if (!Trace::getEntry(i, te)) continue;

                uint16_t x = timelineX + static_cast<uint16_t>(i - startIdx) * blockW;
                if (x >= timelineRight) break;

                if (havePrev) {
                    uint32_t prevMs = prevTick / 1000;
                    uint32_t curMs  = te.tick / 1000;
                    if (curMs != prevMs && curMs > prevMs) {
                        uint32_t gapMs = curMs - prevMs;
                        uint16_t tickColor;
                        if (gapMs <= 2)       tickColor = TICK_GREEN;
                        else if (gapMs <= 10) tickColor = TICK_YELLOW;
                        else                  tickColor = TICK_RED;
                        lcd.fillRect(x, lanesTop, 1, totalLaneH, tickColor);
                    }
                }
                prevTick = te.tick;
                havePrev = true;
            }
        }

        // Footer
        {
            uint16_t footerY = LCD::HEIGHT - FOOTER_SPACE;
            char axisBuf[40];
            size_t endIdx = startIdx + visibleEntries;
            if (endIdx > count) endIdx = count;
            snprintf(axisBuf, sizeof(axisBuf), " %u..%u/%u  zoom:auto",
                     (unsigned)startIdx, (unsigned)endIdx, (unsigned)count);
            lcd.blitTextLine(MARGIN, footerY, LINE_W, 8, axisBuf, LABEL_COLOR, BG_COLOR);

            char ctrlBuf[40];
            if (m_timelinePaused) {
                snprintf(ctrlBuf, sizeof(ctrlBuf), " CTR:resume LR:pan UD:cursor");
            } else {
                snprintf(ctrlBuf, sizeof(ctrlBuf), " CTR:pause UD:zoom L:back R:key");
            }
            lcd.blitTextLine(MARGIN, footerY + 10, LINE_W, 8, ctrlBuf,
                             m_timelinePaused ? PAUSE_COLOR : LABEL_COLOR, BG_COLOR);
        }

    } else {
        // --- TIME MODE: fixed time window, entries plotted by timestamp ---
        uint16_t windowMs = TIME_WINDOWS[m_zoomLevel];
        auto windowUs = static_cast<uint32_t>(windowMs) * 1000;

        // Find newest entry tick
        uint32_t newestTick = 0;
        if (count > 0) {
            Trace::Entry ne;
            if (Trace::getEntry(count - 1, ne)) {
                newestTick = ne.tick;
            }
        }

        // Apply pan offset (m_scrollOffset = ms from newest)
        uint32_t windowEnd = newestTick;
        if (m_scrollOffset > 0) {
            auto offsetUs = static_cast<uint32_t>(m_scrollOffset) * 1000;
            if (windowEnd > offsetUs) {
                windowEnd -= offsetUs;
            } else {
                windowEnd = 0;
            }
        }
        uint32_t windowStart = (windowEnd > windowUs) ? (windowEnd - windowUs) : 0;

        // Plot entry blocks proportional to timestamp
        static constexpr uint16_t TIME_BLOCK_W = 2;
        for (size_t i = 0; i < count; i++) {
            if (!Trace::getEntry(i, entry)) continue;
            if (entry.taskId == 0 || entry.taskId >= 6) continue;
            if (entry.tick < windowStart || entry.tick > windowEnd) continue;

            uint32_t relTick = entry.tick - windowStart;
            uint16_t x = timelineX + static_cast<uint16_t>(
                (relTick * static_cast<uint32_t>(timelineW)) / windowUs);
            if (x + TIME_BLOCK_W > timelineRight) continue;

            if (isExpanded && entry.taskId == m_expandedTask) {
                if (entry.serviceId > 0 && entry.serviceId <= 8) {
                    int8_t sub = svcToSubLane[entry.serviceId];
                    if (sub >= 0) {
                        uint16_t sly = expandedY + static_cast<uint16_t>(sub) * subLaneH + 1;
                        lcd.fillRect(x, sly, TIME_BLOCK_W, subLaneH - 2,
                                     serviceColor(entry.serviceId));
                    }
                }
            } else {
                int8_t lane = taskToLane[entry.taskId];
                if (lane >= 0) {
                    uint16_t ly = laneYs[lane] + 1;
                    uint16_t lh = laneHs[lane] - 2;
                    if (lh > 0) {
                        lcd.fillRect(x, ly, TIME_BLOCK_W, lh,
                                     serviceColor(entry.serviceId));
                    }
                }
            }
        }

        // Tick markers: evenly spaced at 1ms intervals (adaptive step)
        {
            uint16_t tickStepMs = 1;
            while (tickStepMs < windowMs &&
                   (static_cast<uint32_t>(tickStepMs) * timelineW / windowMs) < 3) {
                tickStepMs *= 2;
            }
            for (uint16_t ms = 0; ms <= windowMs; ms += tickStepMs) {
                uint16_t x = timelineX + static_cast<uint16_t>(
                    (static_cast<uint32_t>(ms) * timelineW) / windowMs);
                if (x >= timelineRight) break;
                lcd.fillRect(x, lanesTop, 1, totalLaneH, TICK_GREEN);
            }
        }

        // Footer
        {
            uint16_t footerY = LCD::HEIGHT - FOOTER_SPACE;
            char axisBuf[40];
            uint32_t startMs = windowStart / 1000;
            uint32_t endMs = windowEnd / 1000;
            snprintf(axisBuf, sizeof(axisBuf), " %ums window  (%lu..%lums)",
                     (unsigned)windowMs,
                     static_cast<unsigned long>(startMs),
                     static_cast<unsigned long>(endMs));
            lcd.blitTextLine(MARGIN, footerY, LINE_W, 8, axisBuf, LABEL_COLOR, BG_COLOR);

            char ctrlBuf[40];
            if (m_timelinePaused) {
                snprintf(ctrlBuf, sizeof(ctrlBuf), " CTR:resume LR:pan UD:cursor");
            } else {
                snprintf(ctrlBuf, sizeof(ctrlBuf), " CTR:pause UD:zoom L:back R:key");
            }
            lcd.blitTextLine(MARGIN, footerY + 10, LINE_W, 8, ctrlBuf,
                             m_timelinePaused ? PAUSE_COLOR : LABEL_COLOR, BG_COLOR);
        }
    }
}

// =============================================================================
// Input handling
// =============================================================================

InputResult TaskMonitorScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (!pressed) { return InputResult::HANDLED; }

    switch (m_mode) {
        case Mode::TIMELINE: return handleTimelineInput(dir);
        case Mode::LEGEND:   return handleLegendInput(dir);
        default:             return handleStatsInput(dir);
    }
}

InputResult TaskMonitorScreen::handleStatsInput(JoyDirection dir)
{
    switch (dir) {
        case JoyDirection::LEFT:
            return InputResult::EXIT_SCREEN;

        case JoyDirection::RIGHT:
            m_mode = Mode::TIMELINE;
            m_titleDrawn = false;
            m_needsRedraw = true;
            return InputResult::HANDLED;

        default:
            return InputResult::HANDLED;
    }
}

InputResult TaskMonitorScreen::handleTimelineInput(JoyDirection dir)
{
    switch (dir) {
        case JoyDirection::UP:
            if (m_timelinePaused) {
                // Move cursor up
                if (m_taskCursor > 0) {
                    m_taskCursor--;
                    m_needsRedraw = true;
                }
            } else {
                // Zoom in
                if (m_zoomLevel < ZOOM_LEVELS) {
                    m_zoomLevel++;
                    m_needsRedraw = true;
                }
            }
            return InputResult::HANDLED;

        case JoyDirection::DOWN:
            if (m_timelinePaused) {
                // Move cursor down
                if (m_activeLaneCount > 0 && m_taskCursor < m_activeLaneCount - 1) {
                    m_taskCursor++;
                    m_needsRedraw = true;
                }
            } else {
                // Zoom out
                if (m_zoomLevel > 0) {
                    m_zoomLevel--;
                    m_needsRedraw = true;
                }
            }
            return InputResult::HANDLED;

        case JoyDirection::CENTER:
            if (m_timelinePaused) {
                // Resume
                m_timelinePaused = false;
                m_expandedTask = 0xFF;
                m_scrollOffset = 0;
                m_needsRedraw = true;
            } else {
                // Pause
                m_timelinePaused = true;
                m_taskCursor = 0;
                m_scrollOffset = 0;
                m_needsRedraw = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::LEFT:
            if (m_timelinePaused) {
                // Pan left (older) — proportional to window in time mode
                if (m_zoomLevel > 0) {
                    uint16_t panMs = TIME_WINDOWS[m_zoomLevel] / 4;
                    if (panMs < 1) panMs = 1;
                    m_scrollOffset += panMs;
                } else {
                    m_scrollOffset += 8;
                }
                m_needsRedraw = true;
            } else {
                // Back to stats
                m_mode = Mode::STATS;
                m_titleDrawn = false;
                m_scrollOffset = 0;
                m_needsRedraw = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::RIGHT:
            if (m_timelinePaused) {
                // Pan right (newer) — proportional to window in time mode
                if (m_zoomLevel > 0) {
                    uint16_t panMs = TIME_WINDOWS[m_zoomLevel] / 4;
                    if (panMs < 1) panMs = 1;
                    if (m_scrollOffset > panMs) {
                        m_scrollOffset -= panMs;
                    } else {
                        m_scrollOffset = 0;
                    }
                } else {
                    if (m_scrollOffset > 8) {
                        m_scrollOffset -= 8;
                    } else {
                        m_scrollOffset = 0;
                    }
                }
                m_needsRedraw = true;
            } else {
                // Go to legend
                m_mode = Mode::LEGEND;
                m_titleDrawn = false;
                m_needsRedraw = true;
            }
            return InputResult::HANDLED;

        default:
            return InputResult::HANDLED;
    }
}

// =============================================================================
// LEGEND mode — color key for services, tasks, and tick markers
// =============================================================================

void TaskMonitorScreen::renderLegend(LCD& lcd)
{
    if (!m_needsRedraw) return;

    if (!m_titleDrawn) {
        m_titleDrawn = true;
    }

    uint16_t y = MARGIN;

    // Title
    lcd.blitTextLine(MARGIN, y, LINE_W, 8, "Activity Legend", LABEL_COLOR, BG_COLOR);
    y += TITLE_H;
    lcd.drawHLine(MARGIN, y, LINE_W, LABEL_COLOR);
    y += 6;

    // --- Service colors (what the blocks mean) ---
    lcd.blitTextLine(MARGIN, y, LINE_W, 8, "Service colors (blocks):", WHITE, BG_COLOR);
    y += LINE_H;

    // Two columns of service swatches
    static constexpr uint16_t SWATCH_W = 10;
    static constexpr uint16_t SWATCH_H = 8;
    static constexpr uint16_t COL_W = LINE_W / 2;

    struct SvcEntry { uint8_t id; const char* name; };
    static constexpr SvcEntry SVCS[] = {
        {1, "Motion"}, {2, "Config"}, {3, "Safety"}, {4, "Encoder"},
        {5, "SysId"},  {6, "Comms"},  {7, "UI"},     {8, "Dispatch"}
    };

    for (uint8_t i = 0; i < 8; i += 2) {
        // Left column
        uint16_t lx = MARGIN + 2;
        lcd.fillRect(lx, y + 2, SWATCH_W, SWATCH_H, serviceColor(SVCS[i].id));
        lcd.blitTextLine(lx + SWATCH_W + 4, y, COL_W - SWATCH_W - 6, LINE_H,
                         SVCS[i].name, serviceColor(SVCS[i].id), BG_COLOR);

        // Right column
        if (i + 1 < 8) {
            uint16_t rx = MARGIN + COL_W;
            lcd.fillRect(rx, y + 2, SWATCH_W, SWATCH_H, serviceColor(SVCS[i+1].id));
            lcd.blitTextLine(rx + SWATCH_W + 4, y, COL_W - SWATCH_W - 6, LINE_H,
                             SVCS[i+1].name, serviceColor(SVCS[i+1].id), BG_COLOR);
        }
        y += LINE_H;
    }

    y += 4;

    // --- Task lane colors ---
    lcd.blitTextLine(MARGIN, y, LINE_W, 8, "Task lanes:", WHITE, BG_COLOR);
    y += LINE_H;

    for (uint8_t i = 0; i < TASK_LANE_COUNT; i += 2) {
        uint16_t lx = MARGIN + 2;
        lcd.fillRect(lx, y + 2, SWATCH_W, SWATCH_H, TASK_LANES[i].color);
        lcd.blitTextLine(lx + SWATCH_W + 4, y, COL_W - SWATCH_W - 6, LINE_H,
                         TASK_LANES[i].label, TASK_LANES[i].color, BG_COLOR);

        if (i + 1 < TASK_LANE_COUNT) {
            uint16_t rx = MARGIN + COL_W;
            lcd.fillRect(rx, y + 2, SWATCH_W, SWATCH_H, TASK_LANES[i+1].color);
            lcd.blitTextLine(rx + SWATCH_W + 4, y, COL_W - SWATCH_W - 6, LINE_H,
                             TASK_LANES[i+1].label, TASK_LANES[i+1].color, BG_COLOR);
        }
        y += LINE_H;
    }

    y += 4;

    // --- Tick marker colors ---
    lcd.blitTextLine(MARGIN, y, LINE_W, 8, "Tick markers (1ms grid):", WHITE, BG_COLOR);
    y += LINE_H;

    // Green tick
    lcd.fillRect(MARGIN + 4, y + 2, 2, SWATCH_H, 0x0320);
    lcd.blitTextLine(MARGIN + 12, y, LINE_W - 14, LINE_H,
                     "Green  = 1-2ms (normal)", 0x07E0, BG_COLOR);
    y += LINE_H;

    // Yellow tick
    lcd.fillRect(MARGIN + 4, y + 2, 2, SWATCH_H, 0x8400);
    lcd.blitTextLine(MARGIN + 12, y, LINE_W - 14, LINE_H,
                     "Yellow = 3-10ms (gap)", PAUSE_COLOR, BG_COLOR);
    y += LINE_H;

    // Red tick
    lcd.fillRect(MARGIN + 4, y + 2, 2, SWATCH_H, 0x8000);
    lcd.blitTextLine(MARGIN + 12, y, LINE_W - 14, LINE_H,
                     "Red    = >10ms (idle)", 0xF800, BG_COLOR);
    y += LINE_H + 4;

    // --- Zoom info ---
    lcd.drawHLine(MARGIN, y, LINE_W, DIM_GRAY);
    y += 4;
    lcd.blitTextLine(MARGIN, y, LINE_W, 8,
                     "Zoom: auto=entries, 1-128ms=time", DIM_GRAY, BG_COLOR);

    // Footer
    lcd.blitTextLine(MARGIN, LCD::HEIGHT - 10, LINE_W, 8,
                     "L:back to timeline", LABEL_COLOR, BG_COLOR);
}

InputResult TaskMonitorScreen::handleLegendInput(JoyDirection dir)
{
    switch (dir) {
        case JoyDirection::LEFT:
            m_mode = Mode::TIMELINE;
            m_titleDrawn = false;
            m_needsRedraw = true;
            return InputResult::HANDLED;
        default:
            return InputResult::HANDLED;
    }
}

} // namespace UI
