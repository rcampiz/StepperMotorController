/**
 * @file trace_screen.cpp
 * @brief Trace monitor screen implementation
 *
 * A chrome screen: the ScreenManager paints the page, the title tab (which names the
 * view), the status line and the footer (CENTER hint). Each view draws inside the content
 * band [Theme::CONTENT_Y, Theme::CONTENT_BOTTOM]: a small-font header line with the feed
 * state, the view body, and one dim hint line for the controls.
 *
 * TRACE — scrollable text list with per-layer coloring
 *   Format: [index] [indent]Src>Dst method [detail] [value]
 *   Indentation by layer depth; a scrollbar shows where the window sits in the ring
 *   Pause: CENTER freezes the list, the header shows "PAUSED +N new"
 *
 * LEGEND — color key + boundary filter toggles
 *   One row per boundary (layer tag, abbreviation, name, ON/OFF); MAGENTA selection bar
 *   CENTER toggles the selected boundary filter
 *
 * TIMELINE — timeline view (Magic Trace / SystemView style)
 *   Horizontal lanes per layer, colored blocks show activity
 *   Time flows left to right, newest at right edge
 *
 * SERVICES — service-grouped timeline with boundary coloring
 *   One lane per service (Motion, Config, Safety, etc.)
 *   Blocks colored by boundary type showing driver chain visibility
 *
 * Repaints are diffs: text lines are blitted only when their text or colours change,
 * legend rows when their state changes, timeline blocks per slot when the entry under the
 * slot changes. Static parts are drawn once per activation / view switch.
 */

#include "ui/screens/trace_screen.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "L3_services/infra/trace/trace.hpp"
#include "ui/chrome/chrome.hpp"
#include "ui/theme.hpp"
#include <stdio.h>
#include <string.h>

namespace UI {

// =============================================================================
// Palette — all derived from the theme
// =============================================================================

/// Blend two RGB565 colours: `pctB` percent of b over a (palette shades stay theme-derived)
static constexpr uint16_t mix(uint16_t a, uint16_t b, uint16_t pctB) {
    return static_cast<uint16_t>(
        (((((a >> 11) & 0x1F) * (100 - pctB) + ((b >> 11) & 0x1F) * pctB) / 100) << 11) |
        (((((a >> 5) & 0x3F) * (100 - pctB) + ((b >> 5) & 0x3F) * pctB) / 100) << 5) |
        ((((a & 0x1F) * (100 - pctB) + (b & 0x1F) * pctB) / 100)));
}

static constexpr uint16_t BG    = Theme::BG;
static constexpr uint16_t TEXT  = Theme::TEXT;
static constexpr uint16_t DIM   = Theme::DIM;
static constexpr uint16_t BLACK = Theme::BLACK;

// Per-layer base colors (match arch_screen: L1 cyan, L2 green, L3 orange, L4 magenta,
// L5 violet, F yellow)
static constexpr uint16_t COLOR_L1 = Theme::CYAN;
static constexpr uint16_t COLOR_L2 = Theme::GREEN;
static constexpr uint16_t COLOR_L3 = Theme::ORANGE;
static constexpr uint16_t COLOR_L4 = Theme::MAGENTA;
static constexpr uint16_t COLOR_L5 = Theme::VIOLET;
static constexpr uint16_t COLOR_F  = Theme::YELLOW;

// Shade variants for legend entries sharing a layer
static constexpr uint16_t COLOR_L2_TLM = mix(COLOR_L2, BLACK, 40);       // telemetry
static constexpr uint16_t COLOR_L4_ENC = mix(COLOR_L4, TEXT, 45);        // encoder: light
static constexpr uint16_t COLOR_L4_LCD = mix(COLOR_L4, BLACK, 30);       // LCD: medium
static constexpr uint16_t COLOR_L4_FLS = mix(COLOR_L4, BLACK, 55);       // flash: deep
static constexpr uint16_t COLOR_F_LOCK = mix(COLOR_F, BLACK, 40);        // mutex: dim gold
static constexpr uint16_t COLOR_NONE   = mix(TEXT, BLACK, 50);           // legacy / unknown

// Zoom level → block width in pixels (0=auto, 1-7=fixed)
static constexpr uint8_t ZOOM_BLOCK_W[] = {0, 3, 5, 8, 12, 16, 20, 24};

// =============================================================================
// Layout — everything inside the content band
// =============================================================================

static constexpr uint16_t MARGIN   = Theme::MARGIN;
static constexpr uint16_t WIDTH    = Theme::WIDTH;
static constexpr uint16_t LINE_W   = WIDTH - 2 * MARGIN;               // 228 px, 38 chars
static constexpr uint16_t HEADER_Y = Theme::CONTENT_Y + 2;             // feed state line
static constexpr uint16_t RULE_Y   = HEADER_Y + 12;                    // rule under it
static constexpr uint16_t HINT_Y   = Theme::CONTENT_BOTTOM - 10;       // controls hint
static constexpr uint16_t TEXT_H   = 10;                               // small-font line box

// TRACE: list of lines with a scrollbar on the right, inside corner brackets
static constexpr uint16_t LIST_Y = RULE_Y + 6;
static constexpr uint16_t LIST_W = LINE_W - 6;                         // 37 chars
static constexpr uint16_t BAR_X  = MARGIN + LIST_W + 3;
static constexpr uint16_t BAR_W  = 2;

// LEGEND: compact medium-font rows, then the direction key
static constexpr uint16_t LEG_Y     = RULE_Y + 4;
static constexpr uint16_t ROW_PITCH = 15;                              // 14 px bar + separator
static constexpr uint16_t ROW_BAR   = 14;
static constexpr uint16_t STRIPE_W  = 3;

// TIMELINE / SERVICES: labelled lanes in a plot, an axis line and the layer key under it
static constexpr uint16_t PLOT_Y     = RULE_Y + 6;
static constexpr uint16_t PLOT_MAX_H = 198;
static constexpr uint16_t TIMELINE_X = MARGIN + 32;                    // after a 3-char label
static constexpr uint16_t TIMELINE_R = WIDTH - MARGIN;
static constexpr uint16_t TIMELINE_W = TIMELINE_R - TIMELINE_X;
static constexpr uint16_t AXIS_Y     = PLOT_Y + PLOT_MAX_H + 6;
static constexpr uint16_t KEY_Y      = AXIS_Y + 12;

// =============================================================================
// Boundary tables
// =============================================================================

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
        default:    return {"?",   TEXT,     "?",   TEXT,     0};
    }
}

