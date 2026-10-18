/**
 * @file theme.hpp
 * @brief The LCD palette and the fixed layout every screen shares.
 *
 * One place for the look: screens take colours and the band/status/footer geometry from
 * here, so the palette is a one-file change. Colours are RGB565 (what the panel takes).
 * The look is cyberpunk HUD: near-black, neon cyan and magenta, angular tabs, corner
 * brackets, scanlines; tests/test_screen_snapshots.cpp renders it on the host.
 */
#pragma once

#include "harness/defs/harness_l3_l4.hpp"
#include <stdint.h>

namespace UI {
namespace Theme {

// --- palette -----------------------------------------------------------------------
constexpr uint16_t BG       = 0x0842;   ///< near-black with a blue cast, the page
constexpr uint16_t PANEL    = 0x10C6;   ///< raised boxes, footer
constexpr uint16_t SCAN     = 0x0884;   ///< scanline, one shade over the page
constexpr uint16_t GRID     = 0x21CB;   ///< minor grid, separators
constexpr uint16_t GRID_MAJ = 0x3B31;   ///< major grid, rules
constexpr uint16_t CYAN     = 0x079F;   ///< primary neon: tabs, labels, measured values
constexpr uint16_t MAGENTA  = 0xF8DB;   ///< secondary neon: selection, needles, setpoints
constexpr uint16_t VIOLET   = 0x9A3F;   ///< tertiary labels
constexpr uint16_t YELLOW   = 0xFF20;   ///< hazard: warnings, E-STOP, index marks
constexpr uint16_t ORANGE   = 0xFC00;   ///< hot values
constexpr uint16_t GREEN    = 0x2FE8;   ///< running, OK
constexpr uint16_t RED      = 0xF906;   ///< faults
constexpr uint16_t TEXT     = 0xDF3E;   ///< body text (cool white)
constexpr uint16_t DIM      = 0x4A8D;   ///< hints, disabled items
constexpr uint16_t BLACK    = 0x0000;   ///< plot interiors
constexpr uint16_t PINK     = MAGENTA;  ///< alias kept for the selection bar

// --- layout (portrait 240 x 320) ----------------------------------------------------
constexpr uint16_t WIDTH          = 240;
constexpr uint16_t HEIGHT         = 320;
constexpr uint16_t TITLE_H        = 26;    ///< title tab 0..23, rule 24..25
constexpr uint16_t STATUS_Y       = 28;    ///< one small-font line, rule under it at 38
constexpr uint16_t STATUS_H       = 8;
constexpr uint16_t CONTENT_Y      = 42;    ///< first row a screen may draw on
constexpr uint16_t FOOTER_Y       = 307;   ///< rule at 307, text at 310
constexpr uint16_t FOOTER_H       = 13;
constexpr uint16_t CONTENT_BOTTOM = FOOTER_Y - 1;   ///< last row a screen may draw on
constexpr uint16_t MARGIN         = 6;

// --- fonts (ICanvas scale codes) -------------------------------------------------------
constexpr uint8_t  FONT_SMALL  = 1;   ///< 6 x 8
constexpr uint8_t  FONT_LARGE  = 2;   ///< 12 x 16
constexpr uint8_t  FONT_MEDIUM = 3;   ///< 8 x 12
constexpr uint8_t  SMALL_W = 6, SMALL_H = 8, LARGE_W = 12, LARGE_H = 16, MED_W = 8, MED_H = 12;

} // namespace Theme
} // namespace UI
