/**
 * @file trace_screen.cpp
 * @brief Trace monitor screen implementation
 *
 * Four view modes:
 *
 * TRACE — scrollable text list with per-layer coloring
 *   Format: [indent]Src>Dst method [detail] [value]
 *   Indentation by layer depth (0/1/2 spaces)
 *   Pause mode: UP freezes display, title shows "PAUSED +N new"
 *
 * LEGEND — color key + boundary filter toggles
 *   Shows all layer abbreviations with full names and colors
 *   CENTER toggles individual boundary filters on/off
 *
 * GRAPH — timeline view (Magic Trace / SystemView style)
 *   Horizontal lanes per boundary, colored blocks show activity
 *   Time flows left to right, newest at right edge
 *
 * SERVICE — service-grouped timeline with boundary coloring
 *   Dynamic lanes per active service (Motion, Config, Safety, etc.)
 *   Blocks colored by boundary type showing driver chain visibility
 */

#include "ui/screens/trace_screen.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include "L3_services/infra/trace.hpp"
#include <stdio.h>
#include <string.h>

namespace UI {

static constexpr uint16_t MARGIN = 4;
static constexpr uint16_t TITLE_H = 16;
static constexpr uint16_t LABEL_COLOR = 0x07FF;  // Cyan
static constexpr uint16_t PAUSE_COLOR = 0xFFE0;  // Yellow
static constexpr uint16_t WHITE = 0xFFFF;
static constexpr uint16_t BG_COLOR = 0x0000;
static constexpr uint16_t SEL_BG = 0x001F;       // Dark blue (selected row)
static constexpr uint16_t DIM_GRAY = 0x4208;     // Dim lifeline color

// Zoom level → block width in pixels (0=auto, 1-7=fixed)
static constexpr uint8_t ZOOM_BLOCK_W[] = {0, 3, 5, 8, 12, 16, 20, 24};

// Per-layer RGB565 colors
static constexpr uint16_t COLOR_L1 = 0x07FF;  // cyan   — L1 transport
static constexpr uint16_t COLOR_L2 = 0x07E0;  // green  — L2 protocol
static constexpr uint16_t COLOR_L3 = 0xFFE0;  // yellow — L3 services
static constexpr uint16_t COLOR_L4 = 0xF81F;  // magenta — L4 drivers
static constexpr uint16_t COLOR_F  = 0xF800;  // red    — Foundation

struct BoundaryInfo {
    const char* leftName;
    uint16_t leftColor;
    const char* rightName;
    uint16_t rightColor;
    uint8_t indent;
};

// Map boundary bitmask -> human-readable names and layer colors
static BoundaryInfo getBoundaryInfo(uint8_t boundary) {
    switch (boundary) {
        case 1:  return {"Xpt", COLOR_L1, "Pro", COLOR_L2, 0};
        case 2:  return {"Pro", COLOR_L2, "Svc", COLOR_L3, 1};
        case 4:  return {"Svc", COLOR_L3, "Mot", COLOR_L4, 2};
        case 8:  return {"Svc", COLOR_L3, "Enc", COLOR_L4, 2};
        case 16: return {"Svc", COLOR_L3, "Saf", COLOR_F,  2};
        case 32: return {"Svc", COLOR_L3, "Cmd", WHITE,    2};
        default: return {"?",   WHITE,    "?",   WHITE,    0};
    }
}

// Legend table: boundary bit, abbreviation, full name, color
struct LegendRow {
    uint8_t boundaryBit;
    const char* abbrev;
    const char* fullName;
    uint16_t color;
};

static constexpr LegendRow LEGEND_ROWS[] = {
    { 1,  "Xpt", "Transport",    COLOR_L1 },
    { 2,  "Pro", "Protocol",     COLOR_L2 },
    { 4,  "Mot", "Motor Driver", COLOR_L4 },
    { 8,  "Enc", "Encoder",      COLOR_L4 },
    { 16, "Saf", "Safety",       COLOR_F  },
    { 32, "Cmd", "Commands",     WHITE    },
    { 0,  "Svc", "Services",     COLOR_L3 },  // Info-only (not filterable)
};

// Graph lane table: boundary bit, label, color (same order as LEGEND_ROWS[0..5])
struct GraphLane {
    uint8_t boundaryBit;
    const char* label;
    uint16_t color;
};

static constexpr GraphLane GRAPH_LANES_TABLE[] = {
    { 1,  "Xpt", COLOR_L1 },
    { 2,  "Pro", COLOR_L2 },
    { 4,  "Mot", COLOR_L4 },
    { 8,  "Enc", COLOR_L4 },
    { 16, "Saf", COLOR_F  },
    { 32, "Cmd", WHITE    },
};

// Map boundary bit to graph lane index (0-5), returns -1 if unknown
static int8_t boundaryToLane(uint8_t boundary) {
    switch (boundary) {
        case 1:  return 0;
        case 2:  return 1;
        case 4:  return 2;
        case 8:  return 3;
        case 16: return 4;
        case 32: return 5;
        default: return -1;
    }
}

// Short filter name for title bar
static const char* getFilterName(uint8_t mask) {
    if (mask == 0xFF) return nullptr;
    switch (mask) {
        case 1:  return "Xpt";
        case 2:  return "Pro";
        case 4:  return "Mot";
        case 8:  return "Enc";
        case 16: return "Saf";
        case 32: return "Cmd";
        default: return "Flt";
    }
}

// Check if entry passes the current filter
static bool entryMatchesFilter(const Trace::Entry& entry, uint8_t filterMask) {
    if (filterMask == 0xFF) return true;
    if (entry.boundary == 0) return false;
    return (entry.boundary & filterMask) != 0;
}

void TraceScreen::onActivate()
{
    m_needsRedraw = true;
    m_titleDrawn = false;
    m_scrollOffset = 0;
    m_autoScroll = true;
    m_lastTotal = 0;
    m_pauseTotal = 0;
    m_mode = Mode::TRACE;
    m_lastSvcMask = 0;
    m_lastLaneCount = 0;
}

void TraceScreen::render(LCD& lcd)
{
    // Clear screen on mode switch (prevents previous mode's content bleeding through)
    if (!m_titleDrawn) {
        lcd.fillScreen(BG_COLOR);
    }

    switch (m_mode) {
        case Mode::LEGEND:  renderLegend(lcd);  break;
        case Mode::GRAPH:   renderGraph(lcd);   break;
        case Mode::SERVICE: renderService(lcd); break;
        default:            renderTrace(lcd);   break;
    }
    m_needsRedraw = false;
}

// =============================================================================
// TRACE mode — scrollable text list
// =============================================================================

void TraceScreen::renderTrace(LCD& lcd)
{
    size_t total = Trace::getTotal();
    size_t count = Trace::getCount();
    bool newData = (total != m_lastTotal);
    if (newData) {
        m_lastTotal = total;
    }

    bool redrawTitle = false;
    bool redrawLines = false;

    if (!m_titleDrawn) {
        lcd.drawHLine(MARGIN, MARGIN + TITLE_H, LCD::WIDTH - 2 * MARGIN, LABEL_COLOR);
        m_titleDrawn = true;
        redrawTitle = true;
        redrawLines = true;
    }

    if (newData) {
        if (m_autoScroll) {
            m_scrollOffset = 0;
            redrawLines = true;
        }
        redrawTitle = true;
    }

    if (m_needsRedraw) {
        redrawTitle = true;
        redrawLines = true;
    }

    if (!redrawTitle && !redrawLines) return;

    // --- Title bar ---
    static constexpr uint16_t TITLE_W = LCD::WIDTH - 2 * MARGIN;
    if (redrawTitle) {
        const char* filterName = getFilterName(m_filterMask);
        char title[36];

        if (m_autoScroll) {
            if (filterName != nullptr) {
                snprintf(title, sizeof(title), "Trace:%s", filterName);
            } else {
                snprintf(title, sizeof(title), "Trace");
            }
            lcd.blitTextLine(MARGIN, MARGIN, TITLE_W, 8,
                             title, LABEL_COLOR, BG_COLOR);
        } else {
            size_t newCount = total - m_pauseTotal;
            if (filterName != nullptr) {
                snprintf(title, sizeof(title), "PAUSED +%u %s",
                         (unsigned)newCount, filterName);
            } else {
                snprintf(title, sizeof(title), "PAUSED +%u new",
                         (unsigned)newCount);
            }
            lcd.blitTextLine(MARGIN, MARGIN, TITLE_W, 8,
                             title, PAUSE_COLOR, BG_COLOR);
        }
    }

    if (!redrawLines) return;

    // --- Count filtered entries ---
    m_matchCount = 0;
    Trace::Entry tmpEntry;
    for (size_t i = 0; i < count; i++) {
        if (Trace::getEntry(i, tmpEntry) && entryMatchesFilter(tmpEntry, m_filterMask)) {
            m_matchCount++;
        }
    }

    // --- Trace lines (filtered) ---
    uint16_t y = MARGIN + TITLE_H + 4;
    static constexpr uint16_t LINE_W = LCD::WIDTH - (2 * MARGIN);

    size_t skipMatches = 0;
    if (m_matchCount > VISIBLE_LINES) {
        if (m_matchCount > m_scrollOffset + VISIBLE_LINES) {
            skipMatches = m_matchCount - VISIBLE_LINES - m_scrollOffset;
        }
    }

    Trace::Entry entry;
    char lineBuf[42];
    uint16_t colors[42];
    size_t matchIdx = 0;
    uint8_t lineIdx = 0;

    for (size_t i = 0; i < count && lineIdx < VISIBLE_LINES; i++) {
        if (!Trace::getEntry(i, entry)) continue;
        if (!entryMatchesFilter(entry, m_filterMask)) continue;

        if (matchIdx < skipMatches) {
            matchIdx++;
            continue;
        }
        matchIdx++;

        if (entry.boundary != 0 && entry.method != nullptr) {
            // --- Interface trace ---
            char dirChar = '>';
            for (const char* p = entry.tag; *p != '\0'; p++) {
                if (*p == '>' || *p == '<' || *p == '~') {
                    dirChar = *p;
                    break;
                }
            }

            BoundaryInfo bi = getBoundaryInfo(entry.boundary);
            const char* srcName = (dirChar != '<') ? bi.leftName  : bi.rightName;
            const char* dstName = (dirChar != '<') ? bi.rightName : bi.leftName;
            uint16_t srcColor   = (dirChar != '<') ? bi.leftColor  : bi.rightColor;
            uint16_t dstColor   = (dirChar != '<') ? bi.rightColor : bi.leftColor;

            const char* method = entry.method;
            if (strncmp(method, "motor.", 6) == 0) method += 6;
            else if (strncmp(method, "enc.", 4) == 0) method += 4;
            else if (strncmp(method, "safety.", 7) == 0) method += 7;

            uint8_t srcStart = bi.indent;
            uint8_t srcEnd   = srcStart + (uint8_t)strlen(srcName);
            uint8_t dstStart = srcEnd + 1;
            uint8_t dstEnd   = dstStart + (uint8_t)strlen(dstName);

            const char* detail = (entry.detail[0] != '\0') ? entry.detail : nullptr;

            if (entry.arg0 != 0) {
                bool useHex = (strstr(method, "Status") != nullptr
                            || strstr(method, "Param") != nullptr);
                if (detail != nullptr) {
                    snprintf(lineBuf, sizeof(lineBuf),
                             useHex ? "%*s%s%c%s %s %s 0x%lX" : "%*s%s%c%s %s %s %lu",
                             bi.indent, "", srcName, dirChar, dstName, method, detail,
                             static_cast<unsigned long>(entry.arg0));
                } else {
                    snprintf(lineBuf, sizeof(lineBuf),
                             useHex ? "%*s%s%c%s %s 0x%lX" : "%*s%s%c%s %s %lu",
                             bi.indent, "", srcName, dirChar, dstName, method,
                             static_cast<unsigned long>(entry.arg0));
                }
            } else if (detail != nullptr) {
                snprintf(lineBuf, sizeof(lineBuf), "%*s%s%c%s %s %s",
                         bi.indent, "", srcName, dirChar, dstName, method, detail);
            } else {
                snprintf(lineBuf, sizeof(lineBuf), "%*s%s%c%s %s",
                         bi.indent, "", srcName, dirChar, dstName, method);
            }

            size_t lineLen = strlen(lineBuf);
            for (size_t p = 0; p < lineLen; p++) colors[p] = WHITE;
            for (uint8_t p = srcStart; p < srcEnd; p++) colors[p] = srcColor;
            for (uint8_t p = dstStart; p < dstEnd; p++) colors[p] = dstColor;

            lcd.blitTextLineColored(MARGIN, y, LINE_W, LINE_HEIGHT,
                                    lineBuf, colors, BG_COLOR);
        } else {
            // --- Legacy trace ---
            char dirCh = (entry.dir == Trace::ENTRY) ? '>' : '<';
            if (entry.arg0 != 0) {
                snprintf(lineBuf, sizeof(lineBuf), "%c %s %lu",
                         dirCh, entry.tag,
                         static_cast<unsigned long>(entry.arg0));
            } else {
                snprintf(lineBuf, sizeof(lineBuf), "%c %s",
                         dirCh, entry.tag);
            }
            lcd.blitTextLine(MARGIN, y, LINE_W, LINE_HEIGHT,
                             lineBuf, WHITE, BG_COLOR);
        }

        lineIdx++;
        y += LINE_HEIGHT;
    }

    for (; lineIdx < VISIBLE_LINES; lineIdx++) {
        lcd.blitTextLine(MARGIN, y, LINE_W, LINE_HEIGHT,
                         "", WHITE, BG_COLOR);
        y += LINE_HEIGHT;
    }
}

// =============================================================================
// LEGEND mode — color key + filter toggles
// =============================================================================

void TraceScreen::renderLegend(LCD& lcd)
{
    if (!m_needsRedraw) return;
    m_titleDrawn = true;

    static constexpr uint16_t LINE_W = LCD::WIDTH - (2 * MARGIN);
    uint16_t y = MARGIN;

    // Title
    lcd.blitTextLine(MARGIN, y, LINE_W, 8, "Trace Legend", LABEL_COLOR, BG_COLOR);
    y += TITLE_H;
    lcd.drawHLine(MARGIN, y, LINE_W, LABEL_COLOR);
    y += 4;

    char lineBuf[42];
    uint16_t colors[42];

    for (uint8_t i = 0; i < LEGEND_ITEMS - 1; i++) {
        const LegendRow& row = LEGEND_ROWS[i];
        bool enabled = (m_filterMask & row.boundaryBit) != 0;
        uint16_t bg = (i == m_legendCursor) ? SEL_BG : BG_COLOR;

        snprintf(lineBuf, sizeof(lineBuf), " [%c] %s  %s",
                 enabled ? '*' : ' ', row.abbrev, row.fullName);

        size_t len = strlen(lineBuf);
        for (size_t p = 0; p < len; p++) colors[p] = WHITE;
        for (uint8_t p = 5; p < 8 && p < len; p++) colors[p] = row.color;

        lcd.blitTextLineColored(MARGIN, y, LINE_W, LINE_HEIGHT,
                                lineBuf, colors, bg);
        y += LINE_HEIGHT;
    }

    // Svc row (info only)
    {
        const LegendRow& row = LEGEND_ROWS[LEGEND_ITEMS - 1];
        uint16_t bg = (m_legendCursor == LEGEND_ITEMS - 1) ? SEL_BG : BG_COLOR;

        snprintf(lineBuf, sizeof(lineBuf), "      %s  %s (origin)",
                 row.abbrev, row.fullName);

        size_t len = strlen(lineBuf);
        for (size_t p = 0; p < len; p++) colors[p] = WHITE;
        for (uint8_t p = 6; p < 9 && p < len; p++) colors[p] = row.color;

        lcd.blitTextLineColored(MARGIN, y, LINE_W, LINE_HEIGHT,
                                lineBuf, colors, bg);
        y += LINE_HEIGHT;
    }

    lcd.blitTextLine(MARGIN, y, LINE_W, LINE_HEIGHT, "", WHITE, BG_COLOR);
    y += LINE_HEIGHT;

    lcd.blitTextLine(MARGIN, y, LINE_W, LINE_HEIGHT,
                     " >  call into layer", WHITE, BG_COLOR);
    y += LINE_HEIGHT;
    lcd.blitTextLine(MARGIN, y, LINE_W, LINE_HEIGHT,
                     " <  return from layer", WHITE, BG_COLOR);
    y += LINE_HEIGHT;
    lcd.blitTextLine(MARGIN, y, LINE_W, LINE_HEIGHT,
                     " ~  internal (same layer)", WHITE, BG_COLOR);
    y += LINE_HEIGHT;

    lcd.blitTextLine(MARGIN, y, LINE_W, LINE_HEIGHT, "", WHITE, BG_COLOR);
    y += LINE_HEIGHT;

    lcd.blitTextLine(MARGIN, y, LINE_W, LINE_HEIGHT,
                     " CTR:toggle  R:graph", LABEL_COLOR, BG_COLOR);
    y += LINE_HEIGHT;

    while (y + LINE_HEIGHT <= LCD::HEIGHT) {
        lcd.blitTextLine(MARGIN, y, LINE_W, LINE_HEIGHT, "", WHITE, BG_COLOR);
        y += LINE_HEIGHT;
    }
}

// =============================================================================
// GRAPH mode — timeline view
// =============================================================================

// Dim a color to ~25% brightness for lane background strips
static uint16_t dimColor(uint16_t c) {
    uint16_t r = (c >> 11) & 0x1F;
    uint16_t g = (c >> 5)  & 0x3F;
    uint16_t b =  c        & 0x1F;
    return ((r / 4) << 11) | ((g / 4) << 5) | (b / 4);
}

void TraceScreen::renderGraph(LCD& lcd)
{
    size_t total = Trace::getTotal();
    size_t count = Trace::getCount();
    bool newData = (total != m_lastTotal);
    if (newData) {
        m_lastTotal = total;
    }

    // --- Layout (must fit in 320px height) ---
    // Title: 4+16+4 = 24px. Lanes: 6×40=240px. Axis: 10px. Footer: 10px.
    // Total: 24 + 240 + 10 + 10 + margins = 288px (fits in 320)
    static constexpr uint16_t LINE_W = LCD::WIDTH - (2 * MARGIN);
    static constexpr uint16_t LANES_Y = MARGIN + TITLE_H + 4;
    static constexpr uint16_t LANE_H = 40;
    static constexpr uint16_t LANE_BAR_INSET = 1;
    static constexpr uint16_t LANE_BAR_H = LANE_H - (2 * LANE_BAR_INSET);

    uint16_t timelineRight = LCD::WIDTH - MARGIN;
    uint16_t timelineW = timelineRight - TIMELINE_X;

    // Determine what to redraw
    bool redrawTitle = false;
    bool redrawGraph = false;

    if (!m_titleDrawn) {
        m_titleDrawn = true;
        redrawTitle = true;
        redrawGraph = true;
    }

    if (newData) {
        redrawTitle = true;         // Always update title (+N counter)
        if (m_autoScroll) {
            // Throttle graph redraws to ~5Hz to reduce flicker
            static uint8_t frameSkip = 0;
            if (++frameSkip >= 4) {
                redrawGraph = true;
                frameSkip = 0;
            }
        }
        // Paused: do NOT redraw graph — freeze in place
    }

    if (m_needsRedraw) {
        redrawTitle = true;
        redrawGraph = true;
    }

    if (!redrawTitle && !redrawGraph) return;

    // --- Title bar ---
    if (redrawTitle) {
        const char* filterName = getFilterName(m_filterMask);
        char title[36];
        if (m_autoScroll) {
            if (filterName != nullptr) {
                snprintf(title, sizeof(title), "Timeline:%s  %u entries",
                         filterName, (unsigned)count);
            } else {
                snprintf(title, sizeof(title), "Timeline  %u entries",
                         (unsigned)count);
            }
            lcd.blitTextLine(MARGIN, MARGIN, LINE_W, 8, title, LABEL_COLOR, BG_COLOR);
        } else {
            size_t newCount = total - m_pauseTotal;
            if (filterName != nullptr) {
                snprintf(title, sizeof(title), "PAUSED +%u %s",
                         (unsigned)newCount, filterName);
            } else {
                snprintf(title, sizeof(title), "PAUSED +%u new",
                         (unsigned)newCount);
            }
            lcd.blitTextLine(MARGIN, MARGIN, LINE_W, 8, title, PAUSE_COLOR, BG_COLOR);
        }
        lcd.drawHLine(MARGIN, MARGIN + TITLE_H, LINE_W, LABEL_COLOR);
    }

    if (!redrawGraph) return;

    // --- Draw lane backgrounds + labels ---
    for (uint8_t lane = 0; lane < GRAPH_LANES; lane++) {
        const GraphLane& gl = GRAPH_LANES_TABLE[lane];
        uint16_t ly = LANES_Y + (lane * LANE_H) + LANE_BAR_INSET;

        // Dark background strip for the lane (so empty lanes are visible)
        lcd.fillRect(TIMELINE_X, ly, timelineW, LANE_BAR_H, dimColor(gl.color));

        // Lane label (scale 3 = 8x12 medium font — readable on LCD)
        lcd.drawString(MARGIN, ly + ((LANE_BAR_H - 12) / 2),
                        gl.label, gl.color, BG_COLOR, 3);
    }

    // --- Separator lines between lanes ---
    for (uint8_t lane = 0; lane <= GRAPH_LANES; lane++) {
        uint16_t ly = LANES_Y + (lane * LANE_H);
        lcd.drawHLine(TIMELINE_X, ly, timelineW, DIM_GRAY);
    }

    // --- Compute block width from zoom level ---
    uint16_t blockW = 3;
    if (m_zoomLevel == 0 && count > 0) {
        // Auto-fit: try to show all entries
        blockW = timelineW / count;
        if (blockW < 3) blockW = 3;
        if (blockW > 8) blockW = 8;
    } else if (m_zoomLevel > 0 && m_zoomLevel <= ZOOM_LEVELS) {
        blockW = ZOOM_BLOCK_W[m_zoomLevel];
    }
    uint16_t blockDraw = (blockW > 3) ? (blockW - 1) : blockW;
    uint16_t visibleEntries = timelineW / blockW;

    // --- Plot trace entries ---
    size_t startIdx = 0;
    if (count > visibleEntries) {
        if (count > m_scrollOffset + visibleEntries) {
            startIdx = count - visibleEntries - m_scrollOffset;
        }
    }

    if (count > 0) {
        Trace::Entry entry;
        for (size_t i = startIdx; i < count; i++) {
            if (!Trace::getEntry(i, entry)) continue;
            if (entry.boundary == 0) continue;

            int8_t lane = boundaryToLane(entry.boundary);
            if (lane < 0) continue;

            if ((entry.boundary & m_filterMask) == 0) continue;

            uint16_t x = TIMELINE_X + (uint16_t)(i - startIdx) * blockW;
            if (x + blockDraw > timelineRight) break;

            uint16_t ly = LANES_Y + ((uint16_t)lane * LANE_H) + LANE_BAR_INSET;
            lcd.fillRect(x, ly, blockDraw, LANE_BAR_H,
                         GRAPH_LANES_TABLE[lane].color);
        }
    }

    // --- Axis + footer below lanes ---
    static constexpr uint16_t FOOTER_Y = LANES_Y + (GRAPH_LANES * LANE_H) + 2;
    static constexpr uint16_t FOOTER2_Y = FOOTER_Y + 10;
    {
        char axisBuf[40];
        size_t endIdx = startIdx + visibleEntries;
        if (endIdx > count) endIdx = count;
        if (m_zoomLevel == 0) {
            snprintf(axisBuf, sizeof(axisBuf), " %u..%u/%u  zoom:auto",
                     (unsigned)startIdx, (unsigned)endIdx, (unsigned)count);
        } else {
            snprintf(axisBuf, sizeof(axisBuf), " %u..%u/%u  zoom:%u/%u",
                     (unsigned)startIdx, (unsigned)endIdx, (unsigned)count,
                     (unsigned)m_zoomLevel, (unsigned)ZOOM_LEVELS);
        }
        lcd.blitTextLine(MARGIN, FOOTER_Y, LINE_W, 8,
                         axisBuf, LABEL_COLOR, BG_COLOR);
    }
    {
        char ctrlBuf[40];
        if (m_autoScroll) {
            snprintf(ctrlBuf, sizeof(ctrlBuf), " CTR:pause UD:zoom L:back R:svc");
        } else {
            snprintf(ctrlBuf, sizeof(ctrlBuf), " CTR:resume LR:pan UD:zoom");
        }
        lcd.blitTextLine(MARGIN, FOOTER2_Y, LINE_W, 8, ctrlBuf,
                         m_autoScroll ? LABEL_COLOR : PAUSE_COLOR, BG_COLOR);
    }
}

// =============================================================================
// SERVICE mode — service-grouped timeline
// =============================================================================

struct ServiceLaneInfo {
    uint8_t serviceId;
    const char* label;
    uint16_t color;
};

static constexpr ServiceLaneInfo SERVICE_LANES_TABLE[] = {
    { 1, "Mot", 0xF800 },   // SVC_MOTION  — red
    { 2, "Cfg", 0x5DDF },   // SVC_CONFIG  — light blue
    { 3, "Saf", 0xFFE0 },   // SVC_SAFETY  — yellow
    { 4, "Enc", 0x07FF },   // SVC_ENCODER — cyan
    { 5, "SId", 0xF81F },   // SVC_SYSID   — magenta
    { 6, "Com", 0x07E0 },   // SVC_COMMS   — green
    { 7, "UI",  0xC618 },   // SVC_UI      — light gray
    { 8, "Dsp", 0xFD20 },   // SVC_DISPATCH— orange
};

static constexpr uint8_t SERVICE_TABLE_SIZE =
    sizeof(SERVICE_LANES_TABLE) / sizeof(SERVICE_LANES_TABLE[0]);

// Map boundary to block color (within service lanes)
static uint16_t boundaryBlockColor(uint8_t boundary) {
    switch (boundary) {
        case 1:  return COLOR_L1;  // transport — cyan
        case 2:  return COLOR_L2;  // protocol  — green
        case 4:  return COLOR_L4;  // motor drv — magenta
        case 8:  return COLOR_L4;  // encoder   — magenta
        case 16: return COLOR_F;   // safety    — red
        case 32: return WHITE;     // commands  — white
        default: return 0x8410;    // legacy (no boundary) — mid gray
    }
}

void TraceScreen::renderService(LCD& lcd)
{
    size_t total = Trace::getTotal();
    size_t count = Trace::getCount();
    bool newData = (total != m_lastTotal);
    if (newData) {
        m_lastTotal = total;
    }

    static constexpr uint16_t LINE_W = LCD::WIDTH - (2 * MARGIN);

    bool redrawTitle = false;
    bool redrawGraph = false;

    if (!m_titleDrawn) {
        m_titleDrawn = true;
        redrawTitle = true;
        redrawGraph = true;
    }

    if (newData) {
        redrawTitle = true;
        if (m_autoScroll) {
            // Throttle graph redraws to ~5Hz to reduce flicker
            static uint8_t frameSkip = 0;
            if (++frameSkip >= 4) {
                redrawGraph = true;
                frameSkip = 0;
            }
        }
    }

    if (m_needsRedraw) {
        redrawTitle = true;
        redrawGraph = true;
    }

    if (!redrawTitle && !redrawGraph) return;

    // --- Title bar ---
    if (redrawTitle) {
        char title[36];
        if (m_autoScroll) {
            snprintf(title, sizeof(title), "Services  %u entries", (unsigned)count);
            lcd.blitTextLine(MARGIN, MARGIN, LINE_W, 8, title, LABEL_COLOR, BG_COLOR);
        } else {
            size_t newCount = total - m_pauseTotal;
            snprintf(title, sizeof(title), "PAUSED +%u new", (unsigned)newCount);
            lcd.blitTextLine(MARGIN, MARGIN, LINE_W, 8, title, PAUSE_COLOR, BG_COLOR);
        }
        lcd.drawHLine(MARGIN, MARGIN + TITLE_H, LINE_W, LABEL_COLOR);
    }

    if (!redrawGraph) return;

    // --- Scan entries to find active services ---
    uint16_t activeMask = 0;
    Trace::Entry entry;
    for (size_t i = 0; i < count; i++) {
        if (Trace::getEntry(i, entry) && entry.serviceId > 0 && entry.serviceId <= 8) {
            activeMask |= (1 << entry.serviceId);
        }
    }

    // Build active lane list (only services with data)
    uint8_t activeLanes[MAX_SERVICE_LANES];
    uint8_t laneCount = 0;
    for (uint8_t s = 0; s < SERVICE_TABLE_SIZE && laneCount < MAX_SERVICE_LANES; s++) {
        if (activeMask & (1 << SERVICE_LANES_TABLE[s].serviceId)) {
            activeLanes[laneCount++] = s;
        }
    }

    if (laneCount == 0) {
        lcd.blitTextLine(MARGIN, MARGIN + TITLE_H + 8, LINE_W, LINE_HEIGHT,
                         " No service activity", DIM_GRAY, BG_COLOR);
        return;
    }

    // --- Adaptive lane height based on active service count ---
    static constexpr uint16_t LANES_Y = MARGIN + TITLE_H + 4;
    static constexpr uint16_t FOOTER_SPACE = 22;
    uint16_t availH = LCD::HEIGHT - LANES_Y - FOOTER_SPACE;
    uint16_t laneH = availH / laneCount;
    if (laneH > 40) { laneH = 40; }
    if (laneH < 20) { laneH = 20; }

    uint16_t timelineRight = LCD::WIDTH - MARGIN;
    uint16_t timelineW = timelineRight - TIMELINE_X;
    static constexpr uint16_t LANE_BAR_INSET = 1;
    uint16_t laneBarH = laneH - (2 * LANE_BAR_INSET);

    // --- Draw lane backgrounds + labels ---
    // Only clear label column + below-lanes area when layout changes
    // (lane set or count changed). This eliminates the main flicker source.
    bool layoutChanged = (activeMask != m_lastSvcMask) || (laneCount != m_lastLaneCount);
    if (layoutChanged) {
        m_lastSvcMask = activeMask;
        m_lastLaneCount = laneCount;

        uint16_t lanesBottom = LANES_Y + (laneCount * laneH);
        lcd.fillRect(MARGIN, LANES_Y, TIMELINE_X - MARGIN, LCD::HEIGHT - LANES_Y, BG_COLOR);
        if (lanesBottom < LCD::HEIGHT) {
            lcd.fillRect(TIMELINE_X, lanesBottom, timelineW, LCD::HEIGHT - lanesBottom, BG_COLOR);
        }
    }

    for (uint8_t i = 0; i < laneCount; i++) {
        const ServiceLaneInfo& sli = SERVICE_LANES_TABLE[activeLanes[i]];
        uint16_t ly = LANES_Y + (i * laneH) + LANE_BAR_INSET;

        lcd.fillRect(TIMELINE_X, ly, timelineW, laneBarH, dimColor(sli.color));
        if (layoutChanged) {
            lcd.drawString(MARGIN, ly + ((laneBarH - 12) / 2),
                           sli.label, sli.color, BG_COLOR, 3);
        }
    }

    // Separator lines between lanes
    for (uint8_t i = 0; i <= laneCount; i++) {
        uint16_t ly = LANES_Y + (i * laneH);
        lcd.drawHLine(TIMELINE_X, ly, timelineW, DIM_GRAY);
    }

    // --- Compute block width from zoom level ---
    uint16_t blockW = 3;
    if (m_zoomLevel == 0 && count > 0) {
        blockW = timelineW / count;
        if (blockW < 3) { blockW = 3; }
        if (blockW > 8) { blockW = 8; }
    } else if (m_zoomLevel > 0 && m_zoomLevel <= ZOOM_LEVELS) {
        blockW = ZOOM_BLOCK_W[m_zoomLevel];
    }
    uint16_t blockDraw = (blockW > 3) ? (blockW - 1) : blockW;
    uint16_t visibleEntries = timelineW / blockW;

    // --- Determine visible range ---
    size_t startIdx = 0;
    if (count > visibleEntries) {
        if (count > m_scrollOffset + visibleEntries) {
            startIdx = count - visibleEntries - m_scrollOffset;
        }
    }

    // --- Build serviceId → lane index map ---
    int8_t svcToLane[9];
    for (uint8_t i = 0; i < 9; i++) { svcToLane[i] = -1; }
    for (uint8_t i = 0; i < laneCount; i++) {
        svcToLane[SERVICE_LANES_TABLE[activeLanes[i]].serviceId] = static_cast<int8_t>(i);
    }

    // --- Plot entries (colored by boundary within service lane) ---
    if (count > 0) {
        for (size_t i = startIdx; i < count; i++) {
            if (!Trace::getEntry(i, entry)) continue;
            if (entry.serviceId == 0 || entry.serviceId > 8) continue;

            int8_t lane = svcToLane[entry.serviceId];
            if (lane < 0) continue;

            uint16_t x = TIMELINE_X + static_cast<uint16_t>(i - startIdx) * blockW;
            if (x + blockDraw > timelineRight) break;

            uint16_t ly = LANES_Y + (static_cast<uint16_t>(lane) * laneH) + LANE_BAR_INSET;
            lcd.fillRect(x, ly, blockDraw, laneBarH, boundaryBlockColor(entry.boundary));
        }
    }

    // --- Footer at fixed bottom position ---
    {
        uint16_t footerY = LCD::HEIGHT - FOOTER_SPACE;
        char axisBuf[40];
        size_t endIdx = startIdx + visibleEntries;
        if (endIdx > count) { endIdx = count; }
        if (m_zoomLevel == 0) {
            snprintf(axisBuf, sizeof(axisBuf), " %u..%u/%u  zoom:auto",
                     (unsigned)startIdx, (unsigned)endIdx, (unsigned)count);
        } else {
            snprintf(axisBuf, sizeof(axisBuf), " %u..%u/%u  zoom:%u/%u",
                     (unsigned)startIdx, (unsigned)endIdx, (unsigned)count,
                     (unsigned)m_zoomLevel, (unsigned)ZOOM_LEVELS);
        }
        lcd.blitTextLine(MARGIN, footerY, LINE_W, 8, axisBuf, LABEL_COLOR, BG_COLOR);

        char ctrlBuf[40];
        if (m_autoScroll) {
            snprintf(ctrlBuf, sizeof(ctrlBuf), " CTR:pause UD:zoom L:back");
        } else {
            snprintf(ctrlBuf, sizeof(ctrlBuf), " CTR:resume LR:pan UD:zoom");
        }
        lcd.blitTextLine(MARGIN, footerY + 10, LINE_W, 8, ctrlBuf,
                         m_autoScroll ? LABEL_COLOR : PAUSE_COLOR, BG_COLOR);
    }
}

// =============================================================================
// Input handling
// =============================================================================

InputResult TraceScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (!pressed) return InputResult::HANDLED;