// Legend table: boundary bit, abbreviation, full name, color, layer tag
struct LegendRow {
    uint16_t boundaryBit;
    const char* abbrev;
    const char* fullName;
    uint16_t color;
    const char* layerTag;    // "L1", "L4", "F", etc.
    uint16_t layerColor;     // Color for the tag
};

// Ordered L1→L2→L3→L4→L5→F, Svc info-only last
static constexpr LegendRow LEGEND_ROWS[] = {
    { 1,      "Xpt", "Transport",  COLOR_L1,     "L1", COLOR_L1 },
    { 2,      "Pro", "Protocol",   COLOR_L2,     "L2", COLOR_L2 },
    { 0x400,  "Tlm", "Telemetry",  COLOR_L2_TLM, "L2", COLOR_L2 },
    { 32,     "Cmd", "Cmd Sink",   COLOR_L3,     "L3", COLOR_L3 },
    { 4,      "Mot", "Motor Drv",  COLOR_L4,     "L4", COLOR_L4 },
    { 8,      "Enc", "Encoder",    COLOR_L4_ENC, "L4", COLOR_L4 },
    { 0x100,  "LCD", "LCD Drv",    COLOR_L4_LCD, "L4", COLOR_L4 },
    { 0x200,  "Fls", "Flash",      COLOR_L4_FLS, "L4", COLOR_L4 },
    { 64,     "SPI", "SPI Bus",    COLOR_L5,     "L5", COLOR_L5 },
    { 16,     "Saf", "Safety",     COLOR_F,      "F",  COLOR_F  },
    { 128,    "Mtx", "Mutex",      COLOR_F_LOCK, "F",  COLOR_F  },
    { 0,      "Svc", "Services",   COLOR_L3,     "",   COLOR_L3 },  // info-only
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

struct ServiceLaneInfo {
    uint8_t serviceId;
    const char* label;
    uint16_t color;
};

static constexpr ServiceLaneInfo SERVICE_LANES_TABLE[] = {
    { 1, "Mot", Theme::RED     },   // SVC_MOTION
    { 2, "Cfg", Theme::VIOLET  },   // SVC_CONFIG
    { 3, "Saf", Theme::YELLOW  },   // SVC_SAFETY
    { 4, "Enc", Theme::CYAN    },   // SVC_ENCODER
    { 5, "SId", Theme::MAGENTA },   // SVC_SYSID
    { 6, "Com", Theme::GREEN   },   // SVC_COMMS
    { 7, "UI",  Theme::TEXT    },   // SVC_UI
    { 8, "Dsp", Theme::ORANGE  },   // SVC_DISPATCH
};

static constexpr uint8_t SERVICE_TABLE_SIZE =
    sizeof(SERVICE_LANES_TABLE) / sizeof(SERVICE_LANES_TABLE[0]);

// Short filter name for the header
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

// Lane background: a dim tint of the lane colour on the black plot
static uint16_t trackColor(uint16_t c) { return mix(c, BLACK, 86); }

// Return a shade of the lane's hue based on which boundary is being drawn.
// Each lane has one color family; different boundaries get distinct shades.
static uint16_t laneBlockColor(uint8_t laneIdx, uint16_t boundary) {
    switch (laneIdx) {
        case 0: // L1
            return COLOR_L1;
        case 1: // L2 (3 boundaries: 1, 2, 0x400)
            switch (boundary) {
                case 1:     return mix(COLOR_L2, BLACK, 50);    // transport echo
                case 0x400: return COLOR_L2_TLM;                // telemetry
                default:    return COLOR_L2;                    // dispatch
            }
        case 2: // L3 (6 boundaries: 2, 4, 8, 16, 32, 0x200)
            switch (boundary) {
                case 2:     return mix(COLOR_L3, Theme::YELLOW, 60);  // amber
                case 32:    return mix(COLOR_L3, Theme::YELLOW, 30);  // light orange
                case 4:     return COLOR_L3;                          // orange
                case 8:     return mix(COLOR_L3, Theme::RED, 35);     // dark orange
                case 16:    return mix(COLOR_L3, Theme::RED, 70);     // red-orange
                case 0x200: return mix(COLOR_L3, BLACK, 45);          // deep (flash cfg)
                default:    return COLOR_L3;
            }
        case 3: // L4 (5 boundaries: 4, 8, 64, 0x100, 0x200)
            switch (boundary) {
                case 4:     return COLOR_L4;                          // motor
                case 8:     return COLOR_L4_ENC;                      // encoder
                case 0x100: return COLOR_L4_LCD;                      // LCD
                case 0x200: return COLOR_L4_FLS;                      // flash
                case 64:    return mix(COLOR_L4, Theme::VIOLET, 70);  // SPI
                default:    return COLOR_L4;
            }
        case 4: // L5 (2 boundaries: 64, 128)
            return (boundary == 64) ? COLOR_L5 : mix(COLOR_L5, BLACK, 45);
        case 5: // F (3 boundaries: 16, 128, 0x100)
            switch (boundary) {
                case 16:    return COLOR_F;                           // safety
                case 0x100: return mix(COLOR_F, TEXT, 45);            // LCD from display task
                case 128:   return COLOR_F_LOCK;                      // mutex
                default:    return COLOR_F;
            }
        default: return COLOR_NONE;
    }
}

// Map boundary to its representative layer color (for service mode / legend)
static uint16_t boundaryColor(uint16_t boundary) {
    switch (boundary) {
        case 1:     return COLOR_L1;      // transport
        case 2:     return COLOR_L2;      // protocol
        case 4:     return COLOR_L4;      // motor drv
        case 8:     return COLOR_L4_ENC;  // encoder
        case 16:    return COLOR_F;       // safety
        case 32:    return COLOR_L3;      // cmd sink
        case 64:    return COLOR_L5;      // SPI bus
        case 128:   return COLOR_F_LOCK;  // mutex
        case 0x100: return COLOR_L4_LCD;  // LCD drv
        case 0x200: return COLOR_L4_FLS;  // flash
        case 0x400: return COLOR_L2_TLM;  // telemetry
        default:    return COLOR_NONE;
    }
}

// =============================================================================
// Drawing helpers
// =============================================================================

/// A small-font line under construction: text plus one colour per character
struct ColorLine {
    static constexpr uint8_t CAP = 40;
    char text[CAP + 1];
    uint16_t colors[CAP];
    uint8_t n = 0;
    ColorLine() { text[0] = '\0'; }
    void put(const char* s, uint16_t c) {
        for (; *s != '\0' && n < CAP; s++) { text[n] = *s; colors[n] = c; n++; }
        text[n] = '\0';
    }
    /// Right-align `s` so it ends at column `width`
    void right(const char* s, uint16_t c, uint8_t width) {
        auto len = static_cast<uint8_t>(strlen(s));
        while (n + len < width && n < CAP) { text[n] = ' '; colors[n] = c; n++; }
        text[n] = '\0';
        put(s, c);
    }
};

static uint32_t hashLine(const char* text, const uint16_t* colors, uint16_t fg)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; text[i] != '\0'; i++) {
        h = (h ^ static_cast<uint8_t>(text[i])) * 16777619u;
        uint16_t c = (colors != nullptr) ? colors[i] : fg;
        h = (h ^ (c & 0xFF)) * 16777619u;
        h = (h ^ (c >> 8)) * 16777619u;
    }
    h = (h ^ fg) * 16777619u;
    return h | 1u;    // 0 means "not drawn"
}

