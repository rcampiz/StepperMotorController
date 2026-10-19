/**
 * @file memory_screen.cpp
 * @brief Memory page: flash and RAM gauges, the RAM split, the stack peaks, under the chrome.
 *
 * Static parts (labels, brackets, the section rule, the colour key, the deck) are drawn once
 * after activation. Once a second the figures are read again and only what changed is sent:
 * the gauge segments that switched, a text box whose text changed, a table row.
 */

#include "ui/screens/memory_screen.hpp"
#include "ui/chrome/chrome.hpp"
#include "ui/theme.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "F_platform/boundary/imemory_stats.hpp"
#include "F_platform/boundary/itask_stats.hpp"
#include <stdio.h>
#include <string.h>

namespace UI {

using namespace Theme;

namespace {

// --- layout (content band 42..306) ---------------------------------------------------------
constexpr uint16_t LINE_W = WIDTH - 2 * MARGIN;          // 228
// Gauges: a head row (label, size, large %), the bracketed segment bar, used / free under it
constexpr uint16_t GAUGE_Y[2] = {44, 92};
constexpr uint16_t PCT_W = 4 * LARGE_W;                  // "100%"
constexpr uint16_t PCT_X = WIDTH - MARGIN - PCT_W;
constexpr uint16_t BOX_DY = 18, BOX_H = 16;              // bracket box under the head row
constexpr uint16_t BAR_DY = 20, BAR_H = 12;
constexpr uint16_t TEXT_DY = 36;                         // small text under the bar
constexpr uint16_t SEG_W = 7, SEG_PITCH = 9;             // 25 x 9 - 2 = 223 px
constexpr uint16_t BAR_X = MARGIN + 2;
// RAM split: stacked bar, then its key
constexpr uint16_t SPLIT_BOX_Y = 140, SPLIT_Y = 142, SPLIT_H = 10;
constexpr uint16_t SPLIT_X = BAR_X, SPLIT_W = MemoryScreen::SEGMENTS * SEG_PITCH - 2;
constexpr uint16_t KEY_Y = 157;
constexpr uint16_t KEY_X[3] = {MARGIN + 2, 84, 166};
constexpr uint16_t KEY_W[3] = {74, 80, 68};
// Stack table: section head, rows (tick, name, bar, %, peak/size), the colour key, the deck
constexpr uint16_t STACK_HEAD_Y = 170;
constexpr uint16_t ROWS_Y = 182, ROW_H = 15;
constexpr uint16_t NAME_X = MARGIN + 4, NAME_W = 60;
constexpr uint16_t RBAR_X = 72, RBAR_DY = 3, RBAR_H = 7;
constexpr uint16_t RSEG_W = 3, RSEG_PITCH = 4, RSEGS = 16;   // 64 px
constexpr uint16_t RPCT_X = 140, RPCT_W = 32;
constexpr uint16_t DETAIL_X = 174, DETAIL_W = WIDTH - MARGIN - 174;   // 10 small chars
constexpr uint16_t ZONES_Y = ROWS_Y + MemoryScreen::MAX_ROWS * ROW_H + 4;   // 276
constexpr uint16_t DECK_Y = ZONES_Y + 12;

static_assert(BAR_X + MemoryScreen::SEGMENTS * SEG_PITCH - 2 <= MARGIN + LINE_W - 2, "gauge runs past its brackets");
static_assert(RBAR_X + RSEGS * RSEG_PITCH <= RPCT_X, "row bar runs into the percentage");
static_assert(GAUGE_Y[1] + TEXT_DY + SMALL_H <= SPLIT_BOX_Y, "RAM gauge runs into the split bar");
static_assert(DECK_Y + 12 < CONTENT_BOTTOM, "memory layout runs into the footer");

/// Copy a text into a cache field, cut to fit
template <size_t N>
void setText(char (&dst)[N], const char* src)
{
    size_t n = strlen(src);
    if (n > N - 1) n = N - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

uint16_t mix565(uint16_t a, uint16_t b, int t256)
{
    const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    return static_cast<uint16_t>(((ar + (br - ar) * t256 / 256) << 11) |
                                 ((ag + (bg - ag) * t256 / 256) << 5) | (ab + (bb - ab) * t256 / 256));
}

/// Headroom colour: green below 70 %, yellow to 90 %, red beyond
uint16_t zoneColor(uint32_t pct)
{
    if (pct > 90) return RED;
    if (pct >= 70) return YELLOW;
    return GREEN;
}

uint32_t percentOf(uint32_t used, uint32_t total)
{
    if (total == 0) return 0;
    return static_cast<uint32_t>((static_cast<uint64_t>(used) * 100u + total / 2u) / total);
}

uint8_t litOf(uint32_t used, uint32_t total, uint8_t segments)
{
    if (total == 0 || used == 0) return 0;
    uint64_t lit = (static_cast<uint64_t>(used) * segments + total / 2u) / total;
    if (lit == 0) lit = 1;
    if (lit > segments) lit = segments;
    return static_cast<uint8_t>(lit);
}

/// "275.2 KB"
void fmtKB(char* out, size_t n, uint32_t bytes)
{
    const uint32_t tenths = static_cast<uint32_t>((static_cast<uint64_t>(bytes) * 10u + 512u) / 1024u);
    snprintf(out, n, "%lu.%lu KB", static_cast<unsigned long>(tenths / 10u), static_cast<unsigned long>(tenths % 10u));
}

/// Compact: bytes below 1000, else "3.1K"
void fmtShort(char* out, size_t n, uint32_t bytes)
{
    if (bytes < 1000u) {
        snprintf(out, n, "%lu", static_cast<unsigned long>(bytes));
        return;
    }
    const uint32_t tenths = static_cast<uint32_t>((static_cast<uint64_t>(bytes) * 10u + 512u) / 1024u);
    if (tenths >= 1000u) snprintf(out, n, "%luK", static_cast<unsigned long>((tenths + 5u) / 10u));
    else snprintf(out, n, "%lu.%luK", static_cast<unsigned long>(tenths / 10u), static_cast<unsigned long>(tenths % 10u));
}

/// One VU segment: lit = neon (a bright top edge, a dark bottom edge), else a dark tint of its zone
void segment(Services::ICanvas& lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t zone, bool lit)
{
    if (!lit) {
        lcd.fillRect(x, y, w, h, mix565(BG, zone, 52));
        return;
    }
    lcd.fillRect(x, y, w, 1, mix565(zone, TEXT, 120));
    lcd.fillRect(x, static_cast<uint16_t>(y + 1), w, static_cast<uint16_t>(h - 2), zone);
    lcd.fillRect(x, static_cast<uint16_t>(y + h - 1), w, 1, mix565(zone, BG, 110));
}

/// Small text right-aligned in a box of w px (the whole box painted, so a shorter text erases)
void smallRight(Services::ICanvas& lcd, uint16_t x, uint16_t y, uint16_t w, const char* text, uint16_t fg)
{
    char buf[40];
    size_t cols = w / SMALL_W;
    if (cols > sizeof buf - 1) cols = sizeof buf - 1;
    size_t len = strlen(text);
    if (len > cols) len = cols;
    memset(buf, ' ', cols - len);
    memcpy(buf + (cols - len), text, len);
    buf[cols] = '\0';
    lcd.blitTextLine(static_cast<uint16_t>(x + w - cols * SMALL_W), y, static_cast<uint16_t>(cols * SMALL_W),
                     SMALL_H, buf, fg, BG);
}

/// Large text right-aligned in a 16 px tall box of w px
void largeRight(Services::ICanvas& lcd, uint16_t x, uint16_t y, uint16_t w, const char* text, uint16_t fg)
{
    size_t len = strlen(text);
    if (len > static_cast<size_t>(w / LARGE_W)) len = w / LARGE_W;
    char buf[8];
    if (len > sizeof buf - 1) len = sizeof buf - 1;
    memcpy(buf, text, len);
    buf[len] = '\0';
    const auto tw = static_cast<uint16_t>(len * LARGE_W);
    if (tw < w) lcd.fillRect(x, y, static_cast<uint16_t>(w - tw), LARGE_H, BG);
    if (len > 0) lcd.drawString(static_cast<uint16_t>(x + w - tw), y, buf, fg, BG, FONT_LARGE);
}

/// Section heading: small violet text, a rule to the right, a dim note at the right end
void sectionHead(Services::ICanvas& lcd, uint16_t y, const char* text, const char* note)
{
    lcd.drawString(MARGIN, y, text, VIOLET, BG, FONT_SMALL);
    const auto noteW = static_cast<uint16_t>(strlen(note) * SMALL_W);
    const auto ruleX = static_cast<uint16_t>(MARGIN + strlen(text) * SMALL_W + 4);
    const auto ruleEnd = static_cast<uint16_t>(WIDTH - MARGIN - noteW - 4);
    if (ruleEnd > ruleX) lcd.fillRect(ruleX, static_cast<uint16_t>(y + 4), static_cast<uint16_t>(ruleEnd - ruleX), 1, GRID_MAJ);
    lcd.drawString(static_cast<uint16_t>(WIDTH - MARGIN - noteW), y, note, DIM, BG, FONT_SMALL);
}

constexpr const char* const GAUGE_LABEL[2] = {"FLASH", "RAM"};

/// A stack table entry before it is drawn
struct StackEntry {
    char name[8];
    uint32_t used, size;    ///< bytes; size 0 = unknown (used then holds the free bytes)
};

} // namespace

void MemoryScreen::onActivate()
{
    m_needsRedraw = true;
    m_full = true;
    m_haveData = false;
    m_frame = 0;
    for (Gauge& g : m_gauge) { memset(&g, 0, sizeof g); g.lit = 0xFF; }
    m_splitA = m_splitB = 0xFFFF;
    memset(m_legend, 0, sizeof m_legend);
    memset(m_rows, 0, sizeof m_rows);
}

InputResult MemoryScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (pressed && dir == JoyDirection::LEFT) return InputResult::EXIT_SCREEN;
    return InputResult::HANDLED;
}

void MemoryScreen::drawStatic(Services::ICanvas& lcd)
{
    Platform::IMemoryStats* mem = Platform::memoryStatsProvider();
    Platform::MemoryStats probe = {};
    m_haveData = mem != nullptr && mem->getMemoryStats(probe);
    if (!m_haveData) {
        Chrome::textBox(lcd, MARGIN, static_cast<uint16_t>(CONTENT_Y + 20), LINE_W, 16, 6, 2,
                        "No memory statistics", TEXT, BG);
        Chrome::drawDeck(lcd, static_cast<uint16_t>(CONTENT_Y + 44), CONTENT_BOTTOM);
        return;
    }
    for (uint8_t g = 0; g < 2; g++) {
        const uint16_t y = GAUGE_Y[g];
        lcd.fillRect(MARGIN, static_cast<uint16_t>(y + 2), 3, 12, MAGENTA);     // the HUD tick
        lcd.drawString(static_cast<uint16_t>(MARGIN + 6), static_cast<uint16_t>(y + 3), GAUGE_LABEL[g], CYAN, BG,
                       FONT_MEDIUM);
        Chrome::drawCorners(lcd, MARGIN, static_cast<uint16_t>(y + BOX_DY), LINE_W, BOX_H, 5, GRID_MAJ);
    }
    Chrome::drawCorners(lcd, MARGIN, SPLIT_BOX_Y, LINE_W, static_cast<uint16_t>(SPLIT_H + 4), 5, GRID_MAJ);
    const uint16_t swatch[3] = {MAGENTA, CYAN, GRID_MAJ};
    for (uint8_t i = 0; i < 3; i++) {
        lcd.fillRect(KEY_X[i], static_cast<uint16_t>(KEY_Y + 1), 6, 6, swatch[i]);
    }
    sectionHead(lcd, STACK_HEAD_Y, "STACK PEAKS", "peak/size B");

    // The colour key, one line: what the three colours mean
    char text[40];
    uint16_t colors[40];
    size_t n = 0;
    auto put = [&](const char* s, uint16_t c) {
        for (; *s && n < sizeof text - 1; s++) { text[n] = *s; colors[n] = c; n++; }
    };
    put("<70% ", GREEN);  put("OK", TEXT);    put("   ", BG);
    put("70-90% ", YELLOW); put("TIGHT", TEXT); put("   ", BG);
    put(">90% ", RED);    put("FULL", TEXT);
    text[n] = '\0';
    lcd.blitTextLineColored(static_cast<uint16_t>(MARGIN + 2), ZONES_Y, static_cast<uint16_t>(LINE_W - 2), SMALL_H,
                            text, colors, BG);
    Chrome::drawDeck(lcd, DECK_Y, CONTENT_BOTTOM);
}

void MemoryScreen::drawGauge(Services::ICanvas& lcd, uint8_t g, uint32_t used, uint32_t total)
{
    Gauge& G = m_gauge[g];
    const uint16_t y = GAUGE_Y[g];
    const uint32_t pct = percentOf(used, total);
    const uint16_t zone = zoneColor(pct);
    char buf[24];

    // Size next to the label: "of 512 KB"
    snprintf(buf, sizeof buf, "of %lu KB%s", static_cast<unsigned long>(total / 1024u), g == 1 ? " static" : "");
    if (strcmp(buf, G.total) != 0) {
        setText(G.total, buf);
        const auto x = static_cast<uint16_t>(MARGIN + 6 + strlen(GAUGE_LABEL[g]) * MED_W + 6);
        lcd.blitTextLine(x, static_cast<uint16_t>(y + 6), static_cast<uint16_t>(PCT_X - 4 - x), SMALL_H, buf, VIOLET, BG);
    }
    // The percentage, large, in the colour of its zone (the colour is part of the text cache)
    snprintf(buf, sizeof buf, "%lu%%%c", static_cast<unsigned long>(pct), zone == RED ? 'r' : zone == YELLOW ? 'y' : 'g');
    if (strcmp(buf, G.pct) != 0) {
        setText(G.pct, buf);
        buf[strlen(buf) - 1] = '\0';
        largeRight(lcd, PCT_X, y, PCT_W, buf, zone);
    }
    // The segments that switched
    const uint8_t lit = litOf(used, total, SEGMENTS);
    if (lit != G.lit) {
        for (uint8_t i = 0; i < SEGMENTS; i++) {
            const bool on = i < lit;
            const bool was = G.lit != 0xFF && i < G.lit;
            if (G.lit != 0xFF && on == was) continue;
            const uint32_t mid = (2u * i + 1u) * 50u / SEGMENTS;
            segment(lcd, static_cast<uint16_t>(BAR_X + i * SEG_PITCH), static_cast<uint16_t>(y + BAR_DY), SEG_W, BAR_H,
                    zoneColor(mid), on);
        }
        G.lit = lit;
    }
    // Used, left; free, right in the zone colour
    char kb[16];
    fmtKB(kb, sizeof kb, used);
    snprintf(buf, sizeof buf, "%s used", kb);
    if (strcmp(buf, G.used) != 0) {
        setText(G.used, buf);
        lcd.blitTextLine(BAR_X, static_cast<uint16_t>(y + TEXT_DY), 108, SMALL_H, buf, TEXT, BG);
    }
    fmtKB(kb, sizeof kb, total > used ? total - used : 0);
    snprintf(buf, sizeof buf, "%s free%c", kb, zone == RED ? 'r' : zone == YELLOW ? 'y' : 'g');
    if (strcmp(buf, G.free) != 0) {
        setText(G.free, buf);
        buf[strlen(buf) - 1] = '\0';
        smallRight(lcd, 120, static_cast<uint16_t>(y + TEXT_DY), static_cast<uint16_t>(WIDTH - MARGIN - 2 - 120), buf, zone);
    }
}

void MemoryScreen::drawSplit(Services::ICanvas& lcd, uint32_t stacks, uint32_t globals, uint32_t total)
{
    if (total == 0) return;
    const auto a = static_cast<uint16_t>(static_cast<uint64_t>(stacks) * SPLIT_W / total);
    const auto b = static_cast<uint16_t>(static_cast<uint64_t>(stacks + globals) * SPLIT_W / total);
    if (a != m_splitA || b != m_splitB) {
        m_splitA = a;
        m_splitB = b;
        // Stacks magenta, globals cyan (neon top edge, a dark seam after each), free as dim
        // diagonal hazard stripes. Rectangles, each pixel once: no row buffer in RAM.
        auto block = [&](uint16_t x0, uint16_t x1, uint16_t base) {
            if (x1 <= x0) return;
            const auto x = static_cast<uint16_t>(SPLIT_X + x0);
            const auto w = static_cast<uint16_t>(x1 - x0 - 1);          // the last column is the seam
            if (w > 0) {
                lcd.fillRect(x, SPLIT_Y, w, 1, mix565(base, TEXT, 120));
                lcd.fillRect(x, static_cast<uint16_t>(SPLIT_Y + 1), w, SPLIT_H - 2, base);
                lcd.fillRect(x, static_cast<uint16_t>(SPLIT_Y + SPLIT_H - 1), w, 1, mix565(base, BG, 110));
            }
            lcd.fillRect(static_cast<uint16_t>(x + w), SPLIT_Y, 1, SPLIT_H, BG);
        };
        block(0, a, MAGENTA);
        block(a, b, CYAN);
        const uint16_t stripe = mix565(BG, GRID_MAJ, 150);
        for (uint16_t r = 0; r < SPLIT_H; r++) {
            for (uint16_t c = b; c < SPLIT_W;) {
                const bool on = (((c + r) / 3u) & 1u) == 0;
                uint16_t end = static_cast<uint16_t>(c + 1);
                while (end < SPLIT_W && ((((end + r) / 3u) & 1u) == 0) == on) end++;
                lcd.fillRect(static_cast<uint16_t>(SPLIT_X + c), static_cast<uint16_t>(SPLIT_Y + r),
                             static_cast<uint16_t>(end - c), 1, on ? stripe : BG);
                c = end;
            }
        }
    }
    // The key: STACKS / GLOBALS / FREE with whole KB
    const uint32_t freeB = total > stacks + globals ? total - stacks - globals : 0;
    const uint32_t values[3] = {stacks, globals, freeB};
    const char* const names[3] = {"STACKS", "GLOBALS", "FREE"};
    const uint16_t inks[3] = {MAGENTA, CYAN, TEXT};
    for (uint8_t i = 0; i < 3; i++) {
        char buf[16];
        snprintf(buf, sizeof buf, "%s %luK", names[i], static_cast<unsigned long>((values[i] + 512u) / 1024u));
        if (strcmp(buf, m_legend[i]) == 0) continue;
        setText(m_legend[i], buf);
        lcd.blitTextLine(static_cast<uint16_t>(KEY_X[i] + 9), KEY_Y, static_cast<uint16_t>(KEY_W[i] - 9), SMALL_H, buf,
                         inks[i], BG);
    }
}

void MemoryScreen::drawRows(Services::ICanvas& lcd)
{
    // Gather: the main stack first, then the tasks, biggest stack first (a stable order)
    StackEntry e[MAX_ROWS];
    uint8_t n = 0;
    Platform::MemoryStats m = {};
    Platform::IMemoryStats* mem = Platform::memoryStatsProvider();
    if (mem != nullptr && mem->getMemoryStats(m) && m.mainStackTotal != 0) {
        snprintf(e[n].name, sizeof e[n].name, "ISR");
        e[n].used = m.mainStackUsed;
        e[n].size = m.mainStackTotal;
        n++;
    }
    const uint8_t firstTask = n;
    Platform::ITaskStats* stats = Platform::taskStatsProvider();
    if (stats != nullptr) {
        Platform::TaskStatsSnapshot snap;
        stats->getSnapshot(snap);
        for (uint8_t i = 0; i < snap.count && i < Platform::TaskStatsSnapshot::MAX_TASKS && n < MAX_ROWS; i++) {
            const Platform::TaskStat& t = snap.tasks[i];
            StackEntry& s = e[n++];
            snprintf(s.name, sizeof s.name, "%.7s", t.name);
            s.size = static_cast<uint32_t>(t.stackWords) * 4u;
            const uint32_t freeB = static_cast<uint32_t>(t.stackHWM) * 4u;
            s.used = s.size > freeB ? s.size - freeB : (s.size == 0 ? freeB : 0);
        }
        for (uint8_t i = static_cast<uint8_t>(firstTask + 1); i < n; i++) {          // insertion sort
            StackEntry k = e[i];
            uint8_t j = i;
            while (j > firstTask && (e[j - 1].size < k.size ||
                                     (e[j - 1].size == k.size && strcmp(e[j - 1].name, k.name) > 0))) {
                e[j] = e[j - 1];
                j--;
            }
            e[j] = k;
        }
    }

    for (uint8_t i = 0; i < MAX_ROWS; i++) {
        Row& R = m_rows[i];
        const auto y = static_cast<uint16_t>(ROWS_Y + i * ROW_H);
        if (i >= n) {
            if (R.valid) {
                lcd.fillRect(MARGIN, y, LINE_W, static_cast<uint16_t>(ROW_H - 2), BG);
                R.valid = false;
            }
            continue;
        }
        const StackEntry& s = e[i];
        const bool known = s.size != 0;
        const uint32_t pct = known ? percentOf(s.used, s.size) : 0;
        const uint16_t zone = known ? zoneColor(pct) : DIM;
        const uint8_t lit = known ? litOf(s.used, s.size, RSEGS) : 0;
        char pctText[8], detail[24], a[12], b[12];
        if (known) {
            snprintf(pctText, sizeof pctText, "%lu%%", static_cast<unsigned long>(pct));
            fmtShort(a, sizeof a, s.used);
            fmtShort(b, sizeof b, s.size);
            snprintf(detail, sizeof detail, "%s/%s", a, b);
        } else {
            snprintf(pctText, sizeof pctText, "--");
            fmtShort(a, sizeof a, s.used);
            snprintf(detail, sizeof detail, "%s free", a);
        }
        const bool whole = !R.valid || strcmp(R.name, s.name) != 0;
        if (whole) {
            Chrome::textBox(lcd, NAME_X, y, NAME_W, 13, 0, 1, s.name, TEXT, BG);
            setText(R.name, s.name);
        }
        if (whole || zone != R.color) {
            lcd.fillRect(MARGIN, static_cast<uint16_t>(y + 1), 2, 11, zone);
        }
        if (whole || lit != R.lit || zone != R.color) {
            for (uint8_t k = 0; k < RSEGS; k++) {
                const bool on = k < lit;
                if (!whole && zone == R.color && on == (k < R.lit)) continue;
                const uint32_t mid = (2u * k + 1u) * 50u / RSEGS;
                segment(lcd, static_cast<uint16_t>(RBAR_X + k * RSEG_PITCH), static_cast<uint16_t>(y + RBAR_DY), RSEG_W,
                        RBAR_H, known ? zoneColor(mid) : DIM, on);
            }
        }
        if (whole || zone != R.color || strcmp(pctText, R.pct) != 0) {
            const size_t len = strlen(pctText);
            Chrome::textBox(lcd, RPCT_X, y, RPCT_W, 13, static_cast<uint16_t>(RPCT_W - len * MED_W), 1, pctText, zone, BG);
            setText(R.pct, pctText);
        }
        if (whole || strcmp(detail, R.detail) != 0) {
            smallRight(lcd, DETAIL_X, static_cast<uint16_t>(y + 3), DETAIL_W, detail, TEXT);
            setText(R.detail, detail);
        }
        R.lit = lit;
        R.color = zone;
        R.valid = true;
    }
}

void MemoryScreen::render(Services::ICanvas& lcd)
{
    if (m_full) {
        m_full = false;
        drawStatic(lcd);
        m_frame = 0;
    }
    if (!m_haveData) return;
    if (m_frame > 0) {                     // sampled once a second: the figures move slowly
        m_frame--;
        return;
    }
    m_frame = FRAMES_PER_SAMPLE - 1;

    Platform::MemoryStats m = {};
    Platform::IMemoryStats* mem = Platform::memoryStatsProvider();
    if (mem == nullptr || !mem->getMemoryStats(m)) return;
    drawGauge(lcd, 0, m.flashUsed, m.flashTotal);
    drawGauge(lcd, 1, m.ramStatic, m.ramTotal);
    const uint32_t stacks = m.stackPoolTotal < m.ramStatic ? m.stackPoolTotal : m.ramStatic;
    drawSplit(lcd, stacks, m.ramStatic - stacks, m.ramTotal);
    drawRows(lcd);
}

} // namespace UI
