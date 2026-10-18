/**
 * @file chrome.hpp
 * @brief The frame every chrome screen sits in: title tab, status line, navigation footer,
 *        and the drawing helpers screens share (text boxes without a double paint, corner
 *        brackets, scanline decks).
 *
 * The ScreenManager draws the tab and the footer once per transition and the status line
 * when it changes; screens draw between Theme::CONTENT_Y and Theme::CONTENT_BOTTOM.
 */
#pragma once

#include "harness/defs/harness_l3_l4.hpp"
#include <stdint.h>

namespace UI {

/// What the status line under the title shows. Filled by the DisplayService each tick.
struct StatusLine {
    char     left[20];      ///< drive state and speed, e.g. "RUN 150rpm"
    uint16_t leftColor;
    char     mid[12];       ///< drive and step mode, e.g. "CM 1/8"
    uint16_t midColor;
    char     right[8];      ///< role or health, e.g. "FL" / "OK"
    uint16_t rightColor;
};

namespace Chrome {

/// Angular cyan tab with the title cut into it, neon rule underneath with a magenta end
void drawTitleBand(Services::ICanvas& lcd, const char* title);

/// The status line (small font) and its rule; paints its whole strip, so it can be redrawn
void drawStatusBar(Services::ICanvas& lcd, const StatusLine& s);

/// "< BACK   o <center>   o 2s HOME"; the back hint is left out at the root
void drawFooter(Services::ICanvas& lcd, bool canGoBack, const char* centerHint);

/// Scanlines between two rows with a bracketed frame: filler for the empty end of a screen
void drawDeck(Services::ICanvas& lcd, uint16_t top, uint16_t bottom);

/// Corner brackets of a box (HUD frame), `len` pixels along each edge
void drawCorners(Services::ICanvas& lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 uint16_t len, uint16_t color);

/// Fill the box (x, y, w, h) with `color` except the text cell (tx, ty, tw, th), which the
/// caller paints with the text itself. No pixel is painted twice, so a selection bar does
/// not flash.
void fillAround(Services::ICanvas& lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                uint16_t tx, uint16_t ty, uint16_t tw, uint16_t th, uint16_t color);

/// A medium-font (8x12) text box: the text with `bg` behind it, then `bg` around it up to
/// (x, y, w, h). Text is clipped to the box width.
void textBox(Services::ICanvas& lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
             uint16_t padX, uint16_t padY, const char* text, uint16_t fg, uint16_t bg);

} // namespace Chrome
} // namespace UI
