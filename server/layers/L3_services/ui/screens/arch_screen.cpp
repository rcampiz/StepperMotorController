/**
 * @file arch_screen.cpp
 * @brief Architecture visualization screen implementation
 *
 * Five modes: DIAGRAM (static layer overview), MATRIX (harnesses x tasks heatmap from the
 * harness meter), FLOW (SCPI command lifecycle through layers), TASKS (one task's layers),
 * HARNESS (every metered harness signal live: calls/s, bytes/s, busy %, crossing task).
 * FLOW and TASKS read the trace ring (debug builds); MATRIX and HARNESS read the harness
 * meter (harness/trace/harness_meter.hpp, every build), sampled every SAMPLE_US.
 */

#include "ui/screens/arch_screen.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "L3_services/infra/trace/trace.hpp"
#include "harness/trace/interface_trace.hpp"
#include "ui/chrome/chrome.hpp"
#include "ui/theme.hpp"
#include <stdio.h>
#include <string.h>

namespace UI {

static constexpr uint16_t MARGIN = Theme::MARGIN;
static constexpr uint16_t TOP = Theme::CONTENT_Y + 2;            // first content row
static constexpr uint16_t HINT_Y = Theme::CONTENT_BOTTOM - 10;   // one small-font hint line
static constexpr uint16_t TITLE_H = 14;
static constexpr uint16_t LINE_H = 12;
static constexpr uint16_t LINE_W = Services::Canvas::WIDTH - (2 * MARGIN);
static constexpr uint16_t WHITE   = Theme::TEXT;
static constexpr uint16_t BG      = Theme::BG;
static constexpr uint16_t CYAN    = Theme::CYAN;
static constexpr uint16_t GREEN   = Theme::GREEN;
static constexpr uint16_t MAGENTA = Theme::MAGENTA;
static constexpr uint16_t RED     = Theme::RED;
static constexpr uint16_t YELLOW  = Theme::YELLOW;
static constexpr uint16_t ORANGE  = Theme::ORANGE;
static constexpr uint16_t DIM     = Theme::DIM;
static constexpr uint16_t PAUSE_COLOR = Theme::YELLOW;

// Layer colors: L1=cyan, L2=green, L3=orange, L4=magenta, L5=red, F=yellow
static constexpr uint16_t LAYER_COLORS[] = {
    CYAN, GREEN, ORANGE, MAGENTA, Theme::VIOLET, YELLOW
};
static constexpr const char* LAYER_NAMES[] = {
    "L1 Transport", "L2 Protocol", "L3 Services",
    "L4 Drivers", "L5 Board/HAL", "F  Platform"
};
static constexpr const char* LAYER_CONTENTS[] = {
    "UART  RTT",
    "Parser  Telemetry",
    "Motion Safety Cfg",
    "Motor Enc LCD Flash",
    "GPIO SPI UART ADC",
    "Tasks orthogonal"
};

// Task columns for matrix mode
static constexpr const char* TASK_COLS[] = { "Mot", "Enc", "Com", "Dsp", "Tmr" };
static constexpr uint8_t TASK_IDS[] = { 1, 2, 3, 4, 5 };
static constexpr uint8_t NUM_TASKS = 5;
static constexpr uint8_t NUM_LAYERS = 6;

// Dim a color to ~25% brightness
static uint16_t dimColor(uint16_t c) {
    uint16_t r = (c >> 11) & 0x1F;
    uint16_t g = (c >> 5)  & 0x3F;
    uint16_t b =  c        & 0x1F;
    return ((r / 4) << 11) | ((g / 4) << 5) | (b / 4);
}

// Map boundary to layer index (0-5)
static uint8_t boundaryToLayer(uint16_t boundary) {
    if (boundary & Harness::ITrace::L1_L2_TRANSPORT) return 0;  // L1
    if (boundary & Harness::ITrace::L2_L3_DISPATCH)  return 1;  // L2
    if (boundary & Harness::ITrace::L2_TELEMETRY)    return 1;  // L2 (telemetry response)
    if (boundary & Harness::ITrace::L3_L4_MOTOR)     return 3;  // L4 (driver call)
    if (boundary & Harness::ITrace::L3_L4_ENCODER)   return 3;  // L4 (driver call)
    if (boundary & Harness::ITrace::L4_LCD)          return 3;  // L4 (LCD driver)
    if (boundary & Harness::ITrace::L4_FLASH)        return 3;  // L4 (flash driver)
    if (boundary & Harness::ITrace::L4_L5_SPI)       return 4;  // L5 (SPI hardware)
    if (boundary & Harness::ITrace::L5_F_LOCK)       return 5;  // F  (RTOS mutex)
    if (boundary & Harness::ITrace::L3_F_SAFETY)     return 2;  // L3 (service)
    if (boundary & Harness::ITrace::L3_CMD_SINK)     return 2;  // L3 (command queue)
    return 2;  // Default to L3 for entries with boundary=0
}

// --- harness meter views ------------------------------------------------------------------
// Harness colours: the upper layer's colour, the task sheet in the platform's yellow
static constexpr uint16_t HARNESS_COLORS[ArchScreen::HARNESSES] = {
    YELLOW, CYAN, GREEN, ORANGE, MAGENTA, Theme::VIOLET
};
// Matrix row labels, one per harness (H_TASKS_L3, H_L1_L2 .. H_L4_L5, X_L5_L1)
static constexpr const char* HARNESS_SHORT[ArchScreen::HARNESSES] = {
    "T>L3", "L1L2", "L2L3", "L3L4", "L4L5", "L5L1"
};
// Task slots (Harness::TaskId): letter and lane colour, as on the Task Monitor
static constexpr char TASK_LETTER[Harness::Meter::TASK_SLOTS] = { '?', 'M', 'E', 'C', 'D', 'T' };
static constexpr uint16_t TASK_COLOR[Harness::Meter::TASK_SLOTS] = {
    DIM, RED, CYAN, GREEN, YELLOW, Theme::VIOLET
};
static constexpr uint8_t TASK_NONE = 0xFE;     // RowDrawn::task: nobody crossed
static constexpr uint8_t NOT_DRAWN = 0xFF;
static constexpr uint32_t CELL_NOT_DRAWN = 0xFFFFFFFFu;

// Which harness each meter net belongs to: the nets are grouped by harness, in order, so
// the index steps whenever Meter::info(net).harness changes
static uint8_t harnessOf(uint8_t net) {
    uint8_t h = 0;
    for (uint8_t n = 1; n <= net && n < Harness::Meter::NET_COUNT; n++) {
        if (strcmp(Harness::Meter::info(n).harness, Harness::Meter::info(n - 1).harness) != 0) h++;
    }
    return (h < ArchScreen::HARNESSES) ? h : static_cast<uint8_t>(ArchScreen::HARNESSES - 1);
}

// A rate in at most 4 characters: 999, 9.9k, 999k, 9.9M, 999M
static void formatRate(char* out, size_t size, uint32_t v) {
    if (v < 1000u) {
        snprintf(out, size, "%u", static_cast<unsigned>(v));
    } else if (v < 10000u) {
        snprintf(out, size, "%u.%uk", static_cast<unsigned>(v / 1000u), static_cast<unsigned>((v / 100u) % 10u));
    } else if (v < 1000000u) {
        snprintf(out, size, "%uk", static_cast<unsigned>(v / 1000u));
    } else if (v < 10000000u) {
        snprintf(out, size, "%u.%uM", static_cast<unsigned>(v / 1000000u),
                 static_cast<unsigned>((v / 100000u) % 10u));
    } else {
        snprintf(out, size, "%uM", static_cast<unsigned>(v / 1000000u));
    }
}

void ArchScreen::onActivate()
{
    m_needsRedraw = true;
    m_titleDrawn = false;
    m_mode = Mode::DIAGRAM;
    m_paused = false;
    m_lastTotal = 0;
}

void ArchScreen::switchMode(Mode m)
{
    m_mode = m;
    m_titleDrawn = false;
    m_needsRedraw = true;
    if (m == Mode::MATRIX || m == Mode::HARNESS) sampleMeter(true);   // rates start from here
}

bool ArchScreen::meterDue() const
{
    return static_cast<uint32_t>(Harness::Meter::nowUs() - m_sampleUs) >= SAMPLE_US;
}

void ArchScreen::sampleMeter(bool baselineOnly)
{
    const uint32_t now = Harness::Meter::nowUs();
    const uint32_t elapsed = now - m_sampleUs;
    const bool rates = !baselineOnly && elapsed != 0u;
    if (rates) memset(m_taskRate, 0, sizeof m_taskRate);

    for (uint8_t n = 0; n < NETS; n++) {
        Harness::Meter::Counters c;
        Harness::Meter::read(n, c);
        if (rates) {
            const Harness::Meter::Counters& p = m_prev[n];
            NetRate& r = m_rate[n];
            // Counters only grow and wrap at 32 bits: unsigned deltas
            const uint64_t dCalls = static_cast<uint32_t>(c.calls - p.calls);
            const uint64_t dBytes = static_cast<uint32_t>(c.bytes - p.bytes);
            const uint64_t dBusy = static_cast<uint32_t>(c.busyUs - p.busyUs);
            uint64_t v = dCalls * 1000000u / elapsed;
            r.calls = (v > 0xFFFFFFFFu) ? 0xFFFFFFFFu : static_cast<uint32_t>(v);
            v = dBytes * 1000000u / elapsed;
            r.bytes = (v > 0xFFFFFFFFu) ? 0xFFFFFFFFu : static_cast<uint32_t>(v);
            v = dBusy * 1000u / elapsed;
            r.busyPm = (v > 9999u) ? 9999u : static_cast<uint16_t>(v);
            // The task that crossed most, and whether anyone else did
            uint32_t best = 0, others = 0;
            r.task = TASK_NONE;
            const uint8_t h = harnessOf(n);
            for (uint8_t t = 0; t < Harness::Meter::TASK_SLOTS; t++) {
                const uint32_t d = c.byTask[t] - p.byTask[t];
                if (d > best) { others += best; best = d; r.task = t; }
                else          { others += d; }
                if (t >= 1 && t <= MATRIX_TASKS) {
                    v = static_cast<uint64_t>(d) * 1000000u / elapsed;
                    m_taskRate[h][t - 1] += (v > 0x0FFFFFFFu) ? 0x0FFFFFFFu : static_cast<uint32_t>(v);
                }
            }
            r.mixed = (best != 0u && others != 0u);
        }
        m_prev[n] = c;
    }
    m_sampleUs = now;
    if (rates) m_haveRates = true;
    if (baselineOnly) m_haveRates = false;
}

void ArchScreen::render(Services::ICanvas& lcd)
{
    if (!m_titleDrawn) {
        lcd.fillRect(0, Theme::CONTENT_Y, Theme::WIDTH,
                     static_cast<uint16_t>(Theme::CONTENT_BOTTOM - Theme::CONTENT_Y + 1), BG);
        m_titleDrawn = true;
        for (uint8_t i = 0; i < NUM_LAYERS; i++) m_layerDrawn[i] = 0xFFFF;
        for (uint8_t n = 0; n < NETS; n++) {
            m_rowDrawn[n].calls[0] = m_rowDrawn[n].bytes[0] = m_rowDrawn[n].busy[0] = '\x01';
            m_rowDrawn[n].bar = NOT_DRAWN;
            m_rowDrawn[n].task = NOT_DRAWN;
        }
        for (uint8_t h = 0; h < HARNESSES; h++) {
            for (uint8_t t = 0; t < MATRIX_TASKS; t++) m_cellDrawn[h][t] = CELL_NOT_DRAWN;
        }
    }

    switch (m_mode) {
        case Mode::DIAGRAM: renderDiagram(lcd); break;
        case Mode::MATRIX:  renderMatrix(lcd);  break;
        case Mode::FLOW:    renderFlow(lcd);    break;
        case Mode::TASKS:   renderTasks(lcd);   break;
        case Mode::HARNESS: renderHarness(lcd); break;
    }
    m_needsRedraw = false;
}

// =============================================================================
// DIAGRAM mode — static architecture overview
// =============================================================================

void ArchScreen::renderDiagram(Services::ICanvas& lcd)
{
    // Six layer bands, host side at the top, hardware at the bottom, with the live boundary
    // traffic of each layer (trace entries in the ring) as a bar and a count on the right.
    static constexpr uint16_t BAND_H = 34;
    static constexpr uint16_t BAND_GAP = 4;
    static constexpr uint16_t BAR_X = Theme::WIDTH - 72;
    static constexpr uint16_t BAR_W = 56;
    static constexpr const char* LAYER_TAGS[] = { "L1", "L2", "L3", "L4", "L5", "F" };
    static constexpr const char* LAYER_SHORT[] = {
        "Transport", "Protocol", "Services", "Drivers", "Board", "Platform" };

    uint16_t y = TOP;
    if (m_needsRedraw) {
        lcd.blitTextLine(MARGIN, y, LINE_W, 8, "LAYERS   live boundary traffic", DIM, BG);
        y += TITLE_H;
        for (uint8_t i = 0; i < NUM_LAYERS; i++) {
            uint16_t color = LAYER_COLORS[i];
            auto by = static_cast<uint16_t>(y + i * (BAND_H + BAND_GAP));
            lcd.fillRect(MARGIN, by, LINE_W, BAND_H, Theme::PANEL);
            lcd.fillRect(MARGIN, by, 4, BAND_H, color);
            lcd.drawString(static_cast<uint16_t>(MARGIN + 10), static_cast<uint16_t>(by + 3), LAYER_TAGS[i],
                           color, Theme::PANEL, Theme::FONT_LARGE);
            lcd.drawString(static_cast<uint16_t>(MARGIN + 44), static_cast<uint16_t>(by + 3), LAYER_SHORT[i],
                           WHITE, Theme::PANEL, Theme::FONT_MEDIUM);
            lcd.blitTextLine(static_cast<uint16_t>(MARGIN + 44), static_cast<uint16_t>(by + 20), 112, 8,
                             LAYER_CONTENTS[i], DIM, Theme::PANEL);
            lcd.fillRect(BAR_X, static_cast<uint16_t>(by + 8), BAR_W, 8, Theme::BLACK);
            // Arrow to the next band (not after L5: F is orthogonal, not below)
            if (i < 4) {
                auto ay = static_cast<uint16_t>(by + BAND_H);
                lcd.fillRect(static_cast<uint16_t>(MARGIN + 18), ay, 1, BAND_GAP, CYAN);
            }
            if (i == 4) {
                auto ay = static_cast<uint16_t>(by + BAND_H);
                lcd.fillRect(MARGIN, static_cast<uint16_t>(ay + 1), LINE_W, 1, DIM);   // orthogonal rule
            }
        }
        Chrome::hintLine(lcd, HINT_Y, "RIGHT: matrix, flow, tasks, harness", DIM);
    }

    // Live counts: throttled like the other modes
    size_t total = Trace::getTotal();
    size_t count = Trace::getCount();
    bool newData = (total != m_lastTotal);
    if (newData) m_lastTotal = total;
    if (!m_needsRedraw && (!newData || m_paused)) return;
    static uint8_t frameSkip = 0;
    if (!m_needsRedraw && ++frameSkip < 4) return;
    frameSkip = 0;

    uint16_t layerCount[NUM_LAYERS] = {};
    Trace::Entry entry;
    size_t scan = count < 200 ? count : 200;
    for (size_t ri = 0; ri < scan; ri++) {
        if (!Trace::getEntry(count - 1 - ri, entry)) continue;
        uint8_t layer = boundaryToLayer(entry.boundary);
        if (layer < NUM_LAYERS) layerCount[layer]++;
    }
    y = static_cast<uint16_t>(TOP + TITLE_H);
    char buf[8];
    for (uint8_t i = 0; i < NUM_LAYERS; i++) {
        if (layerCount[i] == m_layerDrawn[i]) continue;
        m_layerDrawn[i] = layerCount[i];
        auto by = static_cast<uint16_t>(y + i * (BAND_H + BAND_GAP));
        auto w = static_cast<uint16_t>(layerCount[i] * BAR_W / (scan ? scan : 1));
        if (layerCount[i] > 0 && w == 0) w = 1;
        if (w > 0) lcd.fillRect(BAR_X, static_cast<uint16_t>(by + 8), w, 8, LAYER_COLORS[i]);
        if (w < BAR_W) lcd.fillRect(static_cast<uint16_t>(BAR_X + w), static_cast<uint16_t>(by + 8),
                                    static_cast<uint16_t>(BAR_W - w), 8, Theme::BLACK);
        snprintf(buf, sizeof buf, "%u", static_cast<unsigned>(layerCount[i]));
        lcd.blitTextLine(BAR_X, static_cast<uint16_t>(by + 20), BAR_W, 8, buf, LAYER_COLORS[i], Theme::PANEL);
    }
}

// =============================================================================
// MATRIX mode — live harnesses x tasks heatmap (harness meter)
// =============================================================================

void ArchScreen::renderMatrix(Services::ICanvas& lcd)
{
    const bool sampled = !m_paused && meterDue();
    if (sampled) sampleMeter(false);
    if (!m_needsRedraw && !sampled) return;

    uint16_t y = TOP;
    char buf[40];

    // Layout constants
    static constexpr uint16_t LABEL_W = 30;   // Row label width
    static constexpr uint16_t CELL_W = 38;    // Cell width
    static constexpr uint16_t CELL_H = 28;    // Cell height
    static constexpr uint16_t HDR_H = 12;     // Column header height
    static constexpr uint16_t GRID_X = MARGIN + LABEL_W + 2;
    static constexpr uint16_t GRID_Y = TOP + TITLE_H + 4 + HDR_H;

    if (m_needsRedraw) {
        // Title
        if (m_paused) {
            lcd.blitTextLine(MARGIN, y, LINE_W, 8, "PAUSED  harness x task", PAUSE_COLOR, BG);
        } else {
            lcd.blitTextLine(MARGIN, y, LINE_W, 8, "Harness x Tasks  calls/s", CYAN, BG);
        }
        y += TITLE_H;
        lcd.drawHLine(MARGIN, y, LINE_W, CYAN);
        y += 4;

        // Column headers
        for (uint8_t c = 0; c < NUM_TASKS; c++) {
            auto cx = static_cast<uint16_t>(GRID_X + c * CELL_W);
            lcd.blitTextLine(cx, y, CELL_W, HDR_H, TASK_COLS[c], TASK_COLOR[TASK_IDS[c]], BG);
        }
        // Row labels: one per harness, in its colour
        for (uint8_t r = 0; r < HARNESSES; r++) {
            lcd.blitTextLine(MARGIN, static_cast<uint16_t>(GRID_Y + r * CELL_H + 8), LABEL_W, 8,
                             HARNESS_SHORT[r], HARNESS_COLORS[r], BG);   // on the counts' line
        }

        // Legend
        y = static_cast<uint16_t>(GRID_Y + HARNESSES * CELL_H + 4);
        lcd.drawHLine(MARGIN, y, LINE_W, DIM);
        y += 4;
        lcd.blitTextLine(MARGIN, y, LINE_W, 8, " crossings/s by the task", DIM, BG);
        y += LINE_H;
        lcd.blitTextLine(MARGIN, y, LINE_W, 8, " bright>=100  dim<10  . none", DIM, BG);

        Chrome::hintLine(lcd, HINT_Y, m_paused ? "PAUSED   CENTER resume   RIGHT next"
                                               : "CENTER pause   RIGHT next view", m_paused ? PAUSE_COLOR : DIM);
    }

    // Cells: repainted only when the rate they show changed
    for (uint8_t r = 0; r < HARNESSES; r++) {
        const uint16_t color = HARNESS_COLORS[r];
        auto cy = static_cast<uint16_t>(GRID_Y + r * CELL_H);
        for (uint8_t c = 0; c < NUM_TASKS; c++) {
            const uint32_t rate = m_haveRates ? m_taskRate[r][c] : 0u;
            if (rate == m_cellDrawn[r][c]) continue;
            m_cellDrawn[r][c] = rate;
            auto cx = static_cast<uint16_t>(GRID_X + c * CELL_W);

            if (rate == 0) {
                // Empty cell: dark outline
                lcd.fillRect(cx + 1, cy + 1, CELL_W - 2, CELL_H - 2, BG);
                lcd.drawRect(cx, cy, CELL_W, CELL_H, DIM);
                lcd.blitTextLine(cx + 2, cy + 8, CELL_W - 4, 8, ".", DIM, BG);
                continue;
            }
            // Filled cell: brightness by rate
            uint16_t fillColor;
            uint16_t textColor = BG;
            if (rate >= 100) {
                fillColor = color;                // Full brightness
            } else if (rate >= 10) {
                fillColor = dimColor(color);      // Medium: 25%, then half way to white
                uint16_t rr = (fillColor >> 11) & 0x1F;
                uint16_t gg = (fillColor >> 5)  & 0x3F;
                uint16_t bb =  fillColor        & 0x1F;
                rr = rr + (0x1F - rr) / 2;
                gg = gg + (0x3F - gg) / 2;
                bb = bb + (0x1F - bb) / 2;
                fillColor = static_cast<uint16_t>((rr << 11) | (gg << 5) | bb);
            } else {
                fillColor = dimColor(color);      // Dim (25%): the count in the harness colour
                textColor = color;
            }
            lcd.fillRect(cx + 1, cy + 1, CELL_W - 2, CELL_H - 2, fillColor);
            lcd.drawRect(cx, cy, CELL_W, CELL_H, color);
            formatRate(buf, sizeof buf, rate);
            lcd.blitTextLine(cx + 2, cy + 8, CELL_W - 4, 8, buf, textColor, fillColor);
        }
    }
}

// =============================================================================
// HARNESS mode — every metered harness signal, live
// =============================================================================

void ArchScreen::renderHarness(Services::ICanvas& lcd)
{
    // Columns: harness tick, signal, calls/s, bytes/s, busy %, busy bar, crossing task
    static constexpr uint16_t ROW_H = 11;
    static constexpr uint16_t HEAD_Y = TOP;
    static constexpr uint16_t ROWS_Y = TOP + 13;
    static constexpr uint16_t TICK_X = MARGIN, TICK_W = 2;
    static constexpr uint16_t NAME_X = MARGIN + 6, NAME_W = 60;
    static constexpr uint16_t CALLS_X = 72, CALLS_W = 30;
    static constexpr uint16_t BYTES_X = 104, BYTES_W = 30;
    static constexpr uint16_t BUSY_X = 140, BUSY_W = 24;
    static constexpr uint16_t BAR_X = 168, BAR_W = 42, BAR_DY = 3, BAR_H = 5;
    static constexpr uint16_t TASK_X = 216, TASK_W = 12;
    static constexpr uint16_t TEXT_DY = 2;   // small font inside an 11 px row

    const bool sampled = !m_paused && meterDue();
    if (sampled) sampleMeter(false);
    if (!m_needsRedraw && !sampled) return;

    char buf[8];

    if (m_needsRedraw) {
        // Column heads: the view's name where the signal names go
        lcd.blitTextLine(MARGIN, HEAD_Y, static_cast<uint16_t>(CALLS_X - MARGIN), 8,
                         m_paused ? "PAUSED" : "HARNESS", m_paused ? PAUSE_COLOR : CYAN, BG);
        lcd.blitTextLine(CALLS_X, HEAD_Y, CALLS_W, 8, "  N/s", DIM, BG);
        lcd.blitTextLine(BYTES_X, HEAD_Y, BYTES_W, 8, "  B/s", DIM, BG);
        lcd.blitTextLine(BUSY_X, HEAD_Y, BUSY_W, 8, "busy", DIM, BG);
        lcd.blitTextLine(BAR_X, HEAD_Y, BAR_W, 8, " 0.5s", DIM, BG);
        lcd.blitTextLine(TASK_X, HEAD_Y, TASK_W, 8, "T", DIM, BG);
        lcd.drawHLine(MARGIN, static_cast<uint16_t>(HEAD_Y + 10), LINE_W, Theme::GRID_MAJ);

        // Harness heads and signal names
        uint16_t y = ROWS_Y;
        for (uint8_t n = 0; n < NETS; n++) {
            const Harness::Meter::NetInfo& info = Harness::Meter::info(n);
            const uint8_t h = harnessOf(n);
            const uint16_t color = HARNESS_COLORS[h];
            if (n == 0 || h != harnessOf(static_cast<uint8_t>(n - 1))) {
                const auto len = static_cast<uint16_t>(strlen(info.harness));
                const auto textW = static_cast<uint16_t>(len * Theme::SMALL_W);
                lcd.blitTextLine(MARGIN, static_cast<uint16_t>(y + TEXT_DY), textW, 8, info.harness, color, BG);
                const auto ruleX = static_cast<uint16_t>(MARGIN + textW + 4);
                lcd.drawHLine(ruleX, static_cast<uint16_t>(y + TEXT_DY + 3),
                              static_cast<uint16_t>(MARGIN + LINE_W - ruleX), dimColor(color));
                y += ROW_H;
            }
            lcd.fillRect(TICK_X, static_cast<uint16_t>(y + 1), TICK_W, static_cast<uint16_t>(ROW_H - 2), color);
            lcd.blitTextLine(NAME_X, static_cast<uint16_t>(y + TEXT_DY), NAME_W, 8, info.signal, WHITE, BG);
            y += ROW_H;
        }

        Chrome::hintLine(lcd, HINT_Y, m_paused ? "PAUSED   CENTER resume   RIGHT next"
                                               : "CENTER pause   RIGHT next view", m_paused ? PAUSE_COLOR : DIM);
    }

    // Live cells: each repainted only when its text (or bar length) changed
    uint16_t y = ROWS_Y;
    for (uint8_t n = 0; n < NETS; n++) {
        const uint8_t h = harnessOf(n);
        if (n == 0 || h != harnessOf(static_cast<uint8_t>(n - 1))) y += ROW_H;   // harness head
        const auto ty = static_cast<uint16_t>(y + TEXT_DY);
        const NetRate& r = m_rate[n];
        RowDrawn& d = m_rowDrawn[n];
        const bool live = m_haveRates;

        // calls/s
        char val[8];
        if (live && r.calls != 0u) formatRate(val, sizeof val, r.calls); else snprintf(val, sizeof val, "-");
        snprintf(buf, sizeof buf, "%5s", val);
        if (strcmp(buf, d.calls) != 0) {
            memcpy(d.calls, buf, sizeof d.calls);
            lcd.blitTextLine(CALLS_X, ty, CALLS_W, 8, buf, (live && r.calls != 0u) ? WHITE : DIM, BG);
        }
        // bytes/s
        if (live && r.bytes != 0u) formatRate(val, sizeof val, r.bytes); else snprintf(val, sizeof val, "-");
        snprintf(buf, sizeof buf, "%5s", val);
        if (strcmp(buf, d.bytes) != 0) {
            memcpy(d.bytes, buf, sizeof d.bytes);
            lcd.blitTextLine(BYTES_X, ty, BYTES_W, 8, buf, (live && r.bytes != 0u) ? Theme::VIOLET : DIM, BG);
        }
        // busy %: one decimal under 10 %
        const uint16_t pm = live ? r.busyPm : 0u;
        if (pm == 0u) {
            snprintf(val, sizeof val, "-");
        } else if (pm < 100u) {
            snprintf(val, sizeof val, "%u.%u%%", static_cast<unsigned>(pm / 10u), static_cast<unsigned>(pm % 10u));
        } else {
            snprintf(val, sizeof val, "%u%%", static_cast<unsigned>((pm > 9990u ? 9990u : pm) / 10u));
        }
        snprintf(buf, sizeof buf, "%4s", val);
        if (strcmp(buf, d.busy) != 0) {
            memcpy(d.busy, buf, sizeof d.busy);
            const uint16_t busyColor = (pm == 0u) ? DIM : (pm >= 900u) ? RED : (pm >= 500u) ? ORANGE : CYAN;
            lcd.blitTextLine(BUSY_X, ty, BUSY_W, 8, buf, busyColor, BG);
        }
        // busy bar, in the harness colour on a dark track
        auto w = static_cast<uint16_t>((pm >= 1000u ? 1000u : pm) * BAR_W / 1000u);
        if (pm != 0u && w == 0u) w = 1;
        if (w != d.bar) {
            const auto by = static_cast<uint16_t>(y + BAR_DY);
            if (d.bar == NOT_DRAWN) {
                if (w > 0u) lcd.fillRect(BAR_X, by, w, BAR_H, HARNESS_COLORS[h]);
                if (w < BAR_W) lcd.fillRect(static_cast<uint16_t>(BAR_X + w), by,
                                            static_cast<uint16_t>(BAR_W - w), BAR_H, Theme::PANEL);
            } else if (w > d.bar) {
                lcd.fillRect(static_cast<uint16_t>(BAR_X + d.bar), by, static_cast<uint16_t>(w - d.bar), BAR_H,
                             HARNESS_COLORS[h]);
            } else {
                lcd.fillRect(static_cast<uint16_t>(BAR_X + w), by, static_cast<uint16_t>(d.bar - w), BAR_H,
                             Theme::PANEL);
            }
            d.bar = static_cast<uint8_t>(w);
        }
        // the task that crossed most: its letter in its lane colour, '+' when others did too
        const uint8_t task = (live && r.task < Harness::Meter::TASK_SLOTS)
                                 ? static_cast<uint8_t>(r.task | (r.mixed ? 0x80u : 0u)) : TASK_NONE;
        if (task != d.task) {
            d.task = task;
            if (task == TASK_NONE) {
                lcd.blitTextLine(TASK_X, ty, TASK_W, 8, "", DIM, BG);
            } else {
                const uint8_t slot = static_cast<uint8_t>(task & 0x7Fu);
                const char text[3] = { TASK_LETTER[slot], (task & 0x80u) ? '+' : ' ', '\0' };
                const uint16_t colors[2] = { TASK_COLOR[slot], DIM };
                lcd.blitTextLineColored(TASK_X, ty, TASK_W, 8, text, colors, BG);
            }
        }
        y += ROW_H;
    }
}

// =============================================================================
// FLOW mode — SCPI command lifecycle
// =============================================================================

void ArchScreen::renderFlow(Services::ICanvas& lcd)
{
    size_t total = Trace::getTotal();
    size_t count = Trace::getCount();
    bool newData = (total != m_lastTotal);
    if (newData) m_lastTotal = total;

    if (!m_needsRedraw && !newData) return;
    if (!m_needsRedraw && m_paused) return;

    // Throttle live updates
    if (!m_needsRedraw && newData && !m_paused) {
        static uint8_t frameSkip = 0;
        if (++frameSkip < 8) return;
        frameSkip = 0;
    }

    uint16_t y = TOP;

    // Title
    lcd.blitTextLine(MARGIN, y, LINE_W, 8, "SCPI Flow", CYAN, BG);
    y += TITLE_H;
    lcd.drawHLine(MARGIN, y, LINE_W, CYAN);
    y += 4;

    // --- Scan trace entries — fully data-driven ---
    // Both per-layer info and per-boundary info come from actual trace
    // entries.  No static interface name assumptions.
    struct LayerInfo {
        uint8_t taskId;                  // RTOS task that executed (0=none)
        const char* method;              // method name from trace (nullptr=none)
        char detail[Trace::DETAIL_SIZE]; // detail string from trace
        bool active;                     // has trace entries
    };
    LayerInfo layerInfo[NUM_LAYERS] = {};

    // Boundary crossing info — derived from trace, not a static table.
    // Display boundaries: [0]=L1-L2, [1]=L2-L3, [2]=L3-L4, [3]=L4-L5, [4]=L5-F
    struct BoundaryData {
        bool crossed;          // was this boundary actually used?
        const char* iface;     // interface name (derived from boundary type)
        const char* method;    // actual method called
    };
    BoundaryData bndData[5] = {};

    char lastCmd[20] = "(none)";

    // Two-phase scan: find the current SCPI command, then collect data
    // filtered to that command's RTOS task.
    Trace::Entry entry;
    size_t cmdRi = SIZE_MAX;
    uint8_t cmdTaskId = 0;

    // Phase 1: Find the most recent L2_L3_DISPATCH = current command
    for (size_t ri = 0; ri < count; ri++) {
        size_t i = count - 1 - ri;
        if (!Trace::getEntry(i, entry)) continue;
        if (entry.boundary & Harness::ITrace::L2_L3_DISPATCH) {
            strncpy(lastCmd, entry.detail, sizeof(lastCmd) - 1);
            lastCmd[sizeof(lastCmd) - 1] = '\0';
            cmdRi = ri;
            cmdTaskId = entry.taskId;
            break;
        }
    }

    // Phase 2: Collect per-layer AND per-boundary info from trace data
    if (cmdRi != SIZE_MAX) {
        for (size_t ri = 0; ri < count && ri <= cmdRi + 12; ri++) {
            size_t i = count - 1 - ri;
            if (!Trace::getEntry(i, entry)) continue;
            if (entry.taskId != cmdTaskId) continue;

            // Fill per-layer info
            uint8_t layer = boundaryToLayer(entry.boundary);
            if (layer < NUM_LAYERS && !layerInfo[layer].active) {
                layerInfo[layer].taskId = entry.taskId;
                layerInfo[layer].method = entry.method;
                snprintf(layerInfo[layer].detail, Trace::DETAIL_SIZE, "%s", entry.detail);   // F13
                layerInfo[layer].active = true;
            }

            // Fill per-boundary info (map trace boundary → display boundary)
            if ((entry.boundary & Harness::ITrace::L1_L2_TRANSPORT) && !bndData[0].crossed) {
                bndData[0] = { true, "ITransport", entry.method };
            }
            if ((entry.boundary & Harness::ITrace::L2_L3_DISPATCH) && !bndData[1].crossed) {
                bndData[1] = { true, "ICommandDispatcher", entry.method };
            }
            if ((entry.boundary & Harness::ITrace::L3_L4_MOTOR) && !bndData[2].crossed) {
                bndData[2] = { true, "IMotorDriver", entry.method };
            }
            if ((entry.boundary & Harness::ITrace::L3_L4_ENCODER) && !bndData[2].crossed) {
                bndData[2] = { true, "IEncoder", entry.method };
            }
            if ((entry.boundary & Harness::ITrace::L4_L5_SPI) && !bndData[3].crossed) {
                bndData[3] = { true, "ISPIBus", entry.method };
            }
            if ((entry.boundary & Harness::ITrace::L5_F_LOCK) && !bndData[4].crossed) {
                bndData[4] = { true, "ILock", entry.method };
            }
        }
    }

    // Canonical SCPI path: L1→L2→L3→L4→L5 (always shown)
    // F_platform shown separately as orthogonal dependency
    static constexpr uint8_t SHOW_LAYERS[] = { 0, 1, 2, 3, 4 };
    static constexpr uint8_t SHOW_COUNT = 5;

    // Show last command
    char buf[40];
    snprintf(buf, sizeof(buf), "Last: %s", lastCmd);
    lcd.blitTextLine(MARGIN, y, LINE_W, LINE_H, buf, WHITE, BG);
    y += LINE_H + 2;

    // Draw the flow diagram
    static constexpr uint16_t FLOW_X = 20;     // Left margin for layer labels
    static constexpr uint16_t ARROW_X = 14;    // Vertical line x
    static constexpr uint16_t TASK_X = 150;    // Task label x
    // Compact layout to fit L1-L5 + F_platform in 320px
    static constexpr uint16_t LAYER_H = 16;    // Layer name + method
    static constexpr uint16_t ARROW_GAP = 20;  // Arrow + interface name + data desc
    static constexpr uint16_t STEP_H = LAYER_H + ARROW_GAP;
    static constexpr uint16_t F_SECTION_H = ARROW_GAP + LAYER_H; // L5→F boundary + F box

    // Clear fixed flow area (5 layers + arrows + F_platform section)
    static constexpr uint16_t MAX_FLOW_H = (SHOW_COUNT * STEP_H) + F_SECTION_H;
    lcd.fillRect(MARGIN, y, LINE_W, MAX_FLOW_H, BG);

    uint8_t prevTaskId = 0;
    for (uint8_t s = 0; s < SHOW_COUNT; s++) {
        uint8_t layer = SHOW_LAYERS[s];
        uint16_t color = LAYER_COLORS[layer];
        bool active = layerInfo[layer].active;
        uint16_t labelColor = active ? color : dimColor(color);

        // --- Layer box: two rows ---
        //   Row 1: Layer name (e.g. "L3 Services")  — layer color
        //   Row 2: Live trace method or static desc  — white (live) / dim (static)
        // Colored sidebar
        lcd.fillRect(ARROW_X - 3, y, 7, LAYER_H - 2,
                     active ? dimColor(color) : Theme::PANEL);

        // Layer name
        const char* layerLabel;
        switch (layer) {
            case 0: layerLabel = "L1 Transport"; break;
            case 1: layerLabel = "L2 Protocol"; break;
            case 2: layerLabel = "L3 Services"; break;
            case 3: layerLabel = "L4 Drivers"; break;
            default: layerLabel = "L5 Board/HAL"; break;
        }
        lcd.blitTextLine(FLOW_X, y, 120, 8, layerLabel, labelColor, BG);

        // Method/detail line — shows what's actually happening
        // Use live trace data if available, otherwise show static description
        if (active && layerInfo[layer].method != nullptr) {
            // Live data: method(detail) or just method
            if (layerInfo[layer].detail[0] != '\0') {
                snprintf(buf, sizeof(buf), " %s(%s)",
                         layerInfo[layer].method, layerInfo[layer].detail);
            } else {
                snprintf(buf, sizeof(buf), " %s()", layerInfo[layer].method);
            }
            lcd.blitTextLine(FLOW_X + 4, y + 8, 130, 8, buf, WHITE, BG);
        } else {
            // Static description for layers without trace data
            static constexpr const char* STATIC_DESC[] = {
                "UART rx/tx",         // L1
                "parse & dispatch",   // L2
                "service execute",    // L3
                "driver API call",    // L4
                "SPI/GPIO/ADC ops",   // L5
                "RTOS scheduling",    // F
            };
            const char* desc = (layer < NUM_LAYERS) ? STATIC_DESC[layer] : "";
            lcd.blitTextLine(FLOW_X + 4, y + 8, 130, 8, desc, DIM, BG);
        }

        // Task label on the right side
        uint8_t taskId = layerInfo[layer].taskId;
        // For L5, inherit L4's task (L5 always runs in same task as L4 caller)
        if (layer == 4 && taskId == 0 && layerInfo[3].taskId != 0) {
            taskId = layerInfo[3].taskId;
        }
        // For L1/L2, default to Comms task if no trace data
        if ((layer == 0 || layer == 1) && taskId == 0) {
            taskId = 3;  // Comms
        }
        const char* taskName;
        switch (taskId) {
            case 1: taskName = "[Motor]"; break;
            case 2: taskName = "[Encdr]"; break;
            case 3: taskName = "[Comms]"; break;
            case 4: taskName = "[Displ]"; break;
            case 5: taskName = "[Timer]"; break;
            default: taskName = ""; break;
        }
        lcd.blitTextLine(TASK_X, y, 80, 8, taskName,
                         active ? WHITE : DIM, BG);

        // Task handoff indicator
        if (s > 0 && prevTaskId != 0 && taskId != 0 && taskId != prevTaskId) {
            snprintf(buf, sizeof(buf), "handoff");
            lcd.blitTextLine(TASK_X, y + 8, 80, 8, buf, YELLOW, BG);
        }

        y += LAYER_H;

        // --- Arrow + interface info between layers ---
        if (s < SHOW_COUNT - 1) {
            // Vertical arrow line
            lcd.fillRect(ARROW_X, y, 1, ARROW_GAP - 2, DIM);
            // Small arrowhead
            lcd.fillRect(ARROW_X - 1, y + ARROW_GAP - 6, 3, 1, DIM);

            // Interface name — data-driven from trace entries.
            // Only show when the boundary was actually crossed.
            if (bndData[s].crossed) {
                uint16_t bndColor = LAYER_COLORS[SHOW_LAYERS[s]];
                lcd.blitTextLine(FLOW_X, y + 1, LINE_W - FLOW_X, 8,
                                 bndData[s].iface, bndColor, BG);
                // Show the actual method that crossed this boundary
                lcd.blitTextLine(FLOW_X + 4, y + 10, LINE_W - FLOW_X - 4, 8,
                                 bndData[s].method ? bndData[s].method : "",
                                 DIM, BG);
            } else {
                // Clear the boundary text area (not crossed)
                lcd.blitTextLine(FLOW_X, y + 1, LINE_W - FLOW_X, 8,
                                 "", BG, BG);
                lcd.blitTextLine(FLOW_X + 4, y + 10, LINE_W - FLOW_X - 4, 8,
                                 "", BG, BG);
            }
            y += ARROW_GAP;
        }

        if (taskId != 0) prevTaskId = taskId;
    }

    // --- L5 → F_platform boundary (data-driven, same as above) ---
    lcd.fillRect(ARROW_X, y, 1, ARROW_GAP - 2, DIM);
    lcd.fillRect(ARROW_X - 1, y + ARROW_GAP - 6, 3, 1, DIM);

    if (bndData[4].crossed) {
        lcd.blitTextLine(FLOW_X, y + 1, LINE_W - FLOW_X, 8,
                         bndData[4].iface, RED, BG);
        lcd.blitTextLine(FLOW_X + 4, y + 10, LINE_W - FLOW_X - 4, 8,
                         bndData[4].method != nullptr ? bndData[4].method : "", DIM, BG);
    } else {
        lcd.blitTextLine(FLOW_X, y + 1, LINE_W - FLOW_X, 8, "", BG, BG);
        lcd.blitTextLine(FLOW_X + 4, y + 10, LINE_W - FLOW_X - 4, 8, "", BG, BG);
    }
    y += ARROW_GAP;

    // F_platform layer box
    bool fActive = layerInfo[5].active;
    lcd.fillRect(ARROW_X - 3, y, 7, LAYER_H - 2,
                 fActive ? dimColor(YELLOW) : Theme::PANEL);
    lcd.blitTextLine(FLOW_X, y, 120, 8, "F  Platform",
                     fActive ? YELLOW : dimColor(YELLOW), BG);
    if (fActive && (layerInfo[5].method != nullptr)) {
        snprintf(buf, sizeof(buf), " %s()", layerInfo[5].method);
        lcd.blitTextLine(FLOW_X + 4, y + 8, 130, 8, buf, WHITE, BG);
    } else {
        lcd.blitTextLine(FLOW_X + 4, y + 8, 130, 8,
                         "FreeRTOSMutex impl", dimColor(YELLOW), BG);
    }

    Chrome::hintLine(lcd, HINT_Y, m_paused ? "PAUSED   CENTER resume   RIGHT next"
                                           : "CENTER pause   RIGHT next view", m_paused ? PAUSE_COLOR : DIM);
}

// =============================================================================
// Input handling
// =============================================================================

InputResult ArchScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (!pressed) return InputResult::HANDLED;

