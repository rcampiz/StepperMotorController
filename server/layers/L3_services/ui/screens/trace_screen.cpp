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
#include "harness/pins/icanvas.hpp"
#include "L3_services/infra/trace/trace.hpp"
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

// Per-layer RGB565 base colors (matches arch_screen)
static constexpr uint16_t COLOR_L1 = 0x07FF;  // cyan    — L1 transport
static constexpr uint16_t COLOR_L2 = 0x07E0;  // green   — L2 protocol
static constexpr uint16_t COLOR_L3 = 0xFD20;  // orange  — L3 services
static constexpr uint16_t COLOR_L4 = 0xF81F;  // magenta — L4 drivers
static constexpr uint16_t COLOR_L5 = 0xF800;  // red     — L5 board/HAL
static constexpr uint16_t COLOR_F  = 0xFFE0;  // yellow  — Foundation

// Shade variants for legend entries sharing a layer
static constexpr uint16_t COLOR_L2_TLM  = 0x0560;  // medium green  — L2 telemetry
static constexpr uint16_t COLOR_L4_ENC  = 0xB01F;  // medium purple — L4 encoder
static constexpr uint16_t COLOR_L4_LCD  = 0xD01F;  // light purple  — L4 LCD
static constexpr uint16_t COLOR_L4_FLS  = 0x901F;  // deep purple   — L4 flash
static constexpr uint16_t COLOR_F_LOCK  = 0xCE00;  // dim gold      — F mutex

struct BoundaryInfo {
    const char* leftName;
    uint16_t leftColor;
    const char* rightName;
    uint16_t rightColor;
    uint8_t indent;
};

// Map boundary bitmask -> human-readable names and layer colors
static BoundaryInfo getBoundaryInfo(uint16_t boundary) {
    switch (boundary) {
        case 1:     return {"Xpt", COLOR_L1, "Pro", COLOR_L2, 0};
        case 2:     return {"Pro", COLOR_L2, "Svc", COLOR_L3, 1};
        case 4:     return {"Svc", COLOR_L3, "Mot", COLOR_L4, 2};
        case 8:     return {"Svc", COLOR_L3, "Enc", COLOR_L4_ENC, 2};
        case 16:    return {"Svc", COLOR_L3, "Saf", COLOR_F,  2};
        case 32:    return {"Svc", COLOR_L3, "Cmd", COLOR_L3, 2};
        case 64:    return {"Drv", COLOR_L4, "SPI", COLOR_L5, 3};
        case 128:   return {"SPI", COLOR_L5, "Mtx", COLOR_F_LOCK,  4};
        case 0x100: return {"UI",  COLOR_F,  "LCD", COLOR_L4_LCD, 2};
        case 0x200: return {"Svc", COLOR_L3, "Fls", COLOR_L4_FLS, 2};
        case 0x400: return {"Pro", COLOR_L2, "Tlm", COLOR_L2_TLM, 1};
        default:    return {"?",   WHITE,    "?",   WHITE,    0};
    }
}

// Legend table: boundary bit, abbreviation, full name, color, layer tag
struct LegendRow {
    uint16_t boundaryBit;
    const char* abbrev;
    const char* fullName;
    uint16_t color;
    const char* layerTag;    // "L1", "L4", " F", etc.
    uint16_t layerColor;     // Color for the tag
};

// Ordered L1→L2→L3→L4→L5→F, Svc info-only last
static constexpr LegendRow LEGEND_ROWS[] = {
    { 1,      "Xpt", "Transport",  COLOR_L1,     "L1", COLOR_L1 },  // cyan
    { 2,      "Pro", "Protocol",   COLOR_L2,     "L2", COLOR_L2 },  // green
    { 0x400,  "Tlm", "Telemetry",  COLOR_L2_TLM, "L2", COLOR_L2 },  // med green
    { 32,     "Cmd", "Cmd Sink",   COLOR_L3,     "L3", COLOR_L3 },  // orange
    { 4,      "Mot", "Motor Drv",  COLOR_L4,     "L4", COLOR_L4 },  // magenta
    { 8,      "Enc", "Encoder",    COLOR_L4_ENC, "L4", COLOR_L4 },  // purple
    { 0x100,  "LCD", "LCD Drv",    COLOR_L4_LCD, "L4", COLOR_L4 },  // lt purple
    { 0x200,  "Fls", "Flash",      COLOR_L4_FLS, "L4", COLOR_L4 },  // dk purple
    { 64,     "SPI", "SPI Bus",    COLOR_L5,     "L5", COLOR_L5 },  // red
    { 16,     "Saf", "Safety",     COLOR_F,      " F", COLOR_F  },  // yellow
    { 128,    "Mtx", "Mutex",      COLOR_F_LOCK, " F", COLOR_F  },  // gold
    { 0,      "Svc", "Services",   COLOR_L3,     "  ", COLOR_L3 },  // info-only
};