void TraceScreen::cachedLine(Services::ICanvas& lcd, uint16_t x, uint16_t y, uint16_t w,
                             uint16_t h, const char* text, const uint16_t* colors,
                             uint16_t fg, uint32_t& cache)
{
    uint32_t key = hashLine(text, colors, fg);
    if (key == cache) return;
    cache = key;
    if (colors != nullptr) lcd.blitTextLineColored(x, y, w, h, text, colors, BG);
    else                   lcd.blitTextLine(x, y, w, h, text, fg, BG);
}

/// A medium-font text segment of a row
struct Seg { uint16_t x; const char* text; uint16_t fg; };

/// Paint the box [x0, x1) x [y, y + h) with `bg` and the segments (sorted by x) on it, every
/// pixel once: the gaps between segments are filled, the segments draw their own cells.
static void segRow(Services::ICanvas& lcd, uint16_t x0, uint16_t x1, uint16_t y, uint16_t h,
                   uint16_t padY, const Seg* segs, uint8_t n, uint16_t bg)
{
    const uint16_t ty = static_cast<uint16_t>(y + padY);
    lcd.fillRect(x0, y, static_cast<uint16_t>(x1 - x0), padY, bg);
    lcd.fillRect(x0, static_cast<uint16_t>(ty + Theme::MED_H), static_cast<uint16_t>(x1 - x0),
                 static_cast<uint16_t>(h - padY - Theme::MED_H), bg);
    uint16_t cx = x0;
    for (uint8_t i = 0; i < n; i++) {
        if (segs[i].text == nullptr || segs[i].text[0] == '\0' || segs[i].x < cx) continue;
        if (segs[i].x > cx) lcd.fillRect(cx, ty, static_cast<uint16_t>(segs[i].x - cx), Theme::MED_H, bg);
        lcd.drawString(segs[i].x, ty, segs[i].text, segs[i].fg, bg, Theme::FONT_MEDIUM);
        cx = static_cast<uint16_t>(segs[i].x + strlen(segs[i].text) * Theme::MED_W);
    }
    if (cx < x1) lcd.fillRect(cx, ty, static_cast<uint16_t>(x1 - cx), Theme::MED_H, bg);
}

