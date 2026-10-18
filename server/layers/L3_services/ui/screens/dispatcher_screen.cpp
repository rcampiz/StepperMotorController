/**
 * @file dispatcher_screen.cpp
 * @brief Dispatcher: stat cells, per-service activity bars, queue depth history and the
 *        recent commands, under the chrome.
 *
 * Static parts (labels, brackets, section rules) are drawn once after activation; every
 * frame after that repaints only the cells, bars, history columns and list rows whose
 * content changed, each pixel once.
 */

#include "ui/screens/dispatcher_screen.hpp"
#include "ui/chrome/chrome.hpp"
#include "ui/theme.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "L3_services/comms/comms_service/comms_service.hpp"
#include "L3_services/dispatch/command_queue/command_queue.hpp"
#include "L3_services/motion/motion.hpp"
#include "L3_services/infra/trace/trace.hpp"
#include <stdio.h>
#include <string.h>

namespace UI {

using namespace Theme;

static constexpr uint16_t LINE_W = WIDTH - 2 * MARGIN;   // 228
static constexpr uint8_t QUEUE_MAX = 8;                   // both queues hold 8

// --- layout (content band 42..306) ---------------------------------------------------
// Stat cells: two columns of three, small label + medium value
static constexpr uint16_t CELLS_Y = CONTENT_Y + 1;        // 43..90
static constexpr uint16_t CELL_H = 16;
static constexpr uint16_t CELL_W = 112;
static constexpr uint16_t CELL_GAP = 4;
static constexpr uint16_t CELL_LABEL_W = 34;

// Service activity: section rule, then 8 bar rows inside bracket corners
static constexpr uint16_t SVC_HEAD_Y = 94;                // 94..101
static constexpr uint16_t BARS_Y = 106;                   // rows 106..185
static constexpr uint16_t SVC_ROW_H = 10;
static constexpr uint16_t SVC_LABEL_X = MARGIN + 4;
static constexpr uint16_t SVC_LABEL_W = 20;
static constexpr uint16_t TRACK_X = MARGIN + 28;
static constexpr uint16_t TRACK_W = 166;
static constexpr uint16_t TRACK_H = 6;
static constexpr uint16_t PCT_X = TRACK_X + TRACK_W + 4;  // 4 small chars, right-aligned
static constexpr uint16_t PCT_W = 24;

// Queue history: two graphs, label column on the left
static constexpr uint16_t HIST_Y0 = 192;                  // graph 192..207
static constexpr uint16_t HIST_Y1 = 213;                  // graph 213..228
static constexpr uint16_t HIST_H = 16;
static constexpr uint16_t HIST_COL_W = 3;                 // 2 px bar + 1 px gap
static constexpr uint16_t HIST_X = WIDTH - MARGIN - 2 - 60 * HIST_COL_W;   // 52

// Recent commands: section rule, 4 rows, hint
static constexpr uint16_t RECENT_HEAD_Y = 235;            // 235..242
static constexpr uint16_t RECENT_Y = 246;                 // rows 246..297
static constexpr uint16_t RECENT_ROW_H = 13;
static constexpr uint16_t RECENT_IDX_W = 24;
static constexpr uint16_t HINT_Y = 299;                   // 299..306

static_assert(HINT_Y + SMALL_H - 1 <= CONTENT_BOTTOM, "dispatcher layout runs into the footer");
static_assert(RECENT_Y + 4 * RECENT_ROW_H <= HINT_Y, "recent rows overlap the hint");
static_assert(PCT_X + PCT_W <= WIDTH - MARGIN - 2, "bar percentage runs past the brackets");

// Service info for activity bars (trace serviceId 1..8), colours from the theme
struct SvcBarInfo {
    uint8_t id;
    const char* label;
    uint16_t color;
};

static constexpr SvcBarInfo SVC_BAR_TABLE[] = {
    { 1, "Mot", RED },       // Motion
    { 2, "Cfg", VIOLET },    // Config
    { 3, "Saf", YELLOW },    // Safety
    { 4, "Enc", CYAN },      // Encoder
    { 5, "SId", MAGENTA },   // SysId
    { 6, "Com", GREEN },     // Comms
    { 7, "UI",  TEXT },      // UI
    { 8, "Dsp", ORANGE },    // Dispatch
};

static_assert(sizeof(SVC_BAR_TABLE) / sizeof(SVC_BAR_TABLE[0]) == 8, "one bar per service");

// Stat cell labels and their colours: commands cyan, queues magenta, events violet
static const char* const CELL_LABELS[6] = {"CMD", "UNK", "CMDQ", "EVTQ", "EVT", "LOST"};
static constexpr uint16_t CELL_LABEL_COLORS[6] = {CYAN, CYAN, MAGENTA, MAGENTA, VIOLET, VIOLET};

static uint16_t cellX(uint8_t i) { return static_cast<uint16_t>(MARGIN + (i % 2) * (CELL_W + CELL_GAP)); }
static uint16_t cellY(uint8_t i) { return static_cast<uint16_t>(CELLS_Y + (i / 2) * CELL_H); }

/// Queue fill colour: low green, half orange, beyond half red
static uint16_t depthColor(uint8_t depth)
{
    if (depth <= QUEUE_MAX / 4) return GREEN;
    if (depth <= QUEUE_MAX / 2) return ORANGE;
    return RED;
}

/// A value colour for a queue depth cell: plain until it is more than half full
static uint16_t depthTextColor(uint8_t depth)
{
    if (depth >= QUEUE_MAX) return RED;
    if (depth > QUEUE_MAX / 2) return ORANGE;
    return TEXT;
}

/// Section heading: small violet text, a rule to the right, room for a right-hand note
static void sectionHead(Services::ICanvas& lcd, uint16_t y, const char* text, uint16_t noteW)
{
    lcd.drawString(MARGIN, y, text, VIOLET, BG, FONT_SMALL);
    auto ruleX = static_cast<uint16_t>(MARGIN + strlen(text) * SMALL_W + 4);
    auto ruleEnd = static_cast<uint16_t>(WIDTH - MARGIN - noteW - 4);
    if (ruleEnd > ruleX) lcd.fillRect(ruleX, static_cast<uint16_t>(y + 4), static_cast<uint16_t>(ruleEnd - ruleX), 1, GRID_MAJ);
}

/// The right-hand note of a section heading, right-aligned in `w` px
static void sectionNote(Services::ICanvas& lcd, uint16_t y, uint16_t w, const char* text, uint16_t color)
{
    char buf[24];
    size_t cols = w / SMALL_W;
    if (cols > sizeof buf - 1) cols = sizeof buf - 1;
    size_t len = strlen(text);
    if (len > cols) len = cols;
    memset(buf, ' ', cols - len);
    memcpy(buf + (cols - len), text, len);
    buf[cols] = '\0';
    lcd.blitTextLine(static_cast<uint16_t>(WIDTH - MARGIN - w), y, w, SMALL_H, buf, color, BG);
}

static constexpr uint16_t SVC_NOTE_W = 14 * SMALL_W;
static constexpr uint16_t RECENT_NOTE_W = 5 * SMALL_W;

void DispatcherScreen::onActivate()
{
    m_needsRedraw = true;
    m_full = true;
    m_scrollOffset = 0;
    m_historyHead = 0;
    m_historyCount = 0;
    memset(m_cmdQueueHistory, 0, sizeof(m_cmdQueueHistory));
    memset(m_evtQueueHistory, 0, sizeof(m_evtQueueHistory));
    memset(m_cell, 0, sizeof(m_cell));
    memset(m_cellColor, 0, sizeof(m_cellColor));
    for (uint8_t i = 0; i < SVC_COUNT; i++) { m_svcFill[i] = 0xFFFF; m_svcPct[i] = 0; }
    memset(m_svcHead, 0, sizeof(m_svcHead));
    memset(m_drawnCmd, 0xFF, sizeof(m_drawnCmd));
    memset(m_drawnEvt, 0xFF, sizeof(m_drawnEvt));
    memset(m_recent, 0, sizeof(m_recent));       // the cleared page is the blank row
    memset(m_scrollText, 0, sizeof(m_scrollText));
}

void DispatcherScreen::drawStatic(Services::ICanvas& lcd)
{
    // Stat cell labels; a faint rule under the block
    for (uint8_t i = 0; i < CELL_COUNT; i++) {
        lcd.drawString(static_cast<uint16_t>(cellX(i) + 2), static_cast<uint16_t>(cellY(i) + 4),
                       CELL_LABELS[i], CELL_LABEL_COLORS[i], BG, FONT_SMALL);
    }

    // Service activity: heading and the bracket frame round the bars
    sectionHead(lcd, SVC_HEAD_Y, "SERVICE ACTIVITY", SVC_NOTE_W);
    Chrome::drawCorners(lcd, MARGIN, static_cast<uint16_t>(BARS_Y - 3), LINE_W,
                        static_cast<uint16_t>(SVC_COUNT * SVC_ROW_H + 5), 6, GRID_MAJ);

    // Queue history: labels and brackets (the interiors are painted column by column)
    const uint16_t ys[2] = {HIST_Y0, HIST_Y1};
    const char* const names[2] = {"CMDQ", "EVTQ"};
    for (uint8_t g = 0; g < 2; g++) {
        lcd.drawString(MARGIN, static_cast<uint16_t>(ys[g] + 0), names[g], MAGENTA, BG, FONT_SMALL);
        lcd.drawString(MARGIN, static_cast<uint16_t>(ys[g] + 8), "hist", DIM, BG, FONT_SMALL);
        Chrome::drawCorners(lcd, static_cast<uint16_t>(HIST_X - 2), static_cast<uint16_t>(ys[g] - 2),
                            static_cast<uint16_t>(DEPTH_HISTORY_SIZE * HIST_COL_W + 4),
                            static_cast<uint16_t>(HIST_H + 4), 5, GRID_MAJ);
    }

    // Recent commands heading and the controls hint
    sectionHead(lcd, RECENT_HEAD_Y, "RECENT COMMANDS", RECENT_NOTE_W);
    Chrome::hintLine(lcd, HINT_Y, "UP/DOWN scroll", DIM);
}

void DispatcherScreen::drawCell(Services::ICanvas& lcd, uint8_t i, const char* value, uint16_t color)
{
    if (color == m_cellColor[i] && strncmp(value, m_cell[i], sizeof m_cell[i]) == 0) return;
    snprintf(m_cell[i], sizeof m_cell[i], "%s", value);
    m_cellColor[i] = color;
    Chrome::textBox(lcd, static_cast<uint16_t>(cellX(i) + CELL_LABEL_W), cellY(i),
                    static_cast<uint16_t>(CELL_W - CELL_LABEL_W), CELL_H, 0, 2, value, color, BG);
}

void DispatcherScreen::drawServiceBars(Services::ICanvas& lcd)
{
    // Count trace entries per serviceId
    uint16_t svcCounts[SVC_COUNT + 1] = {};
    size_t traceCount = Trace::getCount();
    size_t totalSvc = 0;
    Trace::Entry trEntry;
    for (size_t i = 0; i < traceCount; i++) {
        if (Trace::getEntry(i, trEntry) && trEntry.serviceId > 0 && trEntry.serviceId <= SVC_COUNT) {
            svcCounts[trEntry.serviceId]++;
            totalSvc++;
        }
    }

    char buf[24];
    if (totalSvc == 0) snprintf(buf, sizeof buf, "no activity");
    else snprintf(buf, sizeof buf, "%u samples", static_cast<unsigned>(totalSvc));
    if (strcmp(buf, m_svcHead) != 0) {
        snprintf(m_svcHead, sizeof m_svcHead, "%s", buf);
        sectionNote(lcd, SVC_HEAD_Y, SVC_NOTE_W, buf, totalSvc == 0 ? DIM : TEXT);
    }

    for (uint8_t s = 0; s < SVC_COUNT; s++) {
        const SvcBarInfo& svc = SVC_BAR_TABLE[s];
        const uint16_t count = svcCounts[svc.id];
        uint16_t fill = 0;
        uint8_t pct = 0;
        if (count > 0) {
            pct = static_cast<uint8_t>(count * 100u / totalSvc);
            fill = static_cast<uint16_t>(count * static_cast<uint32_t>(TRACK_W) / totalSvc);
            if (fill < 1) fill = 1;
        }
        const bool first = (m_svcFill[s] == 0xFFFF);
        if (!first && fill == m_svcFill[s] && pct == m_svcPct[s]) continue;
        const bool wasActive = !first && m_svcFill[s] > 0;
        const bool active = fill > 0;
        auto y = static_cast<uint16_t>(BARS_Y + s * SVC_ROW_H);

        if (first || wasActive != active) {
            lcd.blitTextLine(SVC_LABEL_X, y, SVC_LABEL_W, SMALL_H, svc.label, active ? svc.color : DIM, BG);
        }
        // Track: neon fill then the black remainder, no pixel twice
        auto ty = static_cast<uint16_t>(y + 1);
        if (fill > 0) lcd.fillRect(TRACK_X, ty, fill, TRACK_H, svc.color);
        if (fill < TRACK_W) lcd.fillRect(static_cast<uint16_t>(TRACK_X + fill), ty,
                                         static_cast<uint16_t>(TRACK_W - fill), TRACK_H, BLACK);
        if (first || pct != m_svcPct[s] || wasActive != active) {
            snprintf(buf, sizeof buf, "%3u%%", static_cast<unsigned>(pct));
            lcd.blitTextLine(PCT_X, y, PCT_W, SMALL_H, buf, active ? TEXT : DIM, BG);
        }
        m_svcFill[s] = fill;
        m_svcPct[s] = pct;
    }
}

void DispatcherScreen::drawHistory(Services::ICanvas& lcd, uint16_t y, const uint8_t* history, uint8_t* drawn)
{
    // Column p shows the sample of age (59 - p); columns older than the history are empty
    for (uint8_t p = 0; p < DEPTH_HISTORY_SIZE; p++) {
        const uint8_t age = static_cast<uint8_t>(DEPTH_HISTORY_SIZE - 1 - p);
        uint8_t depth = 0;
        if (age < m_historyCount) {
            depth = history[(m_historyHead + DEPTH_HISTORY_SIZE - 1 - age) % DEPTH_HISTORY_SIZE];
            if (depth > QUEUE_MAX) depth = QUEUE_MAX;
        }
        if (depth == drawn[p]) continue;
        drawn[p] = depth;

        auto x = static_cast<uint16_t>(HIST_X + p * HIST_COL_W);
        auto barH = static_cast<uint16_t>(depth * HIST_H / QUEUE_MAX);
        if (depth > 0 && barH < 1) barH = 1;
        const auto emptyH = static_cast<uint16_t>(HIST_H - barH);
        if (emptyH > 0) lcd.fillRect(x, y, HIST_COL_W, emptyH, BLACK);
        if (barH > 0) {
            auto by = static_cast<uint16_t>(y + emptyH);
            lcd.fillRect(x, by, HIST_COL_W - 1, barH, depthColor(depth));
            lcd.fillRect(static_cast<uint16_t>(x + HIST_COL_W - 1), by, 1, barH, BLACK);
        }
    }
}

void DispatcherScreen::drawRecent(Services::ICanvas& lcd, const char (*cmds)[24], uint8_t count)
{
    char line[40];
    for (uint8_t i = 0; i < RECENT_VISIBLE; i++) {
        const uint8_t idx = static_cast<uint8_t>(m_scrollOffset + i);
        if (idx < count) snprintf(line, sizeof line, "%u.%.23s", static_cast<unsigned>(idx + 1), cmds[idx]);
        else line[0] = '\0';
        if (strcmp(line, m_recent[i]) == 0) continue;
        snprintf(m_recent[i], sizeof m_recent[i], "%s", line);

        auto y = static_cast<uint16_t>(RECENT_Y + i * RECENT_ROW_H);
        char num[12];
        if (idx < count) snprintf(num, sizeof num, "%u.", static_cast<unsigned>(idx + 1));
        else num[0] = '\0';
        Chrome::textBox(lcd, MARGIN, y, RECENT_IDX_W, RECENT_ROW_H, 2, 1, num, VIOLET, BG);
        Chrome::textBox(lcd, static_cast<uint16_t>(MARGIN + RECENT_IDX_W), y,
                        static_cast<uint16_t>(LINE_W - RECENT_IDX_W), RECENT_ROW_H, 2, 1,
                        idx < count ? cmds[idx] : "", TEXT, BG);
    }

    // Which part of the list is on show
    char note[40];
    if (count == 0) snprintf(note, sizeof note, "none");
    else {
        unsigned last = m_scrollOffset + RECENT_VISIBLE;
        if (last > count) last = count;
        unsigned firstShown = (m_scrollOffset < count) ? m_scrollOffset + 1u : last;
        snprintf(note, sizeof note, "%u-%u/%u", firstShown, last, static_cast<unsigned>(count));
    }
    if (strcmp(note, m_scrollText) != 0) {
        snprintf(m_scrollText, sizeof m_scrollText, "%s", note);
        sectionNote(lcd, RECENT_HEAD_Y, RECENT_NOTE_W, note, DIM);
    }
}

void DispatcherScreen::render(Services::ICanvas& lcd)
{
    if (m_full) {
        m_full = false;
        drawStatic(lcd);
    }

    // Gather data
    Harness::DispatchStats dstats;
    Services::g_commsService.getStats(dstats);
    auto cmdQDepth = static_cast<uint8_t>(Services::g_commandQueue.getQueueDepth());
    Services::MotorEventStats evtStats = Services::motion.stepper.event.getStats();

    // Record queue depths for the history graphs
    m_cmdQueueHistory[m_historyHead] = cmdQDepth;
    m_evtQueueHistory[m_historyHead] = evtStats.queueDepth;
    m_historyHead = static_cast<uint8_t>((m_historyHead + 1) % DEPTH_HISTORY_SIZE);
    if (m_historyCount < DEPTH_HISTORY_SIZE) m_historyCount++;

    // Stat cells
    char buf[24];
    snprintf(buf, sizeof buf, "%lu", static_cast<unsigned long>(dstats.totalCommands));
    drawCell(lcd, 0, buf, TEXT);
    snprintf(buf, sizeof buf, "%lu", static_cast<unsigned long>(dstats.unknownCommands));
    drawCell(lcd, 1, buf, dstats.unknownCommands > 0 ? ORANGE : TEXT);
    snprintf(buf, sizeof buf, "%u/%u", static_cast<unsigned>(cmdQDepth), static_cast<unsigned>(QUEUE_MAX));
    drawCell(lcd, 2, buf, depthTextColor(cmdQDepth));
    snprintf(buf, sizeof buf, "%u/%u", static_cast<unsigned>(evtStats.queueDepth), static_cast<unsigned>(QUEUE_MAX));
    drawCell(lcd, 3, buf, depthTextColor(evtStats.queueDepth));
    snprintf(buf, sizeof buf, "%lu", static_cast<unsigned long>(evtStats.sent));
    drawCell(lcd, 4, buf, TEXT);
    snprintf(buf, sizeof buf, "%lu+%lu", static_cast<unsigned long>(evtStats.lostCritical),
             static_cast<unsigned long>(evtStats.lostInfo));
    drawCell(lcd, 5, buf, evtStats.lostCritical > 0 ? RED : (evtStats.lostInfo > 0 ? ORANGE : TEXT));

    drawServiceBars(lcd);
    drawHistory(lcd, HIST_Y0, m_cmdQueueHistory, m_drawnCmd);
    drawHistory(lcd, HIST_Y1, m_evtQueueHistory, m_drawnEvt);

    uint8_t count = dstats.recentCount;
    if (count > RECENT_MAX) count = RECENT_MAX;
    drawRecent(lcd, dstats.recentCmds, count);
}

InputResult DispatcherScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (!pressed) return InputResult::HANDLED;

    switch (dir) {
        case JoyDirection::LEFT:
            return InputResult::EXIT_SCREEN;

        case JoyDirection::UP:
            if (m_scrollOffset > 0) m_scrollOffset--;
            return InputResult::HANDLED;

        case JoyDirection::DOWN:
            if (m_scrollOffset < RECENT_MAX - RECENT_VISIBLE) m_scrollOffset++;
            return InputResult::HANDLED;

        default:
            return InputResult::HANDLED;
    }
}

} // namespace UI
