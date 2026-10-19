/**
 * @file chrome.cpp
 * @brief Title tab, status line, footer and shared drawing helpers.
 */

#include "ui/chrome/chrome.hpp"
#include "ui/theme.hpp"
#include <string.h>
#include <stdio.h>

namespace UI {
namespace Chrome {

using namespace Theme;

namespace {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

uint16_t mix565(uint16_t a, uint16_t b, int t256) {
    const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    return static_cast<uint16_t>(((ar + (br - ar) * t256 / 256) << 11) |
                                 ((ag + (bg - ag) * t256 / 256) << 5) | (ab + (bb - ab) * t256 / 256));
}

int16_t sinApprox(int32_t a1024, int16_t amp) {             // a sine from two parabolas
    int32_t a = a1024 & 1023;
    const int32_t sign = (a < 512) ? 1 : -1;
    a &= 511;
    return static_cast<int16_t>(sign * a * (512 - a) * amp / 65536);
}

// The selection's animation (frames at 20 Hz; the bar repaints every other frame)
constexpr uint16_t SWAY_FRAMES = 32;          // 1.6 s side to side
constexpr int16_t  SWAY_PX = 3;
constexpr uint16_t FLASH_FRAMES = 12;         // a new colour every 0.6 s
constexpr uint16_t STRIPE_W = 7;              // the bar's diagonal stripes
struct Ink { uint16_t top, bottom; };
/// A bar's look: stripes, the label's hard shadow, the value's ink, the label's flash colours
struct BarStyle { uint16_t stripeA, stripeB, shadow, value; Ink faces[3]; };
constexpr BarStyle SELECT_STYLE = {          // selected: hot pink
    PINK, rgb(196, 18, 150), rgb(46, 0, 70), TEXT,
    {{rgb(255, 255, 255), rgb(255, 210, 240)},  // white
     {rgb(255, 250, 120), rgb(255, 160, 20)},   // yellow to amber
     {rgb(120, 255, 255), rgb(60, 150, 255)}}}; // cyan to blue
constexpr BarStyle EDIT_STYLE = {            // editing a value: cyan-blue
    CYAN, rgb(0, 140, 210), rgb(0, 24, 64), BG,
    {{rgb(255, 255, 255), rgb(220, 240, 255)},  // white
     {rgb(255, 250, 120), rgb(255, 170, 30)},   // yellow to amber
     {rgb(255, 120, 230), rgb(250, 40, 170)}}}; // pink to magenta

struct Selection {
    bool active = false;
    bool editing = false;
    uint16_t x = 0, y = 0, w = 0, h = 0;
    char label[28] = {};
    char value[20] = {};
};
Selection s_sel;
uint16_t s_tick = 0;

bool glyphInk(Services::ICanvas& lcd, const char* text, int16_t len, int16_t gx, int16_t gy) {
    if (gy < 0 || gy >= MED_H || gx < 0 || gx >= len * MED_W) return false;
    const uint8_t* rows = lcd.mediumGlyphRows(text[gx / MED_W]);
    return rows != nullptr && (rows[gy] & (0x80u >> (gx % MED_W))) != 0;
}

void composeSelection(Services::ICanvas& lcd)
{
    const Selection& sel = s_sel;
    const auto labelLen = static_cast<int16_t>(strlen(sel.label));
    const auto valueLen = static_cast<int16_t>(strlen(sel.value));
    const int16_t lx0 = static_cast<int16_t>(4 + SWAY_PX + sinApprox(static_cast<int32_t>(s_tick) * 1024 / SWAY_FRAMES, SWAY_PX));
    const int16_t vx0 = static_cast<int16_t>(sel.w - 6 - valueLen * MED_W);
    const BarStyle& style = sel.editing ? EDIT_STYLE : SELECT_STYLE;
    const Ink& ink = style.faces[(s_tick / FLASH_FRAMES) % 3];
    const uint16_t phase = static_cast<uint16_t>(s_tick / 2);
    constexpr int16_t PAD_Y = 2;

    // The label leans right at the top (up to 2 px): slanted letters
    auto labelInk = [&](int16_t gx, int16_t gy) -> bool {
        if (gy < 0 || gy >= MED_H) return false;
        return glyphInk(lcd, sel.label, labelLen, static_cast<int16_t>(gx - (MED_H - 1 - gy) / 4), gy);
    };

    static uint8_t row[WIDTH * 2];
    lcd.streamBitmapStart(sel.x, sel.y, sel.w, sel.h);
    for (uint16_t r = 0; r < sel.h; r++) {
        const int16_t gy = static_cast<int16_t>(r - PAD_Y);
        const uint16_t face = mix565(ink.top, ink.bottom, gy <= 0 ? 0 : gy * 256 / (MED_H - 1));
        for (uint16_t x = 0; x < sel.w; x++) {
            const int16_t lx = static_cast<int16_t>(x - lx0), vx = static_cast<int16_t>(x - vx0);
            uint16_t c;
            if (labelInk(lx, gy)) c = face;
            else if (glyphInk(lcd, sel.value, valueLen, vx, gy)) c = style.value;
            else if (labelInk(static_cast<int16_t>(lx - 2), static_cast<int16_t>(gy - 2)) ||
                     (!sel.editing &&
                      glyphInk(lcd, sel.value, valueLen, static_cast<int16_t>(vx - 1), static_cast<int16_t>(gy - 1))))
                c = style.shadow;
            else c = (((x + r + phase) / STRIPE_W) & 1u) ? style.stripeB : style.stripeA;
            row[x * 2] = static_cast<uint8_t>(c >> 8);
            row[x * 2 + 1] = static_cast<uint8_t>(c & 0xFF);
        }
        lcd.streamBitmapData(row, static_cast<size_t>(sel.w) * 2);
    }
    lcd.streamBitmapEnd();
}

} // namespace

void selectRow(Services::ICanvas& lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
               const char* label, const char* value, bool editing)
{
    if (lcd.mediumGlyphRows('A') == nullptr || w > WIDTH) {       // no font: a plain bar
        textBox(lcd, x, y, w, h, 4, 2, label, editing ? BG : TEXT, editing ? CYAN : MAGENTA);
        return;
    }
    s_sel.active = true;
    s_sel.editing = editing;
    s_sel.x = x; s_sel.y = y; s_sel.w = w; s_sel.h = h;
    snprintf(s_sel.label, sizeof s_sel.label, "%s", label != nullptr ? label : "");
    snprintf(s_sel.value, sizeof s_sel.value, "%s", value != nullptr ? value : "");
    composeSelection(lcd);
}

void unselectRowAt(uint16_t y)
{
    if (s_sel.active && s_sel.y == y) s_sel.active = false;
}

void clearSelection()
{
    s_sel.active = false;
}

void animateSelection(Services::ICanvas& lcd)
{
    s_tick++;
    if (s_sel.active && (s_tick & 1) == 0) composeSelection(lcd);
}

void drawTitleBand(Services::ICanvas& lcd, const char* title)
{
    size_t len = (title != nullptr) ? strlen(title) : 0;
    if (len > 16) len = 16;                                  // 16 x 12 = 192 px tab
    char buf[17];
    memcpy(buf, title, len);
    buf[len] = '\0';

    auto textW = static_cast<uint16_t>(len * LARGE_W);
    const uint16_t tabH = 20;                                // rows 2..21
    auto tabW = static_cast<uint16_t>(textW + 16 + tabH);    // text, padding, the slanted end

    // Rows 0..1 and 22..23: page colour
    lcd.fillRect(0, 0, WIDTH, 2, BG);
    // The tab: a cyan block whose right edge slants (one px per row), text cut in BG
    fillAround(lcd, 0, 2, static_cast<uint16_t>(textW + 16), tabH, 8, 4, textW, LARGE_H, CYAN);
    if (len > 0) lcd.drawString(8, 4, buf, BG, CYAN, FONT_LARGE);
    for (uint16_t r = 0; r < tabH; r++) {
        auto x = static_cast<uint16_t>(textW + 16);
        auto w = static_cast<uint16_t>(tabH - r);
        lcd.fillRect(x, static_cast<uint16_t>(2 + r), w, 1, CYAN);
        lcd.fillRect(static_cast<uint16_t>(x + w), static_cast<uint16_t>(2 + r),
                     static_cast<uint16_t>(WIDTH - x - w), 1, BG);
    }
    (void)tabW;
    lcd.fillRect(0, 22, WIDTH, 2, BG);
    // Rule: cyan, with a magenta segment at the far end and a hazard notch
    lcd.fillRect(0, 24, static_cast<uint16_t>(WIDTH - 44), 2, CYAN);
    lcd.fillRect(static_cast<uint16_t>(WIDTH - 44), 24, 4, 2, BG);
    lcd.fillRect(static_cast<uint16_t>(WIDTH - 40), 24, 40, 2, MAGENTA);
    // Tag at the right of the tab row: three magenta ticks, the HUD signature
    for (uint16_t i = 0; i < 3; i++) {
        lcd.fillRect(static_cast<uint16_t>(WIDTH - 10 - i * 8), 8, 4, 8, i == 0 ? MAGENTA : GRID_MAJ);
    }
}

void drawStatusBar(Services::ICanvas& lcd, const StatusLine& s)
{
    // Three fixed boxes so a shorter new text erases the old one: [4,124) [124,190) [190,236)
    lcd.fillRect(0, STATUS_Y, 4, STATUS_H, BG);
    lcd.blitTextLine(4,   STATUS_Y, 120, STATUS_H, s.left,  s.leftColor,  BG);
    lcd.blitTextLine(124, STATUS_Y, 66,  STATUS_H, s.mid,   s.midColor,   BG);
    lcd.blitTextLine(190, STATUS_Y, 46,  STATUS_H, s.right, s.rightColor, BG);
    lcd.fillRect(236, STATUS_Y, 4, STATUS_H, BG);
    lcd.fillRect(0, static_cast<uint16_t>(STATUS_Y + STATUS_H + 2), WIDTH, 1, GRID);
}

void drawFooter(Services::ICanvas& lcd, bool canGoBack, const char* centerHint)
{
    char text[40];
    uint16_t colors[40];
    size_t n = 0;
    auto put = [&](const char* s, uint16_t c) {
        for (; *s && n < 39; s++) { text[n] = *s; colors[n] = c; n++; }
    };
    if (canGoBack) { put("<", CYAN); put("BACK   ", TEXT); }
    if (centerHint != nullptr && centerHint[0] != '\0') { put("o", MAGENTA); put(centerHint, TEXT); put("   ", TEXT); }
    put("o", MAGENTA); put("2s HOME", TEXT);
    text[n] = '\0';

    lcd.fillRect(0, FOOTER_Y, WIDTH, 1, GRID_MAJ);
    lcd.fillRect(0, static_cast<uint16_t>(FOOTER_Y + 1), WIDTH, 2, PANEL);
    lcd.fillRect(0, static_cast<uint16_t>(FOOTER_Y + 3), 4, SMALL_H, PANEL);
    lcd.blitTextLineColored(4, static_cast<uint16_t>(FOOTER_Y + 3), 232, SMALL_H, text, colors, PANEL);
    lcd.fillRect(236, static_cast<uint16_t>(FOOTER_Y + 3), 4, SMALL_H, PANEL);
    lcd.fillRect(0, static_cast<uint16_t>(FOOTER_Y + 3 + SMALL_H), WIDTH,
                 static_cast<uint16_t>(HEIGHT - (FOOTER_Y + 3 + SMALL_H)), PANEL);
    // Cyan edge marks at both ends of the strip
    lcd.fillRect(0, FOOTER_Y, 2, FOOTER_H, CYAN);
    lcd.fillRect(static_cast<uint16_t>(WIDTH - 2), FOOTER_Y, 2, FOOTER_H, CYAN);
}

void drawDeck(Services::ICanvas& lcd, uint16_t top, uint16_t bottom)
{
    if (bottom <= top + 12) return;
    // Scanlines every 4 px, framed by corner brackets
    for (uint16_t y = static_cast<uint16_t>(top + 4); y < bottom - 2; y = static_cast<uint16_t>(y + 4)) {
        lcd.fillRect(MARGIN, y, static_cast<uint16_t>(WIDTH - 2 * MARGIN), 1, SCAN);
    }
    drawCorners(lcd, MARGIN, top, static_cast<uint16_t>(WIDTH - 2 * MARGIN),
                static_cast<uint16_t>(bottom - top), 10, GRID_MAJ);
}

void drawCorners(Services::ICanvas& lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 uint16_t len, uint16_t color)
{
    auto x1 = static_cast<uint16_t>(x + w - 1);
    auto y1 = static_cast<uint16_t>(y + h - 1);
    lcd.fillRect(x, y, len, 1, color);   lcd.fillRect(x, y, 1, len, color);
    lcd.fillRect(static_cast<uint16_t>(x1 - len + 1), y, len, 1, color);   lcd.fillRect(x1, y, 1, len, color);
    lcd.fillRect(x, y1, len, 1, color);  lcd.fillRect(x, static_cast<uint16_t>(y1 - len + 1), 1, len, color);
    lcd.fillRect(static_cast<uint16_t>(x1 - len + 1), y1, len, 1, color);
    lcd.fillRect(x1, static_cast<uint16_t>(y1 - len + 1), 1, len, color);
}

void fieldRow(Services::ICanvas& lcd, uint16_t y, const char* label, const char* value,
              RowState state, uint16_t valueColor, bool labelToo)
{
    constexpr uint16_t X = MARGIN, W = WIDTH - 2 * MARGIN, H = ROW_H - 2;
    constexpr uint16_t LABEL_W = 112;                       // 13 chars + pad
    constexpr uint16_t VALUE_X = X + LABEL_W, VALUE_W = W - LABEL_W;
    uint16_t bg, fgLabel, fgValue;
    switch (state) {
        case RowState::SELECTED: bg = MAGENTA; fgLabel = TEXT; fgValue = TEXT; break;
        case RowState::EDITING:  bg = CYAN;    fgLabel = BG;   fgValue = BG;   break;
        case RowState::DISABLED: bg = BG;      fgLabel = DIM;  fgValue = DIM;  break;
        default:                 bg = BG;      fgLabel = TEXT; fgValue = (valueColor != 0) ? valueColor : CYAN; break;
    }
    if (state == RowState::NORMAL && valueColor != 0) fgValue = valueColor;

    if (state == RowState::SELECTED || state == RowState::EDITING) {
        const bool editing = state == RowState::EDITING;
        char lab[20];
        size_t n = strlen(label);
        if (n > 11) n = 11;
        lab[0] = '>'; lab[1] = ' ';
        memcpy(lab + 2, label, n);
        lab[2 + n] = '\0';
        char vbuf[16];
        if (editing) snprintf(vbuf, sizeof vbuf, "<%s>", value != nullptr ? value : "");
        else         snprintf(vbuf, sizeof vbuf, "%s", value != nullptr ? value : "");
        if (strlen(vbuf) > 13) vbuf[13] = '\0';
        selectRow(lcd, X, y, W, H, lab, vbuf, editing);
        if (labelToo) {
            lcd.fillRect(X, static_cast<uint16_t>(y + H), W, 1, GRID);
            lcd.fillRect(X, static_cast<uint16_t>(y + H + 1), W, 1, BG);
        }
        return;
    }
    unselectRowAt(y);

    char text[20];
    if (labelToo) {
        text[0] = (state == RowState::SELECTED || state == RowState::EDITING) ? '>' : ' ';
        text[1] = ' ';
        size_t n = strlen(label);
        if (n > 11) n = 11;
        memcpy(text + 2, label, n);
        text[2 + n] = '\0';
        textBox(lcd, X, y, LABEL_W, H, 4, 2, text, fgLabel, bg);
        lcd.fillRect(X, static_cast<uint16_t>(y + H), W, 1, GRID);
        lcd.fillRect(X, static_cast<uint16_t>(y + H + 1), W, 1, BG);
    }
    // Value right-aligned, wrapped in < > while editing
    char vbuf[16];
    if (state == RowState::EDITING) snprintf(vbuf, sizeof vbuf, "<%s>", value);
    else                            snprintf(vbuf, sizeof vbuf, "%s", value);
    size_t vn = strlen(vbuf);
    if (vn > 13) vn = 13;
    vbuf[vn] = '\0';
    auto tw = static_cast<uint16_t>(vn * MED_W);
    textBox(lcd, VALUE_X, y, VALUE_W, H, static_cast<uint16_t>(VALUE_W - tw - 6), 2, vbuf, fgValue, bg);
}

void hintLine(Services::ICanvas& lcd, uint16_t y, const char* text, uint16_t color)
{
    lcd.blitTextLine(MARGIN, y, static_cast<uint16_t>(WIDTH - 2 * MARGIN), SMALL_H, text, color, BG);
}

void fillAround(Services::ICanvas& lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                uint16_t tx, uint16_t ty, uint16_t tw, uint16_t th, uint16_t color)
{
    // Clamp the text cell into the box
    if (tx < x) tx = x;
    if (ty < y) ty = y;
    if (tx + tw > x + w) tw = static_cast<uint16_t>(x + w - tx);
    if (ty + th > y + h) th = static_cast<uint16_t>(y + h - ty);
    if (ty > y)                 lcd.fillRect(x, y, w, static_cast<uint16_t>(ty - y), color);                 // above
    if (ty + th < y + h)        lcd.fillRect(x, static_cast<uint16_t>(ty + th), w,
                                             static_cast<uint16_t>(y + h - ty - th), color);                   // below
    if (tx > x)                 lcd.fillRect(x, ty, static_cast<uint16_t>(tx - x), th, color);                 // left
    if (tx + tw < x + w)        lcd.fillRect(static_cast<uint16_t>(tx + tw), ty,
                                             static_cast<uint16_t>(x + w - tx - tw), th, color);               // right
}

void textBox(Services::ICanvas& lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
             uint16_t padX, uint16_t padY, const char* text, uint16_t fg, uint16_t bg)
{
    size_t len = strlen(text);
    size_t maxLen = (w > padX) ? (w - padX) / MED_W : 0;
    if (len > maxLen) len = maxLen;
    char buf[32];
    if (len > sizeof(buf) - 1) len = sizeof(buf) - 1;
    memcpy(buf, text, len);
    buf[len] = '\0';
    auto tw = static_cast<uint16_t>(len * MED_W);
    auto tx = static_cast<uint16_t>(x + padX);
    auto ty = static_cast<uint16_t>(y + padY);
    fillAround(lcd, x, y, w, h, tx, ty, tw, MED_H, bg);
    if (len > 0) lcd.drawString(tx, ty, buf, fg, bg, FONT_MEDIUM);
}

} // namespace Chrome
} // namespace UI