/// The rule under the header line: a cyan lead-in, then the grid colour
static void drawRule(Services::ICanvas& lcd)
{
    lcd.fillRect(MARGIN, RULE_Y, 36, 1, Theme::CYAN);
    lcd.fillRect(MARGIN + 36, RULE_Y, LINE_W - 36, 1, Theme::GRID_MAJ);
}

/// Feed state at the left of a header line: LIVE (green) or PAUSED +N new (yellow)
static void putFeedState(ColorLine& line, bool live, size_t newCount)
{
    if (live) {
        line.put("LIVE", Theme::GREEN);
    } else {
        char buf[24];
        snprintf(buf, sizeof buf, "PAUSED +%u", static_cast<unsigned>(newCount));
        line.put(buf, Theme::YELLOW);
    }
}

/// Filter name at the right of a header line
static void putFilter(ColorLine& line, uint16_t mask)
{
    const char* name = getFilterName(mask);
    char buf[16];
    snprintf(buf, sizeof buf, "flt %s", name != nullptr ? name : "all");
    line.right(buf, DIM, 38);
    for (uint8_t i = static_cast<uint8_t>(line.n - strlen(buf) + 4); i < line.n; i++) {
        line.colors[i] = (name != nullptr) ? Theme::MAGENTA : Theme::CYAN;
    }
}

// =============================================================================
// Screen
// =============================================================================

const char* TraceScreen::title() const
{
    switch (m_mode) {
        case Mode::LEGEND:  return "Trace Legend";
        case Mode::GRAPH:   return "Trace Timeline";
        case Mode::SERVICE: return "Trace Services";
        default:            return "Trace";
    }
}

const char* TraceScreen::centerHint() const
{
    if (m_mode == Mode::LEGEND) return "TOGGLE";
    return m_autoScroll ? "PAUSE" : "RESUME";
}

void TraceScreen::onActivate()
{
    m_needsRedraw = true;
    m_full = true;
    m_dirty = true;
    m_scrollOffset = 0;
    m_autoScroll = true;
    m_lastTotal = 0;
    m_pauseTotal = 0;
    m_mode = Mode::TRACE;
}

void TraceScreen::switchMode(Mode m)
{
    m_mode = m;
    m_needsRedraw = true;    // new title tab: the manager clears the page and redraws the chrome
    m_full = true;
    m_dirty = true;
}

void TraceScreen::render(Services::ICanvas& lcd)
{
    if (m_full) {
        // The page was cleared: nothing of ours is on it
        m_headerDrawn = m_hintDrawn = m_axisDrawn = m_barDrawn = 0;
        memset(m_lineDrawn, 0, sizeof m_lineDrawn);
        memset(m_rowDrawn, 0xFF, sizeof m_rowDrawn);
        m_drawnBlockW = 0;
    }

    switch (m_mode) {
        case Mode::LEGEND:  renderLegend(lcd);         break;
        case Mode::GRAPH:   renderLanes(lcd, false);   break;
        case Mode::SERVICE: renderLanes(lcd, true);    break;
        default:            renderTrace(lcd);          break;
    }
    m_full = false;
    m_dirty = false;
}

// =============================================================================
// TRACE view — scrollable text list
// =============================================================================

