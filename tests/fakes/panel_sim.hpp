/**
 * @file panel_sim.hpp
 * @brief A simulated ST7789 panel behind the L4 LCD driver: decodes the SPI command stream
 *        (CASET/RASET/RAMWR/RAMWRC, data when D/C is high) into a 240x320 RGB565 frame.
 *
 * The real driver runs unchanged, so a snapshot is exactly the pixel stream the panel receives,
 * Display inversion as the ST7789 does it: off after reset, INVON (0x21) / INVOFF (0x20) switch
 * it, and while it is on every pixel shows inverted. A drawing path that sends the wrong
 * polarity shows up as wrong colours in the frame.
 * writePpm() dumps the frame for a look; scripts/lcd/snapshots_to_png.py converts the folder.
 */
#pragma once

#include "L5_board/boundary/ispi_bus.hpp"
#include "L5_board/boundary/igpio.hpp"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

namespace fakes {

struct PanelSim : Board::ISPIBus {
    static constexpr uint16_t W = 240;
    static constexpr uint16_t H = 320;

    explicit PanelSim(const Board::IGPIO& dc) : m_dc(dc) { memset(frame, 0, sizeof frame); }

    uint16_t frame[H][W];          ///< RGB565 as the panel shows it
    uint32_t pixelsWritten = 0;    ///< every pixel the driver sent, for cost accounting
    uint32_t windows = 0;          ///< RAMWR count: one per drawing call that opens a window
    uint32_t cmds = 0;
    bool asleep = false;        ///< SLPIN .. SLPOUT
    bool displayOn = true;      ///< DISPON / DISPOFF
    bool inverted = false;      ///< INVON .. INVOFF (off after reset)

    void lock() override {}
    void unlock() override {}
    void setMode(Board::SPIMode m) override { (void)m; }
    Board::SPIMode getMode() const override { return Board::SPIMode::Mode0; }

    uint8_t transfer(uint8_t b) override {
        if (!m_dc.read()) { command(b); return 0; }
        data(b);
        return 0;
    }
    void transfer(const uint8_t* tx, uint8_t* rx, size_t len) override {
        for (size_t i = 0; i < len; i++) { uint8_t r = transfer(tx ? tx[i] : 0); if (rx) rx[i] = r; }
    }

    uint16_t px(uint16_t x, uint16_t y) const { return frame[y][x]; }

    /// Count of pixels in a rectangle with exactly this colour
    uint32_t countColor(uint16_t color, uint16_t x = 0, uint16_t y = 0, uint16_t w = W, uint16_t h = H) const {
        uint32_t n = 0;
        for (uint16_t yy = y; yy < y + h && yy < H; yy++)
            for (uint16_t xx = x; xx < x + w && xx < W; xx++)
                if (frame[yy][xx] == color) n++;
        return n;
    }

    bool writePpm(const char* path) const {
        FILE* f = fopen(path, "wb");
        if (!f) return false;
        fprintf(f, "P6\n%u %u\n255\n", W, H);
        for (uint16_t y = 0; y < H; y++) {
            for (uint16_t x = 0; x < W; x++) {
                uint16_t c = frame[y][x];
                uint8_t rgb[3] = { static_cast<uint8_t>(((c >> 11) & 0x1F) * 255 / 31),
                                   static_cast<uint8_t>(((c >> 5) & 0x3F) * 255 / 63),
                                   static_cast<uint8_t>((c & 0x1F) * 255 / 31) };
                fwrite(rgb, 1, 3, f);
            }
        }
        fclose(f);
        return true;
    }

private:
    const Board::IGPIO& m_dc;
    uint8_t m_cmd = 0;
    uint8_t m_args[4] = {};
    uint8_t m_nargs = 0;
    uint16_t m_x0 = 0, m_x1 = W - 1, m_y0 = 0, m_y1 = H - 1;
    uint16_t m_cx = 0, m_cy = 0;
    bool m_hiByte = true;
    uint8_t m_hi = 0;

    void command(uint8_t b) {
        m_cmd = b; m_nargs = 0; cmds++;
        if (b == 0x10) asleep = true;                                              // SLPIN
        if (b == 0x11) asleep = false;                                             // SLPOUT
        if (b == 0x28) displayOn = false;                                          // DISPOFF
        if (b == 0x29) displayOn = true;                                           // DISPON
        if (b == 0x20) inverted = false;                                           // INVOFF
        if (b == 0x21) inverted = true;                                            // INVON
        if (b == 0x2C) { m_cx = m_x0; m_cy = m_y0; m_hiByte = true; windows++; }   // RAMWR
        if (b == 0x3C) { m_hiByte = true; }                                        // RAMWRC
    }

    void data(uint8_t b) {
        switch (m_cmd) {
            case 0x2A: case 0x2B:
                if (m_nargs < 4) m_args[m_nargs++] = b;
                if (m_nargs == 4) {
                    uint16_t a = static_cast<uint16_t>((m_args[0] << 8) | m_args[1]);
                    uint16_t e = static_cast<uint16_t>((m_args[2] << 8) | m_args[3]);
                    if (m_cmd == 0x2A) { m_x0 = a; m_x1 = e; } else { m_y0 = a; m_y1 = e; }
                }
                break;
            case 0x2C: case 0x3C:
                if (m_hiByte) { m_hi = b; m_hiByte = false; return; }
                m_hiByte = true;
                put(static_cast<uint16_t>(inverted ? ~((m_hi << 8) | b) : ((m_hi << 8) | b)));
                break;
            default:
                break;
        }
    }

    void put(uint16_t color) {
        pixelsWritten++;
        if (m_cx < W && m_cy < H) frame[m_cy][m_cx] = color;
        if (m_cx >= m_x1) { m_cx = m_x0; if (m_cy < m_y1) m_cy++; else m_cy = m_y0; }
        else m_cx++;
    }
};

} // namespace fakes
