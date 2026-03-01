/**
 * @file dispatcher_screen.cpp
 * @brief Command dispatcher activity screen implementation
 *
 * Shows dispatch stats, queue depths with graphical history bars,
 * event stats, and scrollable recent command list.
 */

#include "ui/screens/dispatcher_screen.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include "F_platform/tasks/comms_task.hpp"
#include "L3_services/dispatch/command_queue.hpp"
#include "L3_services/dispatch/event_service.hpp"
#include "L3_services/infra/trace.hpp"
#include <stdio.h>
#include <string.h>

namespace UI {

static constexpr uint16_t MARGIN = 4;
static constexpr uint16_t TITLE_H = 16;
static constexpr uint16_t LINE_H = 12;
static constexpr uint16_t LABEL_COLOR = 0x07FF;  // Cyan
static constexpr uint16_t WHITE = 0xFFFF;
static constexpr uint16_t BG_COLOR = 0x0000;
static constexpr uint16_t GREEN = 0x07E0;
static constexpr uint16_t YELLOW = 0xFFE0;
static constexpr uint16_t RED = 0xF800;
static constexpr uint16_t DIM_GRAY = 0x4208;
static constexpr uint16_t LINE_W = LCD::WIDTH - (2 * MARGIN);

// Dim a color to ~25% brightness
static uint16_t dimColor(uint16_t c) {
    uint16_t r = (c >> 11) & 0x1F;
    uint16_t g = (c >> 5)  & 0x3F;
    uint16_t b =  c        & 0x1F;
    return ((r / 4) << 11) | ((g / 4) << 5) | (b / 4);
}

// Service info for activity bars
struct SvcBarInfo {
    uint8_t id;
    const char* label;
    uint16_t color;
};

static constexpr SvcBarInfo SVC_BAR_TABLE[] = {
    { 1, "Mot", 0xF800 },   // Motion   — red
    { 2, "Cfg", 0x5DDF },   // Config   — light blue
    { 3, "Saf", 0xFFE0 },   // Safety   — yellow
    { 4, "Enc", 0x07FF },   // Encoder  — cyan
    { 5, "SId", 0xF81F },   // SysId    — magenta
    { 6, "Com", 0x07E0 },   // Comms    — green
    { 7, "UI",  0xC618 },   // UI       — light gray
    { 8, "Dsp", 0xFD20 },   // Dispatch — orange
};

static constexpr uint8_t SVC_BAR_COUNT =
    sizeof(SVC_BAR_TABLE) / sizeof(SVC_BAR_TABLE[0]);

// Bar graph dimensions
static constexpr uint16_t BAR_H = 20;
static constexpr uint16_t BAR_X = MARGIN;
static constexpr uint16_t BAR_W = LINE_W;

void DispatcherScreen::onActivate()
{
    m_needsRedraw = true;
    m_titleDrawn = false;
    m_scrollOffset = 0;
    m_historyHead = 0;
    m_historyCount = 0;
    memset(m_cmdQueueHistory, 0, sizeof(m_cmdQueueHistory));
    memset(m_evtQueueHistory, 0, sizeof(m_evtQueueHistory));
}

void DispatcherScreen::render(LCD& lcd)
{
    if (!m_titleDrawn) {
        lcd.fillScreen(BG_COLOR);
        m_titleDrawn = true;
    }

    // Gather data
    Tasks::CommsDispatchStats dstats;
    Tasks::CommsTask_GetDispatchStats(dstats);

    uint8_t cmdQDepth = static_cast<uint8_t>(Services::g_commandQueue.getQueueDepth());
    Services::Event::Stats evtStats = Services::Event::getStats();

    // Record queue depths for history graph
    m_cmdQueueHistory[m_historyHead] = cmdQDepth;
    m_evtQueueHistory[m_historyHead] = evtStats.queueDepth;
    m_historyHead = (m_historyHead + 1) % DEPTH_HISTORY_SIZE;
    if (m_historyCount < DEPTH_HISTORY_SIZE) m_historyCount++;

    uint16_t y = MARGIN;
    char buf[40];

    // Title
    lcd.blitTextLine(MARGIN, y, LINE_W, 8,
                     "Dispatcher", LABEL_COLOR, BG_COLOR);
    y += TITLE_H;
    lcd.drawHLine(MARGIN, y, LINE_W, LABEL_COLOR);
    y += 4;

    // Command stats
    snprintf(buf, sizeof(buf), "Cmds:%lu  Unk:%lu",
             static_cast<unsigned long>(dstats.totalCommands),
             static_cast<unsigned long>(dstats.unknownCommands));
    lcd.blitTextLine(MARGIN, y, LINE_W, LINE_H, buf, WHITE, BG_COLOR);
    y += LINE_H;

    // Queue depths
    snprintf(buf, sizeof(buf), "CmdQ:%u/8  EvtQ:%u/8",
             (unsigned)cmdQDepth, (unsigned)evtStats.queueDepth);
    lcd.blitTextLine(MARGIN, y, LINE_W, LINE_H, buf, WHITE, BG_COLOR);
    y += LINE_H;

    // Event stats
    snprintf(buf, sizeof(buf), "Evt tx:%lu lost:%lu+%lu",
             static_cast<unsigned long>(evtStats.sent),
             static_cast<unsigned long>(evtStats.lostCritical),
             static_cast<unsigned long>(evtStats.lostInfo));
    lcd.blitTextLine(MARGIN, y, LINE_W, LINE_H, buf, WHITE, BG_COLOR);
    y += LINE_H + 2;

    // --- Service activity bars (from trace ring buffer) ---
    lcd.blitTextLine(MARGIN, y, LINE_W, 8, "Service activity", LABEL_COLOR, BG_COLOR);
    y += 10;

    {
        static constexpr uint16_t SVC_BAR_H = 10;
        static constexpr uint16_t SVC_LABEL_W = 26;
        static constexpr uint16_t SVC_PCT_W = 24;
        uint16_t svcBarW = LINE_W - SVC_LABEL_W - SVC_PCT_W - 4;
        uint16_t svcAreaStart = y;

        // Count entries per serviceId
        uint16_t svcCounts[9] = {};
        size_t traceCount = Trace::getCount();
        size_t totalSvc = 0;
        Trace::Entry trEntry;
        for (size_t i = 0; i < traceCount; i++) {
            if (Trace::getEntry(i, trEntry)
                && trEntry.serviceId > 0 && trEntry.serviceId <= 8) {
                svcCounts[trEntry.serviceId]++;
                totalSvc++;
            }
        }

        // Render bars for services with activity
        uint8_t activeSvcs = 0;
        for (uint8_t s = 0; s < SVC_BAR_COUNT; s++) {
            const SvcBarInfo& svc = SVC_BAR_TABLE[s];
            if (svcCounts[svc.id] == 0) { continue; }
            activeSvcs++;

            uint16_t pct = static_cast<uint16_t>(svcCounts[svc.id] * 100 / totalSvc);
            uint16_t barFill = static_cast<uint16_t>(svcCounts[svc.id] * svcBarW / totalSvc);
            if (barFill < 1) { barFill = 1; }

            // Label
            lcd.blitTextLine(MARGIN, y, SVC_LABEL_W, SVC_BAR_H,
                             svc.label, svc.color, BG_COLOR);
            // Bar background + fill
            uint16_t barX = MARGIN + SVC_LABEL_W + 2;
            lcd.fillRect(barX, y + 1, svcBarW, SVC_BAR_H - 2, dimColor(svc.color));
            lcd.fillRect(barX, y + 1, barFill, SVC_BAR_H - 2, svc.color);
            // Percentage
            snprintf(buf, sizeof(buf), "%u%%", (unsigned)pct);
            lcd.blitTextLine(barX + svcBarW + 2, y, SVC_PCT_W, SVC_BAR_H,
                             buf, WHITE, BG_COLOR);
            y += SVC_BAR_H;
        }

        if (activeSvcs == 0) {
            lcd.blitTextLine(MARGIN, y, LINE_W, SVC_BAR_H,
                             " No activity", DIM_GRAY, BG_COLOR);
            y += SVC_BAR_H;
        }

        // Pad to fixed height (max 8 bars) so everything below stays at same y
        uint16_t svcAreaEnd = svcAreaStart + SVC_BAR_COUNT * SVC_BAR_H;
        if (y < svcAreaEnd) {
            lcd.fillRect(MARGIN, y, LINE_W, svcAreaEnd - y, BG_COLOR);
            y = svcAreaEnd;
        }
    }
    y += 2;

    // Queue depth graph — command queue (top bar)
    lcd.blitTextLine(MARGIN, y, LINE_W, 8, "CmdQ history", DIM_GRAY, BG_COLOR);
    y += 10;
    renderQueueBar(lcd, BAR_X, y, BAR_W, BAR_H / 2, m_cmdQueueHistory, 8);
    y += BAR_H / 2 + 2;

    // Queue depth graph — event queue (bottom bar)
    lcd.blitTextLine(MARGIN, y, LINE_W, 8, "EvtQ history", DIM_GRAY, BG_COLOR);
    y += 10;
    renderQueueBar(lcd, BAR_X, y, BAR_W, BAR_H / 2, m_evtQueueHistory, 8);
    y += BAR_H / 2 + 4;

    // Separator
    lcd.drawHLine(MARGIN, y, LINE_W, DIM_GRAY);
    y += 4;

    // Recent commands header
    lcd.blitTextLine(MARGIN, y, LINE_W, 8, "Recent commands", LABEL_COLOR, BG_COLOR);
    y += LINE_H;

    // Recent commands list (scrollable)
    uint8_t visibleLines = 4;
    for (uint8_t i = 0; i < visibleLines; i++) {
        uint8_t idx = m_scrollOffset + i;
        if (idx < dstats.recentCount) {
            snprintf(buf, sizeof(buf), " %u. %s",
                     (unsigned)(idx + 1), dstats.recentCmds[idx]);
            lcd.blitTextLine(MARGIN, y, LINE_W, LINE_H, buf, WHITE, BG_COLOR);
        } else {
            lcd.blitTextLine(MARGIN, y, LINE_W, LINE_H, "", WHITE, BG_COLOR);
        }
        y += LINE_H;
    }

    // Footer at fixed bottom position
    lcd.blitTextLine(MARGIN, LCD::HEIGHT - 10, LINE_W, 8,
                     "UD:scroll  L:back", LABEL_COLOR, BG_COLOR);
}

InputResult DispatcherScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (!pressed) return InputResult::HANDLED;

