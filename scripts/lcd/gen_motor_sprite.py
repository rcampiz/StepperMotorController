"""
Generate the saver's pixel-art stepper for the LCD: the bench motor, a StepperOnline
23HS22-2804-ME1K (NEMA 23; its magnetic encoder sits inside the rear cap, so nothing shows of it
from the front; from the back, its raised cover and wires), as server/layers/L3_services/ui/screens/saver_motor_sprite.hpp, and a preview PNG.

Drawn after the owner's CAD render (2026-10-22) at the panel's own resolution, in an oblique
view: the front face square to the viewer on the right (light aluminium cap, four mounting holes,
the side screws, the raised boss) with the shaft coming toward the viewer, and the depth receding
up and to the left: the front cap, the black body, the rear cap. Hotline Miami rim lights: cyan
along the top and left of the silhouette, hot pink along its bottom.

The depth is painted back to front as square slices, each one pixel up and left of the next:
what a nearer slice leaves of a farther one is its top row (the top face) and its left column
(the side).

Run from the repository root:  python scripts/lcd/gen_motor_sprite.py [preview.png]
"""

import math
import os
import sys

PALETTE = [
    ("clear", (0, 0, 0)),
    ("outline", (10, 0, 18)),
    ("cyan", (5, 217, 232)),
    ("pink", (255, 41, 117)),
    ("cap_hi", (246, 246, 249)),
    ("face", (224, 224, 229)),
    ("cap_side", (192, 192, 199)),
    ("cap_shade", (156, 156, 165)),
    ("clip", (40, 40, 46)),
    ("body_top", (78, 78, 85)),
    ("body", (30, 30, 34)),
    ("body_edge", (54, 54, 60)),
    ("wire_green", (90, 170, 60)),
    ("wire_brown", (130, 80, 40)),
    ("wire_yellow", (220, 200, 60)),
    ("shade", (70, 70, 78)),
]
IDX = {name: i for i, (name, _) in enumerate(PALETTE)}

F = 50                      # the front face: 57 mm at ~0.88 px/mm
CH = 4                      # the caps' corner chamfers
CAP = 4                     # a cap's depth in slices (the oblique halves depth)
BODY = 17                   # the black body's
BODY_INSET, BODY_CH = 1, 4  # the body sits a pixel in
DEPTH = CAP + BODY + CAP
CABLE = 9                   # the encoder's wires below the cap
W = F + DEPTH + 3
H = F + DEPTH + CABLE + 3
X0, Y0 = DEPTH + 1, DEPTH + 1   # the front face's top-left


