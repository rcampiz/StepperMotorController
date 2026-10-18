/**
 * @file task_monitor_screen.cpp
 * @brief Task monitor screen implementation
 *
 * Three view modes, drawn under the chrome:
 *
 * STATS — table showing each task's name, state, priority, stack, CPU%
 *   Data comes from ITaskStats platform interface (no FreeRTOS dependency).
 *   Each cell is repainted only when its value changes.
 *
 * TIMELINE — trace-based task activity graph with service coloring
 *   Reads trace ring buffer entries, groups by taskId, colors by serviceId.
 *   Normal: one lane per active task, blocks colored by service
 *   Expanded: selected task splits into service sub-lanes
 *   Every plot column is summarised in a 32-bit code (one field per lane: which service's
 *   block is there, plus the tick marker). Only the columns whose code changed are sent, and
 *   of those only the rows of the lanes that changed: no clear-then-draw, no flicker.
 *
 * LEGEND — colour key, drawn once per visit.
 */

#include "ui/screens/task_monitor_screen.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "harness/pins/itask_stats.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include "L3_services/infra/trace/trace.hpp"
#include "ui/chrome/chrome.hpp"
#include "ui/theme.hpp"
#include <stdio.h>
#include <string.h>

namespace UI {

static constexpr uint16_t MARGIN = Theme::MARGIN;
static constexpr uint16_t LINE_W = Theme::WIDTH - (2 * MARGIN);
static constexpr uint16_t TOP = Theme::CONTENT_Y + 2;

// --- STATS layout ---------------------------------------------------------------------------
static constexpr uint16_t TABLE_Y = TOP + 12;                      // first task row
static constexpr uint8_t  MAX_ROWS = 8;
static constexpr uint16_t ROW_H = Chrome::ROW_H;                    // 16 px bar, separator, gap
static constexpr uint16_t INFO_Y = TABLE_Y + MAX_ROWS * ROW_H + 4;  // heap, uptime
static constexpr uint16_t STATS_HINT_Y = INFO_Y + 2 * ROW_H + 6;
// Table columns (x, width): colour tick, name, state, priority, stack, CPU bar, CPU %
static constexpr uint16_t COL_NAME_X = MARGIN + 3,  COL_NAME_W = 71;
static constexpr uint16_t COL_ST_X   = 80,          COL_ST_W   = 16;
static constexpr uint16_t COL_PR_X   = 96,          COL_PR_W   = 22;
static constexpr uint16_t COL_STK_X  = 118,         COL_STK_W  = 36;
static constexpr uint16_t COL_BAR_X  = 154,         COL_BAR_W  = 46;
static constexpr uint16_t BAR_X = 158, BAR_W = 36, BAR_DY = 5, BAR_H = 6;
static constexpr uint16_t COL_CPU_X  = 200,         COL_CPU_W  = Theme::WIDTH - MARGIN - 200;

// --- TIMELINE layout ------------------------------------------------------------------------
static constexpr uint16_t HEAD_Y = TOP + 1;                          // LIVE/PAUSED badge, entries
static constexpr uint16_t HEAD_H = 14;
static constexpr uint16_t BADGE_W = 60;
static constexpr uint16_t LABEL_X = MARGIN;                          // lane labels
static constexpr uint16_t LABEL_W = 36;
static constexpr uint16_t AXIS_Y = TaskMonitorScreen::PLOT_Y + TaskMonitorScreen::PLOT_H + 4;
static constexpr uint16_t TL_HINT_Y = AXIS_Y + 12;
static constexpr uint8_t  TICK_SHIFT = 26;                           // 2-bit tick field
static constexpr uint32_t COL_UNKNOWN = 0x80000000u;                 // nothing known on the panel

// --- LEGEND layout --------------------------------------------------------------------------
static constexpr uint16_t LEGEND_ROW = 15;

static constexpr const char* HINT_STATS    = "RIGHT timeline   LEFT back";
static constexpr const char* HINT_LIVE     = "UP/DN zoom  LEFT stats  RIGHT key";
static constexpr const char* HINT_PAUSED   = "UP/DN cursor   LEFT/RIGHT pan";
static constexpr const char* HINT_LEGEND   = "LEFT back to the timeline";

// Scale an RGB565 colour by num/den (dimmed lane backgrounds, dark tick markers)
static uint16_t shade(uint16_t c, uint16_t num, uint16_t den) {
    uint16_t r = static_cast<uint16_t>(((c >> 11) & 0x1F) * num / den);
    uint16_t g = static_cast<uint16_t>(((c >> 5)  & 0x3F) * num / den);
    uint16_t b = static_cast<uint16_t>(( c        & 0x1F) * num / den);
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}
static uint16_t laneBg(uint16_t c) { return shade(c, 1, 5); }

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

static uint16_t stateColor(uint8_t state) {
    switch (state) {
        case 0:  return Theme::GREEN;
        case 1:  return Theme::CYAN;
        case 2:  return Theme::VIOLET;
        case 3:  return Theme::YELLOW;
        case 4:  return Theme::RED;
        default: return Theme::DIM;
    }
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
    { 1, "Motor", Theme::RED    },  // TASK_MOTOR
    { 2, "Encdr", Theme::CYAN   },  // TASK_ENCODER
    { 3, "Comms", Theme::GREEN  },  // TASK_COMMS
    { 4, "Displ", Theme::YELLOW },  // TASK_DISPLAY
    { 5, "Timer", Theme::VIOLET },  // TASK_TIMER
};

static constexpr uint8_t TASK_LANE_COUNT =
    sizeof(TASK_LANES) / sizeof(TASK_LANES[0]);

// Service color for blocks within task lanes
static uint16_t serviceColor(uint8_t svcId) {
    static constexpr uint16_t COLORS[] = {
        Theme::DIM,      // 0: none
        Theme::RED,      // 1: motion
        Theme::VIOLET,   // 2: config
        Theme::YELLOW,   // 3: safety
        Theme::CYAN,     // 4: encoder
        Theme::MAGENTA,  // 5: sysid
        Theme::GREEN,    // 6: comms
        Theme::TEXT,     // 7: UI
        Theme::ORANGE,   // 8: dispatch
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

// Tick marker colours: dark, so the blocks stay readable over them (index = tick code)
static uint16_t tickColor(uint32_t tick) {
    switch (tick) {
        case 1:  return shade(Theme::GREEN, 1, 2);    // 1-2 ms: normal
        case 2:  return shade(Theme::YELLOW, 1, 2);   // 3-10 ms: gap
        default: return shade(Theme::RED, 1, 2);      // >10 ms: idle
    }
}

// Find TaskLaneInfo by taskId
static const TaskLaneInfo* findTaskLane(uint8_t taskId) {
    for (uint8_t t = 0; t < TASK_LANE_COUNT; t++) {
        if (TASK_LANES[t].taskId == taskId) { return &TASK_LANES[t]; }
    }
    return nullptr;
}

// Lane colour for a scheduler task, by its name (the RTOS names: Motor, Encoder, Comms,
// Display, Tmr Svc, IDLE)
static uint16_t taskColorByName(const char* name) {
    char p[4] = {};
    for (uint8_t i = 0; i < 3 && name[i] != '\0'; i++) {
        char c = name[i];
        p[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    if (strcmp(p, "mot") == 0) return TASK_LANES[0].color;
    if (strcmp(p, "enc") == 0) return TASK_LANES[1].color;
    if (strcmp(p, "com") == 0) return TASK_LANES[2].color;
    if (strcmp(p, "dis") == 0) return TASK_LANES[3].color;
    if (strcmp(p, "tmr") == 0 || strcmp(p, "tim") == 0) return TASK_LANES[4].color;
    return Theme::DIM;
}

static void setPx(uint8_t* buf, uint16_t row, uint16_t color)
{
    buf[row * 2] = static_cast<uint8_t>(color >> 8);
    buf[row * 2 + 1] = static_cast<uint8_t>(color & 0xFF);
}

// A medium-font cell with the text right-aligned
static void rightBox(Services::ICanvas& lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     const char* text, uint16_t fg)
{
    size_t n = strlen(text);
    size_t maxN = (w > 2) ? (w - 2) / Theme::MED_W : 0;
    if (n > maxN) n = maxN;
    auto pad = static_cast<uint16_t>(w - n * Theme::MED_W - 2);
    Chrome::textBox(lcd, x, y, w, h, pad, 2, text, fg, Theme::BG);
}

// A small-font section heading with a rule running to the right edge
static void sectionHeading(Services::ICanvas& lcd, uint16_t y, const char* text)
{
    auto tw = static_cast<uint16_t>(strlen(text) * Theme::SMALL_W + 4);
    lcd.blitTextLine(MARGIN, y, tw, Theme::SMALL_H, text, Theme::CYAN, Theme::BG);
    lcd.fillRect(static_cast<uint16_t>(MARGIN + tw), static_cast<uint16_t>(y + 3),
                 static_cast<uint16_t>(LINE_W - tw), 1, Theme::GRID_MAJ);
}

const char* TaskMonitorScreen::title() const
{
    switch (m_mode) {
        case Mode::TIMELINE: return "Task Timeline";
        case Mode::LEGEND:   return "Task Legend";
        default:             return "Tasks";
    }
}

const char* TaskMonitorScreen::centerHint() const
{
    static constexpr const char* PAUSE = "PAUSE";
    static constexpr const char* RESUME = "RESUME";
    if (m_mode != Mode::TIMELINE) return nullptr;
    return m_timelinePaused ? RESUME : PAUSE;
}

void TaskMonitorScreen::onActivate()
{
    m_needsRedraw = true;
    m_full = true;
    m_dirty = true;
    m_mode = Mode::STATS;
    m_timelinePaused = false;
    m_zoomLevel = 0;
    m_expandedTask = 0xFF;
    m_taskCursor = 0;
    m_lastTotal = 0;
    m_scrollOffset = 0;
    m_frameSkip = 0;
    m_activeLaneCount = 0;

    Platform::ITaskStats* stats = Platform::taskStatsProvider();
    if (stats != nullptr) {
        Platform::TaskStatsSnapshot snap;
        stats->getSnapshot(snap);
        m_lastTaskCount = snap.count;
    }
}

void TaskMonitorScreen::switchMode(Mode m)
{
    m_mode = m;
    m_needsRedraw = true;      // new title: the manager clears and frames the page
    m_full = true;
    m_dirty = true;
}

void TaskMonitorScreen::render(Services::ICanvas& lcd)
{
    switch (m_mode) {
        case Mode::TIMELINE: renderTimeline(lcd); break;
        case Mode::LEGEND:   renderLegend(lcd);   break;
        default:             renderStats(lcd);    break;
    }
}

// =============================================================================
// STATS mode — task table
// =============================================================================

void TaskMonitorScreen::renderStats(Services::ICanvas& lcd)
{
    Platform::ITaskStats* stats = Platform::taskStatsProvider();

    if (m_full) {
        m_full = false;
        // Column heads over the table, one small-font strip of boxes
        lcd.blitTextLine(MARGIN, TOP, COL_ST_X - MARGIN, Theme::SMALL_H, " TASK", Theme::DIM, Theme::BG);
        lcd.blitTextLine(COL_ST_X, TOP, COL_PR_X - COL_ST_X, Theme::SMALL_H, "ST", Theme::DIM, Theme::BG);
        lcd.blitTextLine(COL_PR_X, TOP, COL_STK_X - COL_PR_X, Theme::SMALL_H, "PR", Theme::DIM, Theme::BG);
        lcd.blitTextLine(COL_STK_X, TOP, COL_BAR_X - COL_STK_X, Theme::SMALL_H, "STACK", Theme::DIM, Theme::BG);
        lcd.blitTextLine(COL_BAR_X, TOP, static_cast<uint16_t>(Theme::WIDTH - MARGIN - COL_BAR_X),
                         Theme::SMALL_H, "  CPU", Theme::DIM, Theme::BG);
        lcd.fillRect(MARGIN, static_cast<uint16_t>(TABLE_Y - 3), LINE_W, 1, Theme::GRID_MAJ);
        Chrome::fieldRow(lcd, INFO_Y, "Free heap", "", Chrome::RowState::NORMAL);
        Chrome::fieldRow(lcd, static_cast<uint16_t>(INFO_Y + ROW_H), "Uptime", "", Chrome::RowState::NORMAL);
        Chrome::hintLine(lcd, STATS_HINT_Y, HINT_STATS, Theme::DIM);
        Chrome::drawDeck(lcd, static_cast<uint16_t>(STATS_HINT_Y + 12), Theme::CONTENT_BOTTOM);
        for (uint8_t i = 0; i < MAX_ROWS; i++) m_rows[i].valid = false;
        m_heapText[0] = m_upText[0] = '\0';
        if (stats == nullptr) {
            Chrome::textBox(lcd, MARGIN, TABLE_Y, LINE_W, 16, 6, 2, "No task stats", Theme::TEXT, Theme::BG);
        }
    }
    if (stats == nullptr) {
        return;
    }

    Platform::TaskStatsSnapshot snap;
    stats->getSnapshot(snap);
    m_lastTaskCount = snap.count;

    char buf[24];
    for (uint8_t i = 0; i < MAX_ROWS; i++) {
        auto y = static_cast<uint16_t>(TABLE_Y + i * ROW_H);
        RowCache& rc = m_rows[i];
        if (i >= snap.count) {
            if (rc.valid) {                                   // the task went away
                lcd.fillRect(MARGIN, y, LINE_W, ROW_H, Theme::BG);
                rc.valid = false;
            }
            continue;
        }
        const Platform::TaskStat& t = snap.tasks[i];
        char name[12];
        snprintf(name, sizeof name, "%.8s", t.name);
        const bool whole = !rc.valid || strcmp(name, rc.name) != 0;

        if (whole) {
            // Colour tick, the name, the row rule
            lcd.fillRect(MARGIN, y, 3, 16, taskColorByName(name));
            Chrome::textBox(lcd, COL_NAME_X, y, COL_NAME_W, 16, 3, 2, name, Theme::TEXT, Theme::BG);
            lcd.fillRect(MARGIN, static_cast<uint16_t>(y + 16), LINE_W, 1, Theme::GRID);
            lcd.fillRect(MARGIN, static_cast<uint16_t>(y + 17), LINE_W, 1, Theme::BG);
            Chrome::fillAround(lcd, COL_BAR_X, y, COL_BAR_W, 16, BAR_X, static_cast<uint16_t>(y + BAR_DY),
                               BAR_W, BAR_H, Theme::BG);
            memcpy(rc.name, name, sizeof rc.name);
        }
        if (whole || t.state != rc.state) {
            char s[2] = { stateChar(t.state), '\0' };
            Chrome::textBox(lcd, COL_ST_X, y, COL_ST_W, 16, 2, 2, s, stateColor(t.state), Theme::BG);
        }
        if (whole || t.priority != rc.priority) {
            snprintf(buf, sizeof buf, "%u", static_cast<unsigned>(t.priority));
            rightBox(lcd, COL_PR_X, y, COL_PR_W, 16, buf, Theme::VIOLET);
        }
        if (whole || t.stackHWM != rc.stack) {
            snprintf(buf, sizeof buf, "%u", static_cast<unsigned>(t.stackHWM));
            rightBox(lcd, COL_STK_X, y, COL_STK_W, 16, buf, t.stackHWM < 64 ? Theme::ORANGE : Theme::TEXT);
        }
        if (whole || t.cpuPercent != rc.cpu) {
            uint8_t cpu = t.cpuPercent > 100 ? 100 : t.cpuPercent;
            const uint16_t hot = cpu >= 80 ? Theme::ORANGE : Theme::CYAN;
            auto lit = static_cast<uint16_t>(cpu * BAR_W / 100);
            if (cpu > 0 && lit == 0) lit = 1;
            auto by = static_cast<uint16_t>(y + BAR_DY);
            if (lit > 0) lcd.fillRect(BAR_X, by, lit, BAR_H, hot);
            if (lit < BAR_W) lcd.fillRect(static_cast<uint16_t>(BAR_X + lit), by,
                                          static_cast<uint16_t>(BAR_W - lit), BAR_H, Theme::PANEL);
            snprintf(buf, sizeof buf, "%u%%", static_cast<unsigned>(t.cpuPercent));
            rightBox(lcd, COL_CPU_X, y, COL_CPU_W, 16, buf, hot);
        }
        rc.state = t.state;
        rc.priority = t.priority;
        rc.stack = t.stackHWM;
        rc.cpu = t.cpuPercent;
        rc.valid = true;
    }

    // System info
    Protocol::TelemetrySnapshot telem = Services::g_telemetryStore.getSnapshot();
    uint32_t secs = telem.system.uptimeTicks / 1000;
    uint32_t mins = secs / 60;
    secs %= 60;
    uint32_t hrs = mins / 60;
    mins %= 60;

    char val[sizeof m_heapText];
    snprintf(val, sizeof val, "%lu B", static_cast<unsigned long>(telem.system.freeHeap));
    if (strcmp(val, m_heapText) != 0) {
        memcpy(m_heapText, val, sizeof m_heapText);
        Chrome::fieldRow(lcd, INFO_Y, "Free heap", val, Chrome::RowState::NORMAL, 0, false);
    }
    snprintf(val, sizeof val, "%luh%02lum%02lus", static_cast<unsigned long>(hrs % 10000),
             static_cast<unsigned long>(mins), static_cast<unsigned long>(secs));
    if (strcmp(val, m_upText) != 0) {
        memcpy(m_upText, val, sizeof m_upText);
        Chrome::fieldRow(lcd, static_cast<uint16_t>(INFO_Y + ROW_H), "Uptime", val,
                         Chrome::RowState::NORMAL, 0, false);
    }
}

// =============================================================================
// TIMELINE mode — trace-based task activity graph
// =============================================================================

void TaskMonitorScreen::buildLayout(Layout& out, bool isExpanded, uint8_t expandedLaneIdx,
                                    const uint8_t* subSvcIds, uint8_t subCount) const
{
    memset(&out, 0, sizeof out);
    static constexpr uint16_t AVAIL_H = PLOT_H - 1;          // the last row closes the lanes
    uint8_t shift = 0;
    uint16_t y = 0;
    uint8_t n = 0;

    if (!isExpanded) {
        // --- NORMAL: equal-height task lanes ---
        uint16_t laneH = AVAIL_H / m_activeLaneCount;
        if (laneH > 52) { laneH = 52; }
        if (laneH < 16) { laneH = 16; }
        for (uint8_t i = 0; i < m_activeLaneCount; i++) {
            out.bands[n++] = { static_cast<uint8_t>(y), static_cast<uint8_t>(laneH),
                               m_activeTasks[i], 0xFF, shift, i };
            shift = static_cast<uint8_t>(shift + 4);
            y = static_cast<uint16_t>(y + laneH);
        }
    } else {
        // --- EXPANDED: compressed lanes + service sub-lanes ---
        static constexpr uint16_t COMPRESSED_H = 10;
        uint16_t compressedTotal = COMPRESSED_H * (m_activeLaneCount - 1);
        uint16_t expandedTotal = AVAIL_H - compressedTotal;
        uint16_t subLaneH = expandedTotal / subCount;
        if (subLaneH > 30) { subLaneH = 30; }
        if (subLaneH < 12) { subLaneH = 12; }
        for (uint8_t i = 0; i < m_activeLaneCount && n < MAX_BANDS; i++) {
            if (i == expandedLaneIdx) {
                for (uint8_t s = 0; s < subCount && n < MAX_BANDS; s++) {
                    out.bands[n++] = { static_cast<uint8_t>(y), static_cast<uint8_t>(subLaneH),
                                       m_activeTasks[i], subSvcIds[s], shift, i };
                    shift = static_cast<uint8_t>(shift + 1);
                    y = static_cast<uint16_t>(y + subLaneH);
                }
            } else {
                out.bands[n++] = { static_cast<uint8_t>(y), static_cast<uint8_t>(COMPRESSED_H),
                                   m_activeTasks[i], 0xFF, shift, i };
                shift = static_cast<uint8_t>(shift + 4);
                y = static_cast<uint16_t>(y + COMPRESSED_H);
            }
        }
    }
    out.count = n;
    out.bottom = static_cast<uint8_t>(y);
}

void TaskMonitorScreen::drawLabels(Services::ICanvas& lcd, const Layout& lay)
{
    const uint8_t cursor = m_timelinePaused ? m_taskCursor : 0xFF;
    bool firstSub = true;
    for (uint8_t b = 0; b < lay.count; b++) {
        const Band& band = lay.bands[b];
        const TaskLaneInfo* tl = findTaskLane(band.task);
        const char* name = tl ? tl->label : "?";
        char text[8];
        uint16_t fg = tl ? tl->color : Theme::TEXT;
        uint16_t bg = Theme::BG;
        uint16_t padX = 3;
        if (band.svc == 0xFF) {
            snprintf(text, sizeof text, "%s", name);
            if (band.lane == cursor) { fg = Theme::TEXT; bg = Theme::MAGENTA; }
        } else if (firstSub) {
            snprintf(text, sizeof text, "v%s", name);
            padX = 0;
            firstSub = false;
        } else {
            snprintf(text, sizeof text, "%s", serviceLabel(band.svc));
            fg = serviceColor(band.svc);
            padX = 12;
        }
        auto tw = static_cast<uint16_t>(strlen(text) * Theme::SMALL_W);
        if (tw > LABEL_W - padX) tw = static_cast<uint16_t>(LABEL_W - padX);
        auto by = static_cast<uint16_t>(PLOT_Y + band.y);
        auto ty = static_cast<uint16_t>(by + (band.h - Theme::SMALL_H) / 2);
        auto tx = static_cast<uint16_t>(LABEL_X + padX);
        if (bg == Theme::BG) {
            Chrome::fillAround(lcd, LABEL_X, by, LABEL_W, band.h, tx, ty, tw, Theme::SMALL_H, bg);
        } else {
            // The selection bar: a 14 px strip across the label cell, page colour around it
            uint16_t barH = band.h > 16 ? 14 : static_cast<uint16_t>(band.h - 2);
            auto barY = static_cast<uint16_t>(by + (band.h - barH) / 2);
            Chrome::fillAround(lcd, LABEL_X, by, LABEL_W, band.h, LABEL_X, barY, LABEL_W, barH, Theme::BG);
            Chrome::fillAround(lcd, LABEL_X, barY, LABEL_W, barH, tx, ty, tw, Theme::SMALL_H, bg);
        }
        lcd.blitTextLine(tx, ty, tw, Theme::SMALL_H, text, fg, bg);
    }
    lcd.fillRect(LABEL_X, static_cast<uint16_t>(PLOT_Y + lay.bottom), LABEL_W,
                 static_cast<uint16_t>(PLOT_H - lay.bottom), Theme::BG);
    m_labelCursor = cursor;
}

void TaskMonitorScreen::paintColumn(Services::ICanvas& lcd, uint16_t x, uint32_t code,
                                    uint16_t from, uint16_t to)
{
    const Layout& lay = m_layout;
    auto put = [&](uint16_t r0, uint16_t r1, uint16_t c) {
        if (r0 < from) r0 = from;
        if (r1 > to) r1 = to;
        for (uint16_t r = r0; r <= r1 && r1 >= r0; r++) setPx(m_colBuf, static_cast<uint16_t>(r - from), c);
    };
    const uint32_t tick = (code >> TICK_SHIFT) & 3;
    for (uint8_t b = 0; b < lay.count; b++) {
        const Band& band = lay.bands[b];
        auto top = static_cast<uint16_t>(band.y);
        auto last = static_cast<uint16_t>(band.y + band.h - 1);
        if (tick != 0) { put(top, last, tickColor(tick)); continue; }   // the marker crosses every lane
        uint16_t fill;
        if (band.svc == 0xFF) {
            uint32_t v = (code >> band.shift) & 0xF;
            const TaskLaneInfo* tl = findTaskLane(band.task);
            fill = v ? serviceColor(static_cast<uint8_t>(v - 1)) : laneBg(tl ? tl->color : Theme::TEXT);
        } else {
            uint16_t sc = serviceColor(band.svc);
            fill = ((code >> band.shift) & 1) ? sc : laneBg(sc);
        }
        put(top, top, Theme::GRID);
        put(static_cast<uint16_t>(top + 1), static_cast<uint16_t>(last - 1), fill);
        put(last, last, Theme::BLACK);
    }
    put(lay.bottom, lay.bottom, Theme::GRID);
    put(static_cast<uint16_t>(lay.bottom + 1), PLOT_H - 1, Theme::BLACK);
    lcd.blitColumn(static_cast<uint16_t>(PLOT_X + x), static_cast<uint16_t>(PLOT_Y + from),
                   static_cast<uint16_t>(to - from + 1), m_colBuf);
}

void TaskMonitorScreen::renderTimeline(Services::ICanvas& lcd)
{
    if (m_full) {
        m_full = false;
        Chrome::drawCorners(lcd, static_cast<uint16_t>(PLOT_X - 2), static_cast<uint16_t>(PLOT_Y - 2),
                            static_cast<uint16_t>(PLOT_W + 4), static_cast<uint16_t>(PLOT_H + 4), 8, Theme::CYAN);
        Chrome::drawDeck(lcd, static_cast<uint16_t>(TL_HINT_Y + 12), Theme::CONTENT_BOTTOM);
        for (uint16_t x = 0; x < PLOT_W; x++) m_colDrawn[x] = COL_UNKNOWN;
        m_labelsValid = false;
        m_headText[0] = '\0';
        m_axisText[0][0] = m_axisText[1][0] = '\0';
        m_hintShown = -1;
        m_dirty = true;
    }

    size_t total = Trace::getTotal();
    size_t count = Trace::getCount();
    bool newData = (total != m_lastTotal);
    if (newData) {
        m_lastTotal = total;
    }

    // --- Header: LIVE/PAUSED badge and the number of entries in the ring ---
    const int8_t pausedNow = m_timelinePaused ? 1 : 0;
    if (m_hintShown != pausedNow) {
        m_hintShown = pausedNow;
        Chrome::textBox(lcd, MARGIN, HEAD_Y, BADGE_W, HEAD_H, m_timelinePaused ? 6 : 14, 1,
                        m_timelinePaused ? "PAUSED" : "LIVE", Theme::BG,
                        m_timelinePaused ? Theme::YELLOW : Theme::GREEN);
        Chrome::hintLine(lcd, TL_HINT_Y, m_timelinePaused ? HINT_PAUSED : HINT_LIVE,
                         m_timelinePaused ? Theme::YELLOW : Theme::DIM);
    }
    {
        char head[sizeof m_headText];
        snprintf(head, sizeof head, "%u entries", static_cast<unsigned>(count));
        if (strcmp(head, m_headText) != 0) {
            memcpy(m_headText, head, sizeof m_headText);
            rightBox(lcd, static_cast<uint16_t>(MARGIN + BADGE_W), static_cast<uint16_t>(HEAD_Y - 1),
                     static_cast<uint16_t>(LINE_W - BADGE_W), 16, head, Theme::TEXT);
        }
    }

    // --- Plot: live updates throttled to ~5 Hz; keys redraw at once ---
    bool update = m_dirty;
    if (newData && m_frameSkip == 0) m_frameSkip = 1;        // data pending: show it within 4 frames
    if (m_frameSkip > 0 && !m_timelinePaused && !update) {
        if (++m_frameSkip > 4) update = true;
    }
    if (update) m_frameSkip = 0;
    if (!update) { return; }
    m_dirty = false;

    static constexpr uint16_t timelineW = PLOT_W;

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

    // --- Lane layout: a new one invalidates the labels and every column ---
    Layout lay;
    buildLayout(lay, isExpanded, expandedLaneIdx, subSvcIds, subCount);
    if (memcmp(&lay, &m_layout, sizeof lay) != 0) {
        m_layout = lay;
        m_labelsValid = false;
        for (uint16_t x = 0; x < PLOT_W; x++) m_colDrawn[x] = COL_UNKNOWN;
    }
    if (!m_labelsValid || m_labelCursor != (m_timelinePaused ? m_taskCursor : 0xFF)) {
        drawLabels(lcd, m_layout);
        m_labelsValid = true;
    }

    // Mapping from trace ids to bands
    int8_t taskToBand[6];
    memset(taskToBand, -1, sizeof(taskToBand));
    int8_t svcToBand[9];
    memset(svcToBand, -1, sizeof(svcToBand));
    for (uint8_t b = 0; b < m_layout.count; b++) {
        const Band& band = m_layout.bands[b];
        if (band.svc == 0xFF) {
            if (band.task < 6) taskToBand[band.task] = static_cast<int8_t>(b);
        } else if (band.svc <= 8) {
            svcToBand[band.svc] = static_cast<int8_t>(b);
        }
    }

    // A block of `w` columns from x for this entry (later entries cover earlier ones)
    auto plotBlock = [&](uint16_t x, uint16_t w, const Trace::Entry& e) {
        int8_t b = -1;
        uint32_t mask = 0, val = 0;
        if (isExpanded && e.taskId == m_expandedTask) {
            if (e.serviceId > 0 && e.serviceId <= 8) {
                b = svcToBand[e.serviceId];
                mask = 1; val = 1;
            }
        } else {
            b = taskToBand[e.taskId];
            mask = 0xF;
            val = static_cast<uint32_t>((e.serviceId <= 8 ? e.serviceId : 0) + 1);
        }
        if (b < 0) return;
        const uint8_t sh = m_layout.bands[b].shift;
        for (uint16_t c = x; c < x + w && c < PLOT_W; c++) {
            m_colNew[c] = (m_colNew[c] & ~(mask << sh)) | (val << sh);
        }
    };
    auto plotTick = [&](uint16_t x, uint32_t t) {
        if (x < PLOT_W) m_colNew[x] = (m_colNew[x] & ~(3u << TICK_SHIFT)) | (t << TICK_SHIFT);
    };
    memset(m_colNew, 0, sizeof m_colNew);

    char axisL[40];
    char axisR[40];

    if (m_zoomLevel == 0) {
        // --- ENTRY MODE: auto-fit, one column per entry ---
        uint16_t blockW = 3;
        if (count > 0) {
            blockW = static_cast<uint16_t>(timelineW / count);
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

        // Entry blocks, and tick markers at ms boundaries between consecutive entries
        uint32_t prevTick = 0;
        bool havePrev = false;
        for (size_t i = startIdx; i < count; i++) {
            if (!Trace::getEntry(i, entry)) continue;
            auto x = static_cast<uint16_t>((i - startIdx) * blockW);
            if (x >= timelineW) break;

            if (havePrev) {
                uint32_t prevMs = prevTick / 1000;
                uint32_t curMs  = entry.tick / 1000;
                if (curMs != prevMs && curMs > prevMs) {
                    uint32_t gapMs = curMs - prevMs;
                    plotTick(x, gapMs <= 2 ? 1u : (gapMs <= 10 ? 2u : 3u));
                }
            }
            prevTick = entry.tick;
            havePrev = true;

            if (entry.taskId == 0 || entry.taskId >= 6) continue;
            if (x + blockDraw > timelineW) continue;
            plotBlock(x, blockDraw, entry);
        }

        size_t endIdx = startIdx + visibleEntries;
        if (endIdx > count) endIdx = count;
        snprintf(axisL, sizeof axisL, "%u..%u/%u", static_cast<unsigned>(startIdx),
                 static_cast<unsigned>(endIdx), static_cast<unsigned>(count));
        snprintf(axisR, sizeof axisR, "zoom auto");
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

        // Entry blocks proportional to timestamp
        static constexpr uint16_t TIME_BLOCK_W = 2;
        for (size_t i = 0; i < count; i++) {
            if (!Trace::getEntry(i, entry)) continue;
            if (entry.taskId == 0 || entry.taskId >= 6) continue;
            if (entry.tick < windowStart || entry.tick > windowEnd) continue;

            uint32_t relTick = entry.tick - windowStart;
            auto x = static_cast<uint16_t>((relTick * static_cast<uint32_t>(timelineW)) / windowUs);
            if (x + TIME_BLOCK_W > timelineW) continue;
            plotBlock(x, TIME_BLOCK_W, entry);
        }

        // Tick markers: evenly spaced at 1ms intervals (adaptive step)
        uint16_t tickStepMs = 1;
        while (tickStepMs < windowMs &&
               (static_cast<uint32_t>(tickStepMs) * timelineW / windowMs) < 3) {
            tickStepMs *= 2;
        }
        for (uint16_t ms = 0; ms <= windowMs; ms += tickStepMs) {
            auto x = static_cast<uint16_t>((static_cast<uint32_t>(ms) * timelineW) / windowMs);
            if (x >= timelineW) break;
            plotTick(x, 1u);
        }

        snprintf(axisL, sizeof axisL, "%lu..%lu ms", static_cast<unsigned long>(windowStart / 1000),
                 static_cast<unsigned long>(windowEnd / 1000));
        snprintf(axisR, sizeof axisR, "zoom %ums", static_cast<unsigned>(windowMs));
    }

    // --- Send the columns that changed, and only the rows of the lanes that changed ---
    for (uint16_t x = 0; x < PLOT_W; x++) {
        const uint32_t was = m_colDrawn[x];
        const uint32_t now = m_colNew[x];
        if (was == now) continue;
        uint16_t from = PLOT_H, to = 0;
        if ((was & COL_UNKNOWN) != 0) {
            from = 0; to = PLOT_H - 1;
        } else if ((((was ^ now) >> TICK_SHIFT) & 3) != 0) {
            from = 0; to = static_cast<uint16_t>(m_layout.bottom - 1);
        } else {
            for (uint8_t b = 0; b < m_layout.count; b++) {
                const Band& band = m_layout.bands[b];
                uint32_t mask = (band.svc == 0xFF ? 0xFu : 1u) << band.shift;
                if (((was ^ now) & mask) == 0) continue;
                auto r0 = static_cast<uint16_t>(band.y + 1);
                auto r1 = static_cast<uint16_t>(band.y + band.h - 2);
                if (r0 < from) from = r0;
                if (r1 > to) to = r1;
            }
        }
        if (from <= to) paintColumn(lcd, x, now, from, to);
        m_colDrawn[x] = now;
    }

    // --- Axis under the plot: visible range left, zoom right ---
    if (strcmp(axisL, m_axisText[0]) != 0) {
        memcpy(m_axisText[0], axisL, sizeof m_axisText[0]);
        lcd.blitTextLine(static_cast<uint16_t>(PLOT_X - 2), AXIS_Y, 120, Theme::SMALL_H, axisL, Theme::CYAN, Theme::BG);
    }
    if (strcmp(axisR, m_axisText[1]) != 0) {
        memcpy(m_axisText[1], axisR, sizeof m_axisText[1]);
        char right[16];
        snprintf(right, sizeof right, "%11.11s", axisR);
        lcd.blitTextLine(static_cast<uint16_t>(PLOT_X + 118), AXIS_Y,
                         static_cast<uint16_t>(Theme::WIDTH - MARGIN - (PLOT_X + 118)), Theme::SMALL_H,
                         right, Theme::DIM, Theme::BG);
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
            switchMode(Mode::TIMELINE);
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
                    m_dirty = true;
                }
            } else {
                // Zoom in
                if (m_zoomLevel < ZOOM_LEVELS) {
                    m_zoomLevel++;
                    m_dirty = true;
                }
            }
            return InputResult::HANDLED;

        case JoyDirection::DOWN:
            if (m_timelinePaused) {
                // Move cursor down
                if (m_activeLaneCount > 0 && m_taskCursor < m_activeLaneCount - 1) {
                    m_taskCursor++;
                    m_dirty = true;
                }
            } else {
                // Zoom out
                if (m_zoomLevel > 0) {
                    m_zoomLevel--;
                    m_dirty = true;
                }
            }
            return InputResult::HANDLED;

        case JoyDirection::CENTER:
            if (m_timelinePaused) {
                // Resume
                m_timelinePaused = false;
                m_expandedTask = 0xFF;
                m_scrollOffset = 0;
            } else {
                // Pause
                m_timelinePaused = true;
                m_taskCursor = 0;
                m_scrollOffset = 0;
            }
            m_dirty = true;
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
                m_dirty = true;
            } else {
                // Back to stats
                m_scrollOffset = 0;
                switchMode(Mode::STATS);
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
                m_dirty = true;
            } else {
                // Go to legend
                switchMode(Mode::LEGEND);
            }
            return InputResult::HANDLED;

        default:
            return InputResult::HANDLED;
    }
}

// =============================================================================
// LEGEND mode — color key for services, tasks, and tick markers
// =============================================================================

void TaskMonitorScreen::renderLegend(Services::ICanvas& lcd)
{
    if (!m_full) return;              // static: drawn once per visit
    m_full = false;

    static constexpr uint16_t SWATCH_W = 12;
    static constexpr uint16_t SWATCH_H = 10;
    static constexpr uint16_t COL_W = LINE_W / 2;

    // One colour key: a swatch and the name in its colour (medium font)
    auto key = [&](uint16_t x, uint16_t y, uint16_t color, const char* name) {
        lcd.fillRect(x, static_cast<uint16_t>(y + 2), SWATCH_W, SWATCH_H, color);
        lcd.drawString(static_cast<uint16_t>(x + SWATCH_W + 6), static_cast<uint16_t>(y + 1), name,
                       color, Theme::BG, Theme::FONT_MEDIUM);
    };

    uint16_t y = TOP;

    // --- Service colors (what the blocks mean) ---
    sectionHeading(lcd, y, "SERVICES  block colours");
    y += 12;

    struct SvcEntry { uint8_t id; const char* name; };
    static constexpr SvcEntry SVCS[] = {
        {1, "Motion"}, {2, "Config"}, {3, "Safety"}, {4, "Encoder"},
        {5, "SysId"},  {6, "Comms"},  {7, "UI"},     {8, "Dispatch"}
    };

    for (uint8_t i = 0; i < 8; i += 2) {
        key(static_cast<uint16_t>(MARGIN + 2), y, serviceColor(SVCS[i].id), SVCS[i].name);
        key(static_cast<uint16_t>(MARGIN + 2 + COL_W), y, serviceColor(SVCS[i + 1].id), SVCS[i + 1].name);
        y += LEGEND_ROW;
    }
    y += 4;

    // --- Task lane colors ---
    sectionHeading(lcd, y, "TASK LANES");
    y += 12;
    for (uint8_t i = 0; i < TASK_LANE_COUNT; i += 2) {
        key(static_cast<uint16_t>(MARGIN + 2), y, TASK_LANES[i].color, TASK_LANES[i].label);
        if (i + 1 < TASK_LANE_COUNT) {
            key(static_cast<uint16_t>(MARGIN + 2 + COL_W), y, TASK_LANES[i + 1].color, TASK_LANES[i + 1].label);
        }
        y += LEGEND_ROW;
    }
    y += 4;

    // --- Tick marker colors ---
    sectionHeading(lcd, y, "TICK MARKERS  1 ms grid");
    y += 12;
    struct TickEntry { uint32_t code; uint16_t color; const char* text; };
    const TickEntry TICKS[] = {
        {1, Theme::GREEN,  "Green  = 1-2ms (normal)"},
        {2, Theme::YELLOW, "Yellow = 3-10ms (gap)"},
        {3, Theme::RED,    "Red    = >10ms (idle)"},
    };
    for (const TickEntry& t : TICKS) {
        lcd.fillRect(static_cast<uint16_t>(MARGIN + 6), y, 2, SWATCH_H, tickColor(t.code));
        lcd.blitTextLine(static_cast<uint16_t>(MARGIN + 20), static_cast<uint16_t>(y + 1),
                         static_cast<uint16_t>(LINE_W - 20), Theme::SMALL_H, t.text, t.color, Theme::BG);
        y += 13;
    }
    y += 4;

    // --- Zoom info ---
    lcd.fillRect(MARGIN, y, LINE_W, 1, Theme::GRID);
    y += 5;
    Chrome::hintLine(lcd, y, "Zoom: auto=entries, 1-128ms=time", Theme::VIOLET);
    y += 14;
    Chrome::hintLine(lcd, y, HINT_LEGEND, Theme::DIM);
    Chrome::drawDeck(lcd, static_cast<uint16_t>(y + 12), Theme::CONTENT_BOTTOM);
}

InputResult TaskMonitorScreen::handleLegendInput(JoyDirection dir)
{
    switch (dir) {
        case JoyDirection::LEFT:
            switchMode(Mode::TIMELINE);
            return InputResult::HANDLED;
        default:
            return InputResult::HANDLED;
    }
}

} // namespace UI