    switch (dir) {
        case JoyDirection::LEFT:
            return InputResult::EXIT_SCREEN;

        case JoyDirection::UP:
            if (m_scrollOffset > 0) {
                m_scrollOffset--;
                m_needsRedraw = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::DOWN:
            if (m_scrollOffset < 7) {
                m_scrollOffset++;
                m_needsRedraw = true;
            }
            return InputResult::HANDLED;

        default:
            return InputResult::HANDLED;
    }
}

// =============================================================================
// Queue depth history bar graph
// =============================================================================

void DispatcherScreen::renderQueueBar(LCD& lcd, uint16_t x, uint16_t y,
                                       uint16_t w, uint16_t h,
                                       const uint8_t* history, uint8_t maxDepth)
{
    // Background
    lcd.fillRect(x, y, w, h, 0x0841);  // Very dark gray

    if (m_historyCount == 0 || maxDepth == 0) return;

    // Each sample gets a column; most recent on the right
    uint8_t sampleCount = m_historyCount;
    uint16_t colW = w / DEPTH_HISTORY_SIZE;
    if (colW < 1) colW = 1;

    // Draw from oldest visible to newest
    uint8_t startIdx = (m_historyHead + DEPTH_HISTORY_SIZE - sampleCount)
                       % DEPTH_HISTORY_SIZE;

    for (uint8_t s = 0; s < sampleCount; s++) {
        uint8_t idx = (startIdx + s) % DEPTH_HISTORY_SIZE;
        uint8_t depth = history[idx];
        if (depth == 0) continue;

        uint16_t barH = (depth * h) / maxDepth;
        if (barH > h) barH = h;
        if (barH < 1) barH = 1;

        // Color based on fill level
        uint16_t color;
        if (depth <= maxDepth / 4) {
            color = GREEN;
        } else if (depth <= maxDepth / 2) {
            color = YELLOW;
        } else {
            color = RED;
        }

        uint16_t bx = x + (s * w) / sampleCount;
        uint16_t bw = ((s + 1) * w) / sampleCount - (s * w) / sampleCount;
        if (bw < 1) bw = 1;

        lcd.fillRect(bx, y + h - barH, bw, barH, color);
    }
}

} // namespace UI