    switch (m_mode) {
        case Mode::DIAGRAM: return handleDiagramInput(dir);
        case Mode::MATRIX:  return handleMatrixInput(dir);
        case Mode::FLOW:    return handleFlowInput(dir);
        case Mode::TASKS:   return handleTasksInput(dir);
        case Mode::HARNESS: return handleHarnessInput(dir);
    }
    return InputResult::HANDLED;
}

InputResult ArchScreen::handleDiagramInput(JoyDirection dir)
{
    switch (dir) {
        case JoyDirection::LEFT:   return InputResult::EXIT_SCREEN;
        case JoyDirection::RIGHT:
        case JoyDirection::CENTER: switchMode(Mode::MATRIX); return InputResult::HANDLED;
        default:                   return InputResult::HANDLED;
    }
}

InputResult ArchScreen::handleMatrixInput(JoyDirection dir)
{
    switch (dir) {
        case JoyDirection::LEFT:   return InputResult::EXIT_SCREEN;
        case JoyDirection::RIGHT:  switchMode(Mode::FLOW); return InputResult::HANDLED;
        case JoyDirection::CENTER:
            m_paused = !m_paused;
            m_needsRedraw = true;
            if (!m_paused) sampleMeter(true);   // the next rates leave the pause out
            return InputResult::HANDLED;
        default:                   return InputResult::HANDLED;
    }
}

