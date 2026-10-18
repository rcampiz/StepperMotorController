/**
 * @file chrome.cpp
 * @brief Title tab, status line, footer and shared drawing helpers.
 */

#include "ui/chrome/chrome.hpp"
#include "ui/theme.hpp"
#include <string.h>

namespace UI {
namespace Chrome {

using namespace Theme;

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