// Graph lanes: one per layer, blocks colored by boundary shade
struct LayerLane {
    const char* label;
    uint16_t color;
    uint16_t boundaryMask;  // which boundaries touch this layer
};

static constexpr LayerLane LAYER_LANES[] = {
    {"L1",  COLOR_L1, 0x0001},  // L1_L2_TRANSPORT
    {"L2",  COLOR_L2, 0x0403},  // L1_L2_TRANSPORT + L2_L3_DISPATCH + L2_TELEMETRY
    {"L3",  COLOR_L3, 0x023E},  // L2_L3 + L3_L4_MOT + L3_L4_ENC + L3_F_SAF + L3_CMD_SINK + L4_FLASH(cfg svc)
    {"L4",  COLOR_L4, 0x034C},  // L3_L4_MOT + L3_L4_ENC + L4_L5_SPI + L4_LCD + L4_FLASH
    {"L5",  COLOR_L5, 0x00C0},  // L4_L5_SPI + L5_F_LOCK
    {"F",   COLOR_F,  0x0190},  // L3_F_SAFETY + L5_F_LOCK + L4_LCD(display task is F)
};

// Short filter name for title bar
static const char* getFilterName(uint16_t mask) {
    if (mask == 0x07FF) return nullptr;
    switch (mask) {
        case 1:     return "Xpt";
        case 2:     return "Pro";
        case 4:     return "Mot";
        case 8:     return "Enc";
        case 16:    return "Saf";
        case 32:    return "Cmd";
        case 64:    return "SPI";
        case 128:   return "Mtx";
        case 0x100: return "LCD";
        case 0x200: return "Fls";
        case 0x400: return "Tlm";
        default:    return "Flt";
    }
}