void TraceScreen::renderTrace(Services::ICanvas& lcd)
{
    static constexpr uint16_t LIST_H = VISIBLE_LINES * LINE_HEIGHT;

    if (m_full) {
        drawRule(lcd);
        Chrome::drawCorners(lcd, MARGIN - 3, LIST_Y - 3, LINE_W + 6, LIST_H + 6, 6, Theme::GRID_MAJ);
    }

    size_t total = Trace::getTotal();
    size_t count = Trace::getCount();
    bool newData = (total != m_lastTotal);
    if (newData) {
        m_lastTotal = total;
    }

    bool redrawLines = m_full || m_dirty;
    if (newData && m_autoScroll) {
        m_scrollOffset = 0;
        redrawLines = true;
    }
    // Paused: the list stays frozen until the user scrolls

    if (redrawLines) {
        // --- Count filtered entries ---
        m_matchCount = 0;
        Trace::Entry tmpEntry;
        for (size_t i = 0; i < count; i++) {
            if (Trace::getEntry(i, tmpEntry) && entryMatchesFilter(tmpEntry, m_filterMask)) {
                m_matchCount++;
            }
        }

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
        uint16_t y = LIST_Y;

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
                for (const char* p = entry.tag; p != nullptr && *p != '\0'; p++) {
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
                for (size_t p = 0; p < lineLen; p++) colors[p] = TEXT;
                for (uint8_t p = 0; p < 5; p++) colors[p] = DIM;
                for (uint8_t p = srcStart; p < srcEnd && p < lineLen; p++) colors[p] = srcColor;
                if (srcEnd < lineLen) colors[srcEnd] = DIM;     // direction mark
                for (uint8_t p = dstStart; p < dstEnd && p < lineLen; p++) colors[p] = dstColor;
            } else {
                // --- Legacy trace ---
                char dirCh = (entry.dir == Trace::ENTRY) ? '>' : '<';
                const char* tag = (entry.tag != nullptr) ? entry.tag : "?";
                if (entry.arg0 != 0) {
                    snprintf(lineBuf, sizeof(lineBuf), "%05lX %c %s %lu",
                             absIdx, dirCh, tag,
                             static_cast<unsigned long>(entry.arg0));
                } else {
                    snprintf(lineBuf, sizeof(lineBuf), "%05lX %c %s",
                             absIdx, dirCh, tag);
                }
                size_t lineLen = strlen(lineBuf);
                for (size_t p = 0; p < lineLen; p++) colors[p] = TEXT;
                for (uint8_t p = 0; p < 5; p++) colors[p] = DIM;
                colors[6] = DIM;
            }

            cachedLine(lcd, MARGIN, y, LIST_W, LINE_HEIGHT, lineBuf, colors, TEXT,
                       m_lineDrawn[lineIdx]);
            lineIdx++;
            y += LINE_HEIGHT;
        }

        for (; lineIdx < VISIBLE_LINES; lineIdx++) {
            cachedLine(lcd, MARGIN, y, LIST_W, LINE_HEIGHT, "", nullptr, TEXT, m_lineDrawn[lineIdx]);
            y += LINE_HEIGHT;
        }

        // --- Scrollbar: where the window sits among the matching entries ---
        uint16_t thumbTop = 0, thumbLen = LIST_H;
        if (m_matchCount > VISIBLE_LINES) {
            thumbLen = static_cast<uint16_t>(LIST_H * VISIBLE_LINES / m_matchCount);
            if (thumbLen < 8) thumbLen = 8;
            size_t range = m_matchCount - VISIBLE_LINES;
            thumbTop = static_cast<uint16_t>((LIST_H - thumbLen) * skipMatches / range);
        }
        uint16_t thumbColor = m_autoScroll ? Theme::CYAN : Theme::MAGENTA;
        uint32_t barKey = (static_cast<uint32_t>(thumbTop) << 16) ^ (static_cast<uint32_t>(thumbLen) << 4)
                        ^ (m_autoScroll ? 1u : 2u);
        if (barKey != m_barDrawn) {
            m_barDrawn = barKey;
            if (thumbTop > 0) lcd.fillRect(BAR_X, LIST_Y, BAR_W, thumbTop, Theme::GRID);
            lcd.fillRect(BAR_X, static_cast<uint16_t>(LIST_Y + thumbTop), BAR_W, thumbLen, thumbColor);
            uint16_t below = static_cast<uint16_t>(LIST_H - thumbTop - thumbLen);
            if (below > 0) lcd.fillRect(BAR_X, static_cast<uint16_t>(LIST_Y + thumbTop + thumbLen),
                                        BAR_W, below, Theme::GRID);
        }
    }

    // --- Header: feed state, entries shown, filter ---
    {
        ColorLine line;
        putFeedState(line, m_autoScroll, total - m_pauseTotal);
        char buf[24];
        if (m_matchCount == count) snprintf(buf, sizeof buf, "  %u entries", static_cast<unsigned>(count));
        else snprintf(buf, sizeof buf, "  %u/%u match", static_cast<unsigned>(m_matchCount),
                      static_cast<unsigned>(count));
        line.put(buf, TEXT);
        putFilter(line, m_filterMask);
        cachedLine(lcd, MARGIN, HEADER_Y, LINE_W, TEXT_H, line.text, line.colors, TEXT, m_headerDrawn);
    }

    cachedLine(lcd, MARGIN, HINT_Y, LINE_W, Theme::SMALL_H,
               m_autoScroll ? "CENTER pause to scroll   RIGHT legend"
                            : "UP older   DOWN newer   RIGHT legend",
               nullptr, DIM, m_hintDrawn);
}

// =============================================================================
// LEGEND view — color key + filter toggles
// =============================================================================