def draw():
    g = [[0] * W for _ in range(H)]

    def px(x, y, name):
        if 0 <= x < W and 0 <= y < H:
            g[y][x] = IDX[name]

    def inside(x, y, x0, y0, size, ch):
        lx, ly = x - x0, y - y0
        if lx < 0 or ly < 0 or lx >= size or ly >= size:
            return False
        r = size - 1
        return not (lx + ly < ch or (r - lx) + ly < ch or lx + (r - ly) < ch or (r - lx) + (r - ly) < ch)

    def slice_(k, inset, ch, top, side, edge=None):
        """The cross-section k slices back: up and left of the face."""
        size = F - 2 * inset
        x0, y0 = X0 - k + inset, Y0 - k + inset
        for y in range(y0, y0 + size):
            for x in range(x0, x0 + size):
                if inside(x, y, x0, y0, size, ch):
                    px(x, y, side)
        for x in range(x0, x0 + size):          # the top row: what the top face shows of it
            for y in range(y0, y0 + ch + 1):
                if inside(x, y, x0, y0, size, ch):
                    px(x, y, top)
                    break
        if edge:                                # a seam: the slice's whole outline
            for y in range(y0, y0 + size):
                for x in range(x0, x0 + size):
                    if inside(x, y, x0, y0, size, ch) and not all(
                            inside(x + dx, y + dy, x0, y0, size, ch) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                        px(x, y, edge)

    # back to front: the rear cap, the black body, the front cap
    for k in range(DEPTH - 1, CAP + BODY - 1, -1):
        slice_(k, 0, CH, "cap_hi", "cap_side", edge="cap_shade" if k == CAP + BODY else None)
    for k in range(CAP + BODY - 1, CAP - 1, -1):
        slice_(k, BODY_INSET, BODY_CH, "body_top", "body")
    for k in range(CAP - 1, 0, -1):
        slice_(k, 0, CH, "cap_hi", "cap_side", edge="cap_shade" if k == CAP - 1 else None)

    # the rear face (toward the viewer): the cap, a bevel, the encoder's raised cover, its wires
    for y in range(Y0, Y0 + F):
        for x in range(X0, X0 + F):
            if inside(x, y, X0, Y0, F, CH):
                px(x, y, "face")
    for i in range(F):                           # lit along the top and left, shaded below and right
        for x, y, c in ((X0 + i, Y0, "cap_hi"), (X0, Y0 + i, "cap_hi"),
                        (X0 + i, Y0 + F - 1, "cap_shade"), (X0 + F - 1, Y0 + i, "cap_shade")):
            if inside(x, y, X0, Y0, F, CH):
                px(x, y, c)
    cx, cy = X0 + (F - 1) / 2.0, Y0 + (F - 1) / 2.0 - 2
    for y in range(Y0, Y0 + F):                  # the encoder's cover: a raised disc, its edge lit above
        for x in range(X0, X0 + F):              # left, shaded below right, a shadow cast on the cap
            d = math.hypot(x - cx, y - cy)
            ds = math.hypot(x - cx - 1.5, y - cy - 1.5)
            if d <= 16.5:
                lit = (x - cx) + (y - cy) < 0
                px(x, y, ("cap_hi" if lit else "cap_shade") if d > 15.3 else "cap_side")
            elif ds <= 16.5:
                px(x, y, "cap_shade")
    # the wires: green, brown, yellow out of the cover's lower edge, through the clip at the cap's edge
    wx = int(cx) + 6
    top = int(cy + math.sqrt(16.5 ** 2 - 6 ** 2))
    for i, colour in enumerate(("wire_green", "wire_brown", "wire_yellow")):
        for y in range(top, Y0 + F + CABLE):
            px(wx + i, y, colour)
    for y in range(Y0 + F - 6, Y0 + F - 1):      # the clip
        for x in range(wx - 2, wx + 5):
            if y in (Y0 + F - 6, Y0 + F - 2) or x in (wx - 2, wx + 4):
                px(x, y, "clip")
    # the rim lights: the silhouette's top and left edges cyan, its bottom edge hot pink
    def clear(x, y):
        return not (0 <= x < W and 0 <= y < H) or g[y][x] == 0
    rim = [row[:] for row in g]
    for y in range(H):
        for x in range(W):
            if g[y][x] == 0:
                continue
            if clear(x, y - 1) or clear(x - 1, y):
                rim[y][x] = IDX["cyan"]
            elif clear(x, y + 1):
                rim[y][x] = IDX["pink"]
    # the outline: every clear pixel beside the motor
    out = [row[:] for row in rim]
    for y in range(H):
        for x in range(W):
            if rim[y][x] == 0 and any(not clear(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                out[y][x] = IDX["outline"]
    return out


def rgb565(c):
    r, g, b = c
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def header(grid):
    rows = []
    for row in grid:
        packed = [(row[i] << 4) | (row[i + 1] if i + 1 < W else 0) for i in range(0, W, 2)]
        rows.append("    {" + ", ".join("0x%02X" % b for b in packed) + "},")
    pal = ", ".join("0x%04X" % rgb565(c) for _, c in PALETTE)
    return ("/**\n * @file saver_motor_sprite.hpp\n * @brief GENERATED by scripts/lcd/gen_motor_sprite.py: the saver's pixel-art stepper, the\n"
            " *        bench's StepperOnline 23HS22-2804-ME1K, %d x %d, 4 bits a pixel (index 0 clear),\n"
            " *        drawn at the panel's resolution.\n */\n"
            "#pragma once\n#include <stdint.h>\n\nnamespace UI {\nnamespace MotorSprite {\n\n"
            "constexpr int16_t W = %d, H = %d;\n"
            "constexpr uint16_t PALETTE[16] = {%s};\n"
            "constexpr uint8_t PIXELS[H][(W + 1) / 2] = {\n%s\n};\n\n"
            "/// The palette index at (x, y), 0 = clear\n"
            "inline uint8_t at(int16_t x, int16_t y) {\n"
            "    if (x < 0 || y < 0 || x >= W || y >= H) return 0;\n"
            "    const uint8_t b = PIXELS[y][x >> 1];\n"
            "    return (x & 1) ? static_cast<uint8_t>(b & 0x0F) : static_cast<uint8_t>(b >> 4);\n"
            "}\n\n} // namespace MotorSprite\n} // namespace UI\n"
            % (W, H, W, H, pal, "\n".join(rows)))


def preview(grid, path, scale=5):
    from PIL import Image
    im = Image.new("RGB", (W * scale, H * scale), (20, 6, 40))
    pix = im.load()
    for y in range(H):
        for x in range(W):
            if grid[y][x]:
                c = PALETTE[grid[y][x]][1]
                for dy in range(scale):
                    for dx in range(scale):
                        pix[x * scale + dx, y * scale + dy] = c
    im.save(path)


if __name__ == "__main__":
    grid = draw()
    out = os.path.join("server", "layers", "L3_services", "ui", "screens", "saver_motor_sprite.hpp")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(header(grid))
    if len(sys.argv) > 1:
        preview(grid, sys.argv[1])
    print("wrote", out, W, "x", H)