// Check if entry passes the current filter
static bool entryMatchesFilter(const Trace::Entry& entry, uint16_t filterMask) {
    if (filterMask == 0x07FF) return true;
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

void TraceScreen::render(Harness::ICanvas& lcd)
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

void TraceScreen::renderTrace(Harness::ICanvas& lcd)
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
        lcd.drawHLine(MARGIN, MARGIN + TITLE_H, Harness::Canvas::WIDTH - 2 * MARGIN, LABEL_COLOR);
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
    static constexpr uint16_t TITLE_W = Harness::Canvas::WIDTH - 2 * MARGIN;
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
    static constexpr uint16_t LINE_W = Harness::Canvas::WIDTH - (2 * MARGIN);

    size_t skipMatches = 0;
    if (m_matchCount > VISIBLE_LINES) {
        if (m_matchCount > m_scrollOffset + VISIBLE_LINES) {
            skipMatches = m_matchCount - VISIBLE_LINES - m_scrollOffset;
        }
    }

    // Hex index prefix: "XXXXX " = 6 chars
    static constexpr uint8_t HEX_PREFIX = 6;

    Trace::Entry entry;
    char lineBuf[48];
    uint16_t colors[48];
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

        // Absolute entry index (rolls over at FFFFF)
        unsigned long absIdx = static_cast<unsigned long>((total - count + i) & 0xFFFFF);

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
            else if (strncmp(method, "spi.", 4) == 0) method += 4;
            else if (strncmp(method, "lock.", 5) == 0) method += 5;

            uint8_t srcStart = HEX_PREFIX + bi.indent;
            uint8_t srcEnd   = srcStart + (uint8_t)strlen(srcName);
            uint8_t dstStart = srcEnd + 1;
            uint8_t dstEnd   = dstStart + (uint8_t)strlen(dstName);

            const char* detail = (entry.detail[0] != '\0') ? entry.detail : nullptr;

            if (entry.arg0 != 0) {
                bool useHex = (strstr(method, "Status") != nullptr
                            || strstr(method, "Param") != nullptr);
                if (detail != nullptr) {
                    snprintf(lineBuf, sizeof(lineBuf),
                             useHex ? "%05lX %*s%s%c%s %s %s 0x%lX"
                                    : "%05lX %*s%s%c%s %s %s %lu",
                             absIdx, bi.indent, "", srcName, dirChar, dstName,
                             method, detail, static_cast<unsigned long>(entry.arg0));
                } else {
                    snprintf(lineBuf, sizeof(lineBuf),
                             useHex ? "%05lX %*s%s%c%s %s 0x%lX"
                                    : "%05lX %*s%s%c%s %s %lu",
                             absIdx, bi.indent, "", srcName, dirChar, dstName,
                             method, static_cast<unsigned long>(entry.arg0));
                }
            } else if (detail != nullptr) {
                snprintf(lineBuf, sizeof(lineBuf), "%05lX %*s%s%c%s %s %s",
                         absIdx, bi.indent, "", srcName, dirChar, dstName,
                         method, detail);
            } else {
                snprintf(lineBuf, sizeof(lineBuf), "%05lX %*s%s%c%s %s",
                         absIdx, bi.indent, "", srcName, dirChar, dstName, method);
            }

            size_t lineLen = strlen(lineBuf);
            for (size_t p = 0; p < lineLen; p++) colors[p] = WHITE;
            for (uint8_t p = 0; p < 5; p++) colors[p] = DIM_GRAY;
            for (uint8_t p = srcStart; p < srcEnd; p++) colors[p] = srcColor;
            for (uint8_t p = dstStart; p < dstEnd; p++) colors[p] = dstColor;

            lcd.blitTextLineColored(MARGIN, y, LINE_W, LINE_HEIGHT,
                                    lineBuf, colors, BG_COLOR);
        } else {
            // --- Legacy trace ---
            char dirCh = (entry.dir == Trace::ENTRY) ? '>' : '<';
            if (entry.arg0 != 0) {
                snprintf(lineBuf, sizeof(lineBuf), "%05lX %c %s %lu",
                         absIdx, dirCh, entry.tag,
                         static_cast<unsigned long>(entry.arg0));
            } else {
                snprintf(lineBuf, sizeof(lineBuf), "%05lX %c %s",
                         absIdx, dirCh, entry.tag);
            }
            size_t lineLen = strlen(lineBuf);
            for (size_t p = 0; p < lineLen; p++) colors[p] = WHITE;
            for (uint8_t p = 0; p < 5; p++) colors[p] = DIM_GRAY;
            lcd.blitTextLineColored(MARGIN, y, LINE_W, LINE_HEIGHT,
                                    lineBuf, colors, BG_COLOR);
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

void TraceScreen::renderLegend(Harness::ICanvas& lcd)
{
    if (!m_needsRedraw) return;
    m_titleDrawn = true;

    static constexpr uint16_t LINE_W = Harness::Canvas::WIDTH - (2 * MARGIN);
    uint16_t y = MARGIN;

    // Title
    lcd.blitTextLine(MARGIN, y, LINE_W, 8, "Trace Legend", LABEL_COLOR, BG_COLOR);
    y += TITLE_H;
    lcd.drawHLine(MARGIN, y, LINE_W, LABEL_COLOR);
    y += 4;

    char lineBuf[42];
    uint16_t colors[42];

    for (uint8_t i = 0; i < LEGEND_ITEMS; i++) {
        const LegendRow& row = LEGEND_ROWS[i];
        uint16_t bg = (i == m_legendCursor) ? SEL_BG : BG_COLOR;

        if (row.boundaryBit != 0) {
            // Filterable boundary row: "L4 [*] Mot  Motor Drv"
            bool enabled = (m_filterMask & row.boundaryBit) != 0;
            snprintf(lineBuf, sizeof(lineBuf), "%s [%c] %s  %s",
                     row.layerTag, enabled ? '*' : ' ', row.abbrev, row.fullName);
        } else {
            // Info-only row (Svc): "   --- Svc  Services"
            snprintf(lineBuf, sizeof(lineBuf), "   --- %s  %s",
                     row.abbrev, row.fullName);
        }

        size_t len = strlen(lineBuf);
        for (size_t p = 0; p < len; p++) colors[p] = WHITE;

        if (row.boundaryBit != 0) {
            // Layer tag chars 0-1 in layer color
            for (uint8_t p = 0; p < 2 && p < len; p++) colors[p] = row.layerColor;
            // Abbreviation at chars 7-9 in boundary color
            for (uint8_t p = 7; p < 10 && p < len; p++) colors[p] = row.color;
        } else {
            // Abbreviation at chars 7-9 in boundary color
            for (uint8_t p = 7; p < 10 && p < len; p++) colors[p] = row.color;
        }

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

    while (y + LINE_HEIGHT <= Harness::Canvas::HEIGHT) {
        lcd.blitTextLine(MARGIN, y, LINE_W, LINE_HEIGHT, "", WHITE, BG_COLOR);
        y += LINE_HEIGHT;
    }
}

// =============================================================================
// GRAPH mode — timeline view
// =============================================================================

// Draw a compact layer color key: " L1 L2 L3 L4 L5 F" (each in its color)
static void drawLayerKey(Harness::ICanvas& lcd, uint16_t y) {
    static constexpr uint16_t LINE_W = Harness::Canvas::WIDTH - (2 * MARGIN);
    // Clear the line first
    lcd.blitTextLine(MARGIN, y, LINE_W, 8, "", WHITE, BG_COLOR);
    // Draw each label at calculated x positions (scale 1 = 6px/char)
    static constexpr struct { const char* label; uint16_t color; } KEY[] = {
        {"L1", COLOR_L1}, {"L2", COLOR_L2}, {"L3", COLOR_L3},
        {"L4", COLOR_L4}, {"L5", COLOR_L5}, {"F",  COLOR_F},
    };
    uint16_t x = MARGIN + 6;  // small indent
    for (uint8_t i = 0; i < 6; i++) {
        lcd.drawString(x, y, KEY[i].label, KEY[i].color, BG_COLOR, 1);
        x += static_cast<uint16_t>(strlen(KEY[i].label)) * 6 + 8;
    }
}

// Dim a color to ~25% brightness for lane background strips
static uint16_t dimColor(uint16_t c) {
    uint16_t r = (c >> 11) & 0x1F;
    uint16_t g = (c >> 5)  & 0x3F;
    uint16_t b =  c        & 0x1F;
    return ((r / 4) << 11) | ((g / 4) << 5) | (b / 4);
}

// Return a shade of the lane's hue based on which boundary is being drawn.
// Each lane has one color family; different boundaries get distinct shades.
static uint16_t laneBlockColor(uint8_t laneIdx, uint16_t boundary) {
    switch (laneIdx) {
        case 0: // L1 — cyan shades
            return 0x07FF;
        case 1: // L2 — green shades (3 boundaries: 1,2,0x400)
            switch (boundary) {
                case 1:     return 0x0540;  // dim green (transport echo)
                case 0x400: return 0x0660;  // medium green (telemetry)
                default:    return 0x07E0;  // bright green (dispatch)
            }
        case 2: // L3 — orange shades (6 boundaries: 2,4,8,16,32,0x200)
            switch (boundary) {
                case 2:     return 0xFE80;  // amber
                case 32:    return 0xFD80;  // light orange
                case 4:     return 0xFC80;  // orange
                case 8:     return 0xFB80;  // dark orange
                case 16:    return 0xFA80;  // red-orange
                case 0x200: return 0xF980;  // deep orange (flash cfg)
                default:    return 0xFC80;
            }
        case 3: // L4 — purple/magenta shades (5 boundaries: 4,8,64,0x100,0x200)
            switch (boundary) {
                case 4:     return 0xF81F;  // bright magenta (motor)
                case 8:     return 0xB01F;  // medium purple (encoder)
                case 0x100: return 0xD01F;  // light purple (LCD)
                case 0x200: return 0x901F;  // deep purple (flash)
                case 64:    return 0x801F;  // dark purple (SPI)
                default:    return 0xF81F;
            }
        case 4: // L5 — red shades (2 boundaries: 64,128)
            return (boundary == 64) ? 0xF800 : 0xC000;  // bright / dark
        case 5: // F — yellow shades (3 boundaries: 16,128,0x100)
            switch (boundary) {
                case 16:    return 0xFFE0;  // bright yellow (safety)
                case 0x100: return 0xDEE0;  // light gold (LCD from display task)
                case 128:   return 0xCE00;  // dim gold (mutex)
                default:    return 0xFFE0;
            }
        default: return 0x8410;
    }
}

// Map boundary to its representative layer color (for service mode / legend)
static uint16_t boundaryColor(uint16_t boundary) {
    switch (boundary) {
        case 1:     return COLOR_L1;      // transport → cyan
        case 2:     return COLOR_L2;      // protocol  → green
        case 4:     return COLOR_L4;      // motor drv → magenta
        case 8:     return COLOR_L4_ENC;  // encoder   → purple shade
        case 16:    return COLOR_F;       // safety    → yellow
        case 32:    return COLOR_L3;      // cmd sink  → orange
        case 64:    return COLOR_L5;      // SPI bus   → red
        case 128:   return COLOR_F_LOCK;  // mutex     → gold
        case 0x100: return COLOR_L4_LCD;  // LCD drv   → light purple
        case 0x200: return COLOR_L4_FLS;  // flash     → deep purple
        case 0x400: return COLOR_L2_TLM;  // telemetry → medium green
        default:    return 0x8410;
    }
}

void TraceScreen::renderGraph(Harness::ICanvas& lcd)
{
    size_t total = Trace::getTotal();
    size_t count = Trace::getCount();
    bool newData = (total != m_lastTotal);
    if (newData) {
        m_lastTotal = total;
    }

    // --- Layout (adaptive height for 8 boundary lanes) ---
    static constexpr uint16_t LINE_W = Harness::Canvas::WIDTH - (2 * MARGIN);
    static constexpr uint16_t LANES_Y = MARGIN + TITLE_H + 4;
    static constexpr uint16_t FOOTER_SPACE_G = 34;
    uint16_t availH = Harness::Canvas::HEIGHT - LANES_Y - FOOTER_SPACE_G;
    uint16_t laneH = availH / GRAPH_LANES;
    if (laneH > 40) laneH = 40;
    if (laneH < 16) laneH = 16;
    static constexpr uint16_t LANE_BAR_INSET = 1;
    uint16_t laneBarH = laneH - (2 * LANE_BAR_INSET);

    uint16_t timelineRight = Harness::Canvas::WIDTH - MARGIN;
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
        const LayerLane& ll = LAYER_LANES[lane];
        uint16_t ly = LANES_Y + (lane * laneH) + LANE_BAR_INSET;

        lcd.fillRect(TIMELINE_X, ly, timelineW, laneBarH, dimColor(ll.color));
        lcd.drawString(MARGIN, ly + ((laneBarH - 12) / 2),
                        ll.label, ll.color, BG_COLOR, 3);
    }

    // --- Separator lines between lanes ---
    for (uint8_t lane = 0; lane <= GRAPH_LANES; lane++) {
        uint16_t ly = LANES_Y + (lane * laneH);
        lcd.drawHLine(TIMELINE_X, ly, timelineW, DIM_GRAY);
    }

    // --- Compute block width from zoom level ---
    uint16_t blockW = 3;
    if (m_zoomLevel == 0 && count > 0) {
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
            if ((entry.boundary & m_filterMask) == 0) continue;

            uint16_t x = TIMELINE_X + static_cast<uint16_t>(i - startIdx) * blockW;
            if (x + blockDraw > timelineRight) break;

            // Color block by lane shade (each lane's hue, boundary picks shade)
            for (uint8_t lane = 0; lane < GRAPH_LANES; lane++) {
                if ((entry.boundary & LAYER_LANES[lane].boundaryMask) != 0) {
                    uint16_t ly = LANES_Y + (static_cast<uint16_t>(lane) * laneH) + LANE_BAR_INSET;
                    lcd.fillRect(x, ly, blockDraw, laneBarH,
                                 laneBlockColor(lane, entry.boundary));
                }
            }
        }
    }

    // --- Footer below lanes: key + axis + controls ---
    uint16_t FOOTER_Y = LANES_Y + (GRAPH_LANES * laneH) + 2;
    drawLayerKey(lcd, FOOTER_Y);
    {
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
        lcd.blitTextLine(MARGIN, FOOTER_Y + 10, LINE_W, 8,
                         axisBuf, LABEL_COLOR, BG_COLOR);
    }
    {
        char ctrlBuf[40];
        if (m_autoScroll) {
            snprintf(ctrlBuf, sizeof(ctrlBuf), " CTR:pause UD:zoom L:back R:svc");
        } else {
            snprintf(ctrlBuf, sizeof(ctrlBuf), " CTR:resume LR:pan UD:zoom");
        }
        lcd.blitTextLine(MARGIN, FOOTER_Y + 20, LINE_W, 8, ctrlBuf,
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


void TraceScreen::renderService(Harness::ICanvas& lcd)
{
    size_t total = Trace::getTotal();
    size_t count = Trace::getCount();
    bool newData = (total != m_lastTotal);
    if (newData) {
        m_lastTotal = total;
    }

    static constexpr uint16_t LINE_W = Harness::Canvas::WIDTH - (2 * MARGIN);

    bool redrawTitle = false;
    bool redrawGraph = false;

    if (!m_titleDrawn) {
        m_titleDrawn = true;
        m_lastLaneCount = 0;  // Force label redraw after screen clear
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

    // All services always shown (lanes resize to fit)
    Trace::Entry entry;
    uint8_t laneCount = SERVICE_TABLE_SIZE;

    // --- Adaptive lane height based on active service count ---
    static constexpr uint16_t LANES_Y = MARGIN + TITLE_H + 4;
    static constexpr uint16_t FOOTER_SPACE = 32;
    uint16_t availH = Harness::Canvas::HEIGHT - LANES_Y - FOOTER_SPACE;
    uint16_t laneH = availH / laneCount;
    if (laneH > 40) { laneH = 40; }
    if (laneH < 20) { laneH = 20; }

    uint16_t timelineRight = Harness::Canvas::WIDTH - MARGIN;
    uint16_t timelineW = timelineRight - TIMELINE_X;
    static constexpr uint16_t LANE_BAR_INSET = 1;
    uint16_t laneBarH = laneH - (2 * LANE_BAR_INSET);

    // --- Draw lane backgrounds + labels ---
    // Labels only drawn once (static layout since all services always shown)
    bool labelsNeeded = (m_lastLaneCount == 0);
    if (labelsNeeded) {
        m_lastLaneCount = laneCount;
        // Clear label column area
        lcd.fillRect(MARGIN, LANES_Y, TIMELINE_X - MARGIN, Harness::Canvas::HEIGHT - LANES_Y, BG_COLOR);
    }

    for (uint8_t i = 0; i < laneCount; i++) {
        const ServiceLaneInfo& sli = SERVICE_LANES_TABLE[i];
        uint16_t ly = LANES_Y + (i * laneH) + LANE_BAR_INSET;

        lcd.fillRect(TIMELINE_X, ly, timelineW, laneBarH, dimColor(sli.color));
        if (labelsNeeded) {
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
        svcToLane[SERVICE_LANES_TABLE[i].serviceId] = static_cast<int8_t>(i);
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
            lcd.fillRect(x, ly, blockDraw, laneBarH, boundaryColor(entry.boundary));
        }
    }

    // --- Footer at fixed bottom position ---
    {
        uint16_t footerY = Harness::Canvas::HEIGHT - FOOTER_SPACE;
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
        drawLayerKey(lcd, footerY);
        lcd.blitTextLine(MARGIN, footerY + 10, LINE_W, 8, axisBuf, LABEL_COLOR, BG_COLOR);

        char ctrlBuf[40];
        if (m_autoScroll) {
            snprintf(ctrlBuf, sizeof(ctrlBuf), " CTR:pause UD:zoom L:back");
        } else {
            snprintf(ctrlBuf, sizeof(ctrlBuf), " CTR:resume LR:pan UD:zoom");
        }
        lcd.blitTextLine(MARGIN, footerY + 20, LINE_W, 8, ctrlBuf,
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
    static constexpr size_t SCROLL_STEP = 5;
    size_t maxScroll = (m_matchCount > VISIBLE_LINES)
                     ? (m_matchCount - VISIBLE_LINES) : 0;

    switch (dir) {
        case JoyDirection::UP:
            if (!m_autoScroll && m_scrollOffset < maxScroll) {
                size_t step = SCROLL_STEP;
                if (m_scrollOffset + step > maxScroll) step = maxScroll - m_scrollOffset;
                m_scrollOffset += step;
                m_needsRedraw = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::DOWN:
            if (!m_autoScroll && m_scrollOffset > 0) {
                if (m_scrollOffset > SCROLL_STEP) {
                    m_scrollOffset -= SCROLL_STEP;
                } else {
                    m_scrollOffset = 0;
                }
                m_needsRedraw = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::CENTER:
            if (m_autoScroll) {
                m_autoScroll = false;
                m_pauseTotal = Trace::getTotal();
            } else {
                m_scrollOffset = 0;
                m_autoScroll = true;
            }
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