void TraceScreen::renderLegend(Services::ICanvas& lcd)
{
    static constexpr uint16_t KEY_TOP = LEG_Y + LEGEND_ITEMS * ROW_PITCH + 4;
    static constexpr uint16_t KEY_H = 36;

    if (m_full) {
        drawRule(lcd);
        // Direction key in a bracketed box under the rows
        Chrome::drawCorners(lcd, MARGIN, KEY_TOP, LINE_W, KEY_H, 6, Theme::GRID_MAJ);
        static constexpr struct { const char* sym; const char* text; } KEY[] = {
            {">", "call into layer"}, {"<", "return from layer"}, {"~", "internal (same layer)"},
        };
        for (uint8_t i = 0; i < 3; i++) {
            ColorLine line;
            line.put(KEY[i].sym, Theme::CYAN);
            line.put("  ", TEXT);
            line.put(KEY[i].text, TEXT);
            lcd.blitTextLineColored(MARGIN + 10, static_cast<uint16_t>(KEY_TOP + 5 + i * 10),
                                    LINE_W - 20, Theme::SMALL_H, line.text, line.colors, BG);
        }
    }

    // --- Header: how many boundaries pass the filter ---
    {
        uint8_t on = 0;
        for (uint8_t i = 0; i < LEGEND_ITEMS; i++) {
            if (LEGEND_ROWS[i].boundaryBit != 0 && (m_filterMask & LEGEND_ROWS[i].boundaryBit) != 0) on++;
        }
        ColorLine line;
        line.put("BOUNDARY FILTER", DIM);
        char buf[16];
        snprintf(buf, sizeof buf, "%u/%u shown", static_cast<unsigned>(on),
                 static_cast<unsigned>(LEGEND_ITEMS - 1));
        line.right(buf, on == LEGEND_ITEMS - 1 ? Theme::CYAN : Theme::MAGENTA, 38);
        cachedLine(lcd, MARGIN, HEADER_Y, LINE_W, TEXT_H, line.text, line.colors, TEXT, m_headerDrawn);
    }

    // --- Rows: repaint the ones whose state changed ---
    for (uint8_t i = 0; i < LEGEND_ITEMS; i++) {
        const LegendRow& row = LEGEND_ROWS[i];
        bool selected = (i == m_legendCursor);
        bool enabled = (row.boundaryBit != 0) && (m_filterMask & row.boundaryBit) != 0;
        auto state = static_cast<uint8_t>((selected ? 1 : 0) | (enabled ? 2 : 0));
        if (state == m_rowDrawn[i]) continue;
        m_rowDrawn[i] = state;

        auto y = static_cast<uint16_t>(LEG_Y + i * ROW_PITCH);
        uint16_t bg = selected ? Theme::MAGENTA : BG;
        bool info = (row.boundaryBit == 0);

        // Boundary colour stripe at the left edge, the row bar, the separator under it
        lcd.fillRect(MARGIN, y, STRIPE_W, ROW_BAR, row.color);
        const char* value = info ? "---" : (enabled ? "ON" : "OFF");
        uint16_t valueColor = info ? DIM : (enabled ? Theme::GREEN : DIM);
        auto valueX = static_cast<uint16_t>(WIDTH - MARGIN - 6 - strlen(value) * Theme::MED_W);
        Seg segs[] = {
            { MARGIN + 7,  selected ? ">" : "",  TEXT },
            { MARGIN + 17, row.layerTag,         selected ? TEXT : row.layerColor },
            { MARGIN + 41, row.abbrev,           selected ? TEXT : ((enabled || info) ? row.color : DIM) },
            { MARGIN + 73, row.fullName,         selected ? TEXT : (enabled ? TEXT : DIM) },
            { valueX,      value,                selected ? TEXT : valueColor },
        };
        segRow(lcd, MARGIN + STRIPE_W, WIDTH - MARGIN, y, ROW_BAR, 1, segs,
               static_cast<uint8_t>(sizeof segs / sizeof segs[0]), bg);
        lcd.fillRect(MARGIN, static_cast<uint16_t>(y + ROW_BAR), LINE_W, 1, Theme::GRID);
    }

    cachedLine(lcd, MARGIN, HINT_Y, LINE_W, Theme::SMALL_H,
               "UP/DN select  LEFT trace  RIGHT graph", nullptr, DIM, m_hintDrawn);
}

// =============================================================================
// TIMELINE / SERVICES views — lanes of activity blocks
// =============================================================================