InputResult ArchScreen::handleHarnessInput(JoyDirection dir)
{
    // Same keys as the matrix; RIGHT wraps to the diagram
    if (dir == JoyDirection::RIGHT) { switchMode(Mode::DIAGRAM); return InputResult::HANDLED; }
    return handleMatrixInput(dir);
}

InputResult ArchScreen::handleFlowInput(JoyDirection dir)
{
    switch (dir) {
        case JoyDirection::LEFT:   return InputResult::EXIT_SCREEN;
        case JoyDirection::RIGHT:  switchMode(Mode::TASKS); return InputResult::HANDLED;
        case JoyDirection::CENTER: m_paused = !m_paused; m_needsRedraw = true; return InputResult::HANDLED;
        default:                   return InputResult::HANDLED;
    }
}

// =============================================================================
// TASKS mode — per-task layer activity
// =============================================================================

// Task name table (indexed by m_selectedTask)
static constexpr const char* TASK_NAMES[] = { "Motor", "Encoder", "Comms", "Display", "Timer" };

void ArchScreen::renderTasks(Services::ICanvas& lcd)
{
    size_t total = Trace::getTotal();
    size_t count = Trace::getCount();
    bool newData = (total != m_lastTotal);
    if (newData) m_lastTotal = total;

    if (!m_needsRedraw && !newData) return;
    if (!m_needsRedraw && m_paused) return;

    // Throttle live updates
    if (!m_needsRedraw && newData && !m_paused) {
        static uint8_t frameSkip = 0;
        if (++frameSkip < 8) return;
        frameSkip = 0;
    }

    uint16_t y = TOP;
    char buf[40];
    uint8_t selTaskId = TASK_IDS[m_selectedTask];

    // Title: "Task: Motor" etc.
    if (m_paused) {
        snprintf(buf, sizeof(buf), "PAUSED  Task: %s", TASK_NAMES[m_selectedTask]);
        lcd.blitTextLine(MARGIN, y, LINE_W, 8, buf, PAUSE_COLOR, BG);
    } else {
        snprintf(buf, sizeof(buf), "Task: %s", TASK_NAMES[m_selectedTask]);
        lcd.blitTextLine(MARGIN, y, LINE_W, 8, buf, CYAN, BG);
    }
    y += TITLE_H;
    lcd.drawHLine(MARGIN, y, LINE_W, CYAN);
    y += 4;

    // Scan trace entries filtered by selected taskId
    struct LayerInfo {
        const char* method;
        char detail[Trace::DETAIL_SIZE];
        bool active;
    };
    LayerInfo layerInfo[NUM_LAYERS] = {};

    // Boundary crossing data: [0]=L1-L2, [1]=L2-L3, [2]=L3-L4, [3]=L4-L5, [4]=L5-F
    struct BoundaryData {
        bool crossed;
        const char* iface;
        const char* method;
    };
    BoundaryData bndData[5] = {};

    // "Last" shows the most recent entry for this task
    char lastMethod[24] = "(idle)";

    Trace::Entry entry;
    bool foundFirst = false;
    for (size_t ri = 0; ri < count; ri++) {
        size_t i = count - 1 - ri;
        if (!Trace::getEntry(i, entry)) continue;
        if (entry.taskId != selTaskId) continue;

        // Capture the most recent entry as "Last"
        if (!foundFirst) {
            if (entry.detail[0] != '\0') {
                snprintf(lastMethod, sizeof(lastMethod), "%s(%s)",
                         entry.method != nullptr ? entry.method : "?", entry.detail);
            } else {
                snprintf(lastMethod, sizeof(lastMethod), "%s()",
                         entry.method != nullptr ? entry.method : "?");
            }
            foundFirst = true;
        }

        // Fill per-layer info
        uint8_t layer = boundaryToLayer(entry.boundary);
        if (layer < NUM_LAYERS && !layerInfo[layer].active) {
            layerInfo[layer].method = entry.method;
            snprintf(layerInfo[layer].detail, Trace::DETAIL_SIZE, "%s", entry.detail);   // F13
            layerInfo[layer].active = true;
        }

        // Fill per-boundary info
        if ((entry.boundary & Harness::ITrace::L1_L2_TRANSPORT) != 0 && !bndData[0].crossed) {
            bndData[0] = { true, "ITransport", entry.method };
        }
        if ((entry.boundary & Harness::ITrace::L2_L3_DISPATCH) != 0 && !bndData[1].crossed) {
            bndData[1] = { true, "ICommandDispatch", entry.method };
        }
        if ((entry.boundary & Harness::ITrace::L3_L4_MOTOR) != 0 && !bndData[2].crossed) {
            bndData[2] = { true, "IMotorDriver", entry.method };
        }
        if ((entry.boundary & Harness::ITrace::L3_L4_ENCODER) != 0 && !bndData[2].crossed) {
            bndData[2] = { true, "IEncoder", entry.method };
        }
        if ((entry.boundary & Harness::ITrace::L4_L5_SPI) != 0 && !bndData[3].crossed) {
            bndData[3] = { true, "ISPIBus", entry.method };
        }
        if ((entry.boundary & Harness::ITrace::L5_F_LOCK) != 0 && !bndData[4].crossed) {
            bndData[4] = { true, "ILock", entry.method };
        }

        // Stop after scanning enough entries for this task
        if (ri > 100) { break; }
    }

    // Show last activity
    snprintf(buf, sizeof(buf), "Last: %s", lastMethod);
    lcd.blitTextLine(MARGIN, y, LINE_W, LINE_H, buf, WHITE, BG);
    y += LINE_H + 2;

    // Layout constants (same as FLOW)
    static constexpr uint16_t FLOW_X = 20;
    static constexpr uint16_t ARROW_X = 14;
    static constexpr uint16_t LAYER_H = 16;
    static constexpr uint16_t ARROW_GAP = 20;
    static constexpr uint16_t STEP_H = LAYER_H + ARROW_GAP;
    static constexpr uint8_t SHOW_LAYERS[] = { 0, 1, 2, 3, 4 };
    static constexpr uint8_t SHOW_COUNT = 5;
    static constexpr uint16_t F_SECTION_H = ARROW_GAP + LAYER_H;
    static constexpr uint16_t MAX_FLOW_H = (SHOW_COUNT * STEP_H) + F_SECTION_H;

    lcd.fillRect(MARGIN, y, LINE_W, MAX_FLOW_H, BG);

    for (uint8_t s = 0; s < SHOW_COUNT; s++) {
        uint8_t layer = SHOW_LAYERS[s];
        uint16_t color = LAYER_COLORS[layer];
        bool active = layerInfo[layer].active;
        uint16_t labelColor = active ? color : dimColor(color);

        // Colored sidebar
        lcd.fillRect(ARROW_X - 3, y, 7, LAYER_H - 2,
                     active ? dimColor(color) : Theme::PANEL);

        // Layer name
        const char* layerLabel;
        switch (layer) {
            case 0: layerLabel = "L1 Transport"; break;
            case 1: layerLabel = "L2 Protocol"; break;
            case 2: layerLabel = "L3 Services"; break;
            case 3: layerLabel = "L4 Drivers"; break;
            default: layerLabel = "L5 Board/HAL"; break;
        }
        lcd.blitTextLine(FLOW_X, y, 120, 8, layerLabel, labelColor, BG);

        // Method/detail line
        if (active && layerInfo[layer].method != nullptr) {
            if (layerInfo[layer].detail[0] != '\0') {
                snprintf(buf, sizeof(buf), " %s(%s)",
                         layerInfo[layer].method, layerInfo[layer].detail);
            } else {
                snprintf(buf, sizeof(buf), " %s()", layerInfo[layer].method);
            }
            lcd.blitTextLine(FLOW_X + 4, y + 8, 150, 8, buf, WHITE, BG);
        } else {
            lcd.blitTextLine(FLOW_X + 4, y + 8, 150, 8, "(no trace)", DIM, BG);
        }
        y += LAYER_H;

        // Arrow + boundary between layers
        if (s < SHOW_COUNT - 1) {
            lcd.fillRect(ARROW_X, y, 1, ARROW_GAP - 2, DIM);
            lcd.fillRect(ARROW_X - 1, y + ARROW_GAP - 6, 3, 1, DIM);

            if (bndData[s].crossed) {
                uint16_t bndColor = LAYER_COLORS[SHOW_LAYERS[s]];
                lcd.blitTextLine(FLOW_X, y + 1, LINE_W - FLOW_X, 8,
                                 bndData[s].iface, bndColor, BG);
                lcd.blitTextLine(FLOW_X + 4, y + 10, LINE_W - FLOW_X - 4, 8,
                                 bndData[s].method != nullptr ? bndData[s].method : "",
                                 DIM, BG);
            } else {
                lcd.blitTextLine(FLOW_X, y + 1, LINE_W - FLOW_X, 8, "", BG, BG);
                lcd.blitTextLine(FLOW_X + 4, y + 10, LINE_W - FLOW_X - 4, 8, "", BG, BG);
            }
            y += ARROW_GAP;
        }
    }

    // L5 → F boundary
    lcd.fillRect(ARROW_X, y, 1, ARROW_GAP - 2, DIM);
    lcd.fillRect(ARROW_X - 1, y + ARROW_GAP - 6, 3, 1, DIM);
    if (bndData[4].crossed) {
        lcd.blitTextLine(FLOW_X, y + 1, LINE_W - FLOW_X, 8,
                         bndData[4].iface, RED, BG);
        lcd.blitTextLine(FLOW_X + 4, y + 10, LINE_W - FLOW_X - 4, 8,
                         bndData[4].method != nullptr ? bndData[4].method : "", DIM, BG);
    } else {
        lcd.blitTextLine(FLOW_X, y + 1, LINE_W - FLOW_X, 8, "", BG, BG);
        lcd.blitTextLine(FLOW_X + 4, y + 10, LINE_W - FLOW_X - 4, 8, "", BG, BG);
    }
    y += ARROW_GAP;

    // F_platform layer box
    bool fActive = layerInfo[5].active;
    lcd.fillRect(ARROW_X - 3, y, 7, LAYER_H - 2,
                 fActive ? dimColor(YELLOW) : Theme::PANEL);
    lcd.blitTextLine(FLOW_X, y, 120, 8, "F  Platform",
                     fActive ? YELLOW : dimColor(YELLOW), BG);
    if (fActive && (layerInfo[5].method != nullptr)) {
        snprintf(buf, sizeof(buf), " %s()", layerInfo[5].method);
        lcd.blitTextLine(FLOW_X + 4, y + 8, 150, 8, buf, WHITE, BG);
    } else {
        lcd.blitTextLine(FLOW_X + 4, y + 8, 150, 8, "(no trace)", DIM, BG);
    }

    Chrome::hintLine(lcd, HINT_Y, m_paused ? "PAUSED   UP/DOWN task   CENTER resume"
                                           : "UP/DOWN task   CENTER pause   RIGHT next", m_paused ? PAUSE_COLOR : DIM);
}

InputResult ArchScreen::handleTasksInput(JoyDirection dir)
{
    switch (dir) {
        case JoyDirection::LEFT:   return InputResult::EXIT_SCREEN;
        case JoyDirection::RIGHT:  switchMode(Mode::HARNESS); return InputResult::HANDLED;
        case JoyDirection::UP:
            m_selectedTask = (m_selectedTask == 0) ? (NUM_TASKS - 1) : (m_selectedTask - 1);
            m_needsRedraw = true;
            return InputResult::HANDLED;
        case JoyDirection::DOWN:
            m_selectedTask = (m_selectedTask + 1) % NUM_TASKS;
            m_needsRedraw = true;
            return InputResult::HANDLED;
        case JoyDirection::CENTER: m_paused = !m_paused; m_needsRedraw = true; return InputResult::HANDLED;
        default:                   return InputResult::HANDLED;
    }
}

} // namespace UI