    switch (m_mode) {
        case Mode::LEGEND:  return handleLegendInput(dir);
        case Mode::GRAPH:   return handleGraphInput(dir);
        case Mode::SERVICE: return handleServiceInput(dir);
        default:            return handleTraceInput(dir);
    }
}

InputResult TraceScreen::handleTraceInput(JoyDirection dir)
{
    size_t maxScroll = (m_matchCount > VISIBLE_LINES)
                     ? (m_matchCount - VISIBLE_LINES) : 0;

    switch (dir) {
        case JoyDirection::UP:
            if (m_scrollOffset < maxScroll) {
                m_scrollOffset++;
                if (m_autoScroll) {
                    m_autoScroll = false;
                    m_pauseTotal = Trace::getTotal();
                }
                m_needsRedraw = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::DOWN:
            if (m_scrollOffset > 0) {
                m_scrollOffset--;
                m_needsRedraw = true;
                if (m_scrollOffset == 0) {
                    m_autoScroll = true;
                }
            }
            return InputResult::HANDLED;

        case JoyDirection::CENTER:
            m_scrollOffset = 0;
            m_autoScroll = true;
            m_needsRedraw = true;
            return InputResult::HANDLED;

        case JoyDirection::RIGHT:
            m_mode = Mode::LEGEND;
            m_titleDrawn = false;
            m_needsRedraw = true;
            return InputResult::HANDLED;

        case JoyDirection::LEFT:
            return InputResult::EXIT_SCREEN;

        default:
            return InputResult::UNHANDLED;
    }
}

InputResult TraceScreen::handleLegendInput(JoyDirection dir)
{
    switch (dir) {
        case JoyDirection::UP:
            if (m_legendCursor > 0) {
                m_legendCursor--;
                m_needsRedraw = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::DOWN:
            if (m_legendCursor < LEGEND_ITEMS - 2) {
                m_legendCursor++;
                m_needsRedraw = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::CENTER: {
            if (m_legendCursor < LEGEND_ITEMS - 1) {
                uint8_t bit = LEGEND_ROWS[m_legendCursor].boundaryBit;
                m_filterMask ^= bit;
                if (m_filterMask == 0) {
                    m_filterMask = bit;
                }
                m_scrollOffset = 0;
                m_autoScroll = true;
                m_needsRedraw = true;
            }
            return InputResult::HANDLED;
        }

        case JoyDirection::RIGHT:
            // Forward to graph mode
            m_mode = Mode::GRAPH;
            m_titleDrawn = false;
            m_scrollOffset = 0;
            m_autoScroll = true;
            m_zoomLevel = 0;
            m_needsRedraw = true;
            return InputResult::HANDLED;

        case JoyDirection::LEFT:
            m_mode = Mode::TRACE;
            m_titleDrawn = false;
            m_needsRedraw = true;
            return InputResult::HANDLED;

        default:
            return InputResult::UNHANDLED;
    }
}

InputResult TraceScreen::handleGraphInput(JoyDirection dir)
{
    switch (dir) {
        case JoyDirection::UP:
            // Zoom in (larger blocks, more detail)
            if (m_zoomLevel < ZOOM_LEVELS) {
                m_zoomLevel++;
                m_scrollOffset = 0;
                m_needsRedraw = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::DOWN:
            // Zoom out (smaller blocks, more entries visible)
            if (m_zoomLevel > 0) {
                m_zoomLevel--;
                m_scrollOffset = 0;
                m_needsRedraw = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::CENTER:
            // Toggle pause/resume
            if (m_autoScroll) {
                m_autoScroll = false;
                m_pauseTotal = Trace::getTotal();
                m_scrollOffset = 0;
            } else {
                m_scrollOffset = 0;
                m_autoScroll = true;
            }
            m_needsRedraw = true;
            return InputResult::HANDLED;

        case JoyDirection::LEFT:
            if (!m_autoScroll) {
                // Pan left (older entries)
                m_scrollOffset += 8;
                m_needsRedraw = true;
            } else {
                // Back to legend
                m_mode = Mode::LEGEND;
                m_titleDrawn = false;
                m_scrollOffset = 0;
                m_autoScroll = true;
                m_needsRedraw = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::RIGHT:
            if (!m_autoScroll) {
                // Pan right (newer entries)
                if (m_scrollOffset > 8) {
                    m_scrollOffset -= 8;
                } else {
                    m_scrollOffset = 0;
                }
                m_needsRedraw = true;
            } else {
                // Forward to service mode
                m_mode = Mode::SERVICE;
                m_titleDrawn = false;
                m_scrollOffset = 0;
                m_autoScroll = true;
                m_zoomLevel = 0;
                m_needsRedraw = true;
            }
            return InputResult::HANDLED;

        default:
            return InputResult::UNHANDLED;
    }
}

InputResult TraceScreen::handleServiceInput(JoyDirection dir)
{
    switch (dir) {
        case JoyDirection::UP:
            if (m_zoomLevel < ZOOM_LEVELS) {
                m_zoomLevel++;
                m_scrollOffset = 0;
                m_needsRedraw = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::DOWN:
            if (m_zoomLevel > 0) {
                m_zoomLevel--;
                m_scrollOffset = 0;
                m_needsRedraw = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::CENTER:
            if (m_autoScroll) {
                m_autoScroll = false;
                m_pauseTotal = Trace::getTotal();
                m_scrollOffset = 0;
            } else {
                m_scrollOffset = 0;
                m_autoScroll = true;
            }
            m_needsRedraw = true;
            return InputResult::HANDLED;

        case JoyDirection::LEFT:
            if (!m_autoScroll) {
                // Pan left (older entries)
                m_scrollOffset += 8;
                m_needsRedraw = true;
            } else {
                // Back to graph
                m_mode = Mode::GRAPH;
                m_titleDrawn = false;
                m_scrollOffset = 0;
                m_autoScroll = true;
                m_needsRedraw = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::RIGHT:
            if (!m_autoScroll) {
                // Pan right (newer entries)
                if (m_scrollOffset > 8) {
                    m_scrollOffset -= 8;
                } else {
                    m_scrollOffset = 0;
                }
                m_needsRedraw = true;
            }
            return InputResult::HANDLED;

        default:
            return InputResult::UNHANDLED;
    }
}

} // namespace UI