void TraceScreen::renderLanes(Services::ICanvas& lcd, bool service)
{
    const uint8_t laneCount = service ? SERVICE_TABLE_SIZE : GRAPH_LANES;
    auto laneH = static_cast<uint16_t>(PLOT_MAX_H / laneCount);
    if (laneH > 40) laneH = 40;
    const auto laneBarH = static_cast<uint16_t>(laneH - 1);          // 1 px black gap under each lane
    const auto plotH = static_cast<uint16_t>(laneH * laneCount);

    auto laneColor = [&](uint8_t lane) -> uint16_t {
        return service ? SERVICE_LANES_TABLE[lane].color : LAYER_LANES[lane].color;
    };

    size_t total = Trace::getTotal();
    size_t count = Trace::getCount();
    bool newData = (total != m_lastTotal);
    if (newData) {
        m_lastTotal = total;
    }

    // --- Static: rule, lane labels, plot brackets, layer key ---
    if (m_full) {
        drawRule(lcd);
        for (uint8_t lane = 0; lane < laneCount; lane++) {
            auto ly = static_cast<uint16_t>(PLOT_Y + lane * laneH);
            const char* label = service ? SERVICE_LANES_TABLE[lane].label : LAYER_LANES[lane].label;
            lcd.drawString(MARGIN, static_cast<uint16_t>(ly + (laneBarH - Theme::MED_H) / 2),
                           label, laneColor(lane), BG, Theme::FONT_MEDIUM);
        }
        Chrome::drawCorners(lcd, TIMELINE_X - 3, PLOT_Y - 3, TIMELINE_W + 6,
                            static_cast<uint16_t>(plotH + 6), 6, Theme::CYAN);
        static constexpr struct { const char* label; uint16_t color; } KEY[] = {
            {"L1", COLOR_L1}, {"L2", COLOR_L2}, {"L3", COLOR_L3},
            {"L4", COLOR_L4}, {"L5", COLOR_L5}, {"F",  COLOR_F},
        };
        ColorLine line;
        line.put(service ? "blocks by layer " : "layers ", DIM);
        for (uint8_t i = 0; i < 6; i++) {
            line.put(" ", DIM);
            line.put(KEY[i].label, KEY[i].color);
        }
        lcd.blitTextLineColored(MARGIN, KEY_Y, LINE_W, Theme::SMALL_H, line.text, line.colors, BG);
    }

    // --- Header: feed state, entries, filter (the services view plots every entry) ---
    {
        ColorLine line;
        putFeedState(line, m_autoScroll, total - m_pauseTotal);
        char buf[24];
        snprintf(buf, sizeof buf, "  %u entries", static_cast<unsigned>(count));
        line.put(buf, TEXT);
        if (!service) putFilter(line, m_filterMask);
        cachedLine(lcd, MARGIN, HEADER_Y, LINE_W, TEXT_H, line.text, line.colors, TEXT, m_headerDrawn);
    }

    cachedLine(lcd, MARGIN, HINT_Y, LINE_W, Theme::SMALL_H,
               !m_autoScroll ? "UP/DN zoom   LEFT/RIGHT pan"
                             : (service ? "UP/DN zoom   LEFT timeline"
                                        : "UP/DN zoom  LEFT legend  RIGHT svc"),
               nullptr, DIM, m_hintDrawn);

    // --- Throttle live redraws to ~5 Hz; paused: frozen until zoom or pan ---
    bool redraw = m_full || m_dirty;
    if (newData && m_autoScroll && ++m_frameSkip >= 4) {
        m_frameSkip = 0;
        redraw = true;
    }
    if (!redraw) return;

    // --- Compute block width from zoom level ---
    uint16_t blockW = 3;
    if (m_zoomLevel == 0 && count > 0) {
        blockW = static_cast<uint16_t>(TIMELINE_W / count);
        if (blockW < 3) blockW = 3;
        if (blockW > 8) blockW = 8;
    } else if (m_zoomLevel > 0 && m_zoomLevel <= ZOOM_LEVELS) {
        blockW = ZOOM_BLOCK_W[m_zoomLevel];
    }
    const auto blockDraw = static_cast<uint16_t>((blockW > 3) ? (blockW - 1) : blockW);
    auto visibleEntries = static_cast<uint16_t>(TIMELINE_W / blockW);
    if (visibleEntries > MAX_SLOTS) visibleEntries = MAX_SLOTS;

    // --- New block width: repaint the empty lanes, then every slot is empty ---
    if (blockW != m_drawnBlockW) {
        m_drawnBlockW = blockW;
        for (uint8_t lane = 0; lane < laneCount; lane++) {
            auto ly = static_cast<uint16_t>(PLOT_Y + lane * laneH);
            lcd.fillRect(TIMELINE_X, ly, TIMELINE_W, laneBarH, trackColor(laneColor(lane)));
            lcd.fillRect(TIMELINE_X, static_cast<uint16_t>(ly + laneBarH), TIMELINE_W, 1, BLACK);
        }
        memset(m_slotDrawn, 0, sizeof m_slotDrawn);
    }

    // --- Visible range ---
    size_t startIdx = 0;
    if (count > visibleEntries) {
        if (count > m_scrollOffset + visibleEntries) {
            startIdx = count - visibleEntries - m_scrollOffset;
        }
    }

    // --- Slots: what each block position shows, repainted where it changed ---
    // Key: timeline = the boundary (0 = nothing); services = boundary | service << 11
    Trace::Entry entry;
    for (uint16_t s = 0; s < visibleEntries; s++) {
        uint16_t key = 0;
        size_t i = startIdx + s;
        if (i < count && Trace::getEntry(i, entry)) {
            if (service) {
                if (entry.serviceId >= 1 && entry.serviceId <= SERVICE_TABLE_SIZE) {
                    key = static_cast<uint16_t>((entry.boundary & 0x07FF) | (entry.serviceId << 11));
                }
            } else if (entry.boundary != 0 && (entry.boundary & m_filterMask) != 0) {
                key = entry.boundary;
            }
        }
        uint16_t old = m_slotDrawn[s];
        if (key == old) continue;
        m_slotDrawn[s] = key;

        auto x = static_cast<uint16_t>(TIMELINE_X + s * blockW);
        for (uint8_t lane = 0; lane < laneCount; lane++) {
            bool wasHere, isHere;
            uint16_t color = trackColor(laneColor(lane));
            if (service) {
                uint8_t svc = SERVICE_LANES_TABLE[lane].serviceId;
                wasHere = (old >> 11) == svc;
                isHere = (key >> 11) == svc;
                if (isHere) color = boundaryColor(key & 0x07FF);
            } else {
                uint16_t mask = LAYER_LANES[lane].boundaryMask;
                wasHere = (old & mask) != 0;
                isHere = (key & mask) != 0;
                if (isHere) color = laneBlockColor(lane, key);
            }
            if (!wasHere && !isHere) continue;
            lcd.fillRect(x, static_cast<uint16_t>(PLOT_Y + lane * laneH), blockDraw, laneBarH, color);
        }
    }

    // --- Axis: the visible range of the ring and the zoom ---
    {
        size_t endIdx = startIdx + visibleEntries;
        if (endIdx > count) endIdx = count;
        char range[24], zoom[16];
        snprintf(range, sizeof range, "#%u..%u of %u", static_cast<unsigned>(startIdx),
                 static_cast<unsigned>(endIdx), static_cast<unsigned>(count));
        if (m_zoomLevel == 0) snprintf(zoom, sizeof zoom, "zoom auto");
        else snprintf(zoom, sizeof zoom, "zoom %u/%u", static_cast<unsigned>(m_zoomLevel),
                      static_cast<unsigned>(ZOOM_LEVELS));
        ColorLine line;
        line.put(range, TEXT);
        line.right(zoom, Theme::CYAN, 38);
        cachedLine(lcd, MARGIN, AXIS_Y, LINE_W, TEXT_H, line.text, line.colors, TEXT, m_axisDrawn);
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
                m_dirty = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::DOWN:
            if (!m_autoScroll && m_scrollOffset > 0) {
                if (m_scrollOffset > SCROLL_STEP) {
                    m_scrollOffset -= SCROLL_STEP;
                } else {
                    m_scrollOffset = 0;
                }
                m_dirty = true;
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
            m_dirty = true;
            return InputResult::HANDLED;

        case JoyDirection::RIGHT:
            switchMode(Mode::LEGEND);
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
                m_dirty = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::DOWN:
            if (m_legendCursor < LEGEND_ITEMS - 2) {
                m_legendCursor++;
                m_dirty = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::CENTER: {
            if (m_legendCursor < LEGEND_ITEMS - 1) {
                uint16_t bit = LEGEND_ROWS[m_legendCursor].boundaryBit;   // 0x100.. need 16 bits
                m_filterMask ^= bit;
                if (m_filterMask == 0) {
                    m_filterMask = bit;
                }
                m_scrollOffset = 0;
                m_autoScroll = true;
                m_dirty = true;
            }
            return InputResult::HANDLED;
        }

        case JoyDirection::RIGHT:
            // Forward to graph mode
            m_scrollOffset = 0;
            m_autoScroll = true;
            m_zoomLevel = 0;
            switchMode(Mode::GRAPH);
            return InputResult::HANDLED;

        case JoyDirection::LEFT:
            switchMode(Mode::TRACE);
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
                m_dirty = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::DOWN:
            // Zoom out (smaller blocks, more entries visible)
            if (m_zoomLevel > 0) {
                m_zoomLevel--;
                m_scrollOffset = 0;
                m_dirty = true;
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
            m_dirty = true;
            return InputResult::HANDLED;

        case JoyDirection::LEFT:
            if (!m_autoScroll) {
                // Pan left (older entries)
                m_scrollOffset += 8;
                m_dirty = true;
            } else {
                // Back to legend
                m_scrollOffset = 0;
                m_autoScroll = true;
                switchMode(Mode::LEGEND);
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
                m_dirty = true;
            } else {
                // Forward to service mode
                m_scrollOffset = 0;
                m_autoScroll = true;
                m_zoomLevel = 0;
                switchMode(Mode::SERVICE);
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
                m_dirty = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::DOWN:
            if (m_zoomLevel > 0) {
                m_zoomLevel--;
                m_scrollOffset = 0;
                m_dirty = true;
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
            m_dirty = true;
            return InputResult::HANDLED;

        case JoyDirection::LEFT:
            if (!m_autoScroll) {
                // Pan left (older entries)
                m_scrollOffset += 8;
                m_dirty = true;
            } else {
                // Back to graph
                m_scrollOffset = 0;
                m_autoScroll = true;
                switchMode(Mode::GRAPH);
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
                m_dirty = true;
            }
            return InputResult::HANDLED;

        default:
            return InputResult::UNHANDLED;
    }
}

} // namespace UI
