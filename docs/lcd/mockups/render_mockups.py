"""Vaporwave LCD mock-ups at the real 240x320, drawn with the firmware's own 5x7 font (scale 1/2 and
the 8x12 'medium' doubling rule from lcd_st7789.hpp), colours quantised to RGB565. Output: one PNG per
screen at 3x nearest-neighbour, plus a contact sheet."""
import math, os, re, sys
from PIL import Image, ImageDraw

HDR = "C:/Users/campi/StepperMotorController/server/layers/L4_drivers/devices/lcd_st7789.hpp"
OUT = sys.argv[1] if len(sys.argv) > 1 else "."
os.makedirs(OUT, exist_ok=True)
W, H = 240, 320

# ---------------------------------------------------------------- font from the firmware header
FONT = {}
for m in re.finditer(r"\{(0x[0-9A-Fa-f]{2}), (0x[0-9A-Fa-f]{2}), (0x[0-9A-Fa-f]{2}), (0x[0-9A-Fa-f]{2}), (0x[0-9A-Fa-f]{2})\},\s*// (\d+)", open(HDR).read()):
    FONT[int(m.group(6))] = [int(m.group(i), 16) for i in range(1, 6)]
assert len(FONT) >= 95, len(FONT)


def q565(rgb):
    r, g, b = rgb
    return ((r >> 3) << 3 | (r >> 5), (g >> 2) << 2 | (g >> 6), (b >> 3) << 3 | (b >> 5))


# ---------------------------------------------------------------- palette (all RGB565-representable)
BG = q565((16, 0, 40))          # deep violet-black
PANEL = q565((36, 14, 74))
GRID = q565((60, 30, 110))
GRID_MAJ = q565((110, 60, 180))
PINK = q565((255, 46, 151))
CYAN = q565((46, 242, 255))
VIOLET = q565((155, 92, 255))
ORANGE = q565((255, 140, 60))
YELLOW = q565((255, 214, 63))
WHITE = q565((244, 236, 255))
DIM = q565((120, 100, 160))
BLACK = (0, 0, 0)
SUN = [q565(c) for c in [(255, 60, 140), (255, 90, 120), (255, 120, 100), (255, 150, 80), (255, 190, 70), (255, 220, 90)]]


class Canvas:
    def __init__(self):
        self.im = Image.new("RGB", (W, H), BG)
        self.d = ImageDraw.Draw(self.im)

    def px(self, x, y, c):
        if 0 <= x < W and 0 <= y < H:
            self.im.putpixel((x, y), c)

    def char(self, x, y, ch, fg, bg=None, scale=1):
        g = FONT.get(ord(ch), FONT[32])
        if scale == 3:                                   # the firmware's 8x12 medium font
            COL_SRC = [0, 1, 1, 2, 3, 3, 4]
            ROW_DUP = [2, 2, 1, 2, 1, 2, 1]
            yy = y
            for r in range(7):
                for _ in range(ROW_DUP[r]):
                    for oc in range(7):
                        on = g[COL_SRC[oc]] & (1 << r)
                        if on: self.px(x + oc, yy, fg)
                        elif bg is not None: self.px(x + oc, yy, bg)
                    if bg is not None: self.px(x + 7, yy, bg)
                    yy += 1
            if bg is not None:
                for oc in range(8): self.px(x + oc, yy, bg)
            return 8
        for col in range(5):
            for r in range(7):
                on = g[col] & (1 << r)
                for sx in range(scale):
                    for sy in range(scale):
                        if on: self.px(x + col * scale + sx, y + r * scale + sy, fg)
                        elif bg is not None: self.px(x + col * scale + sx, y + r * scale + sy, bg)
        return 6 * scale

    def text(self, x, y, s, fg, bg=None, scale=1):
        for ch in s:
            x += self.char(x, y, ch, fg, bg, scale)
        return x

    def rect(self, x, y, w, h, c, fill=True):
        if fill: self.d.rectangle([x, y, x + w - 1, y + h - 1], fill=c)
        else: self.d.rectangle([x, y, x + w - 1, y + h - 1], outline=c)

    def line(self, x0, y0, x1, y1, c):
        self.d.line([x0, y0, x1, y1], fill=c)

    def circle(self, cx, cy, r, c, fill=False):
        if fill: self.d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=c)
        else: self.d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=c)

    def save(self, name):
        self.im.resize((W * 3, H * 3), Image.NEAREST).save(os.path.join(OUT, name + ".png"))
        return self.im


def text_w(s, scale):
    return len(s) * (8 if scale == 3 else 6 * scale)


# ---------------------------------------------------------------- shared chrome
def sunset_title(c, title, y=0, h=26):
    """Title band: six horizontal sunset stripes, title in white on top, thin cyan rule under it."""
    for i, col in enumerate(SUN):
        c.rect(0, y + i * (h // 6), W, h // 6 + 1, col)
    c.rect(0, y + h - 2, W, 2, CYAN)
    c.text((W - text_w(title, 2)) // 2, y + 5, title, WHITE, None, 2)


def horizon_grid(c, top, bottom, color=GRID, vanish_x=W // 2):
    """Perspective floor: horizontal lines spaced geometrically, verticals converging on the horizon."""
    span = bottom - top
    n = 6
    for i in range(1, n + 1):
        yy = top + int(span * (i / n) ** 2)
        c.line(0, yy, W - 1, yy, color)
    for k in range(-5, 6):
        xb = vanish_x + k * 40
        c.line(vanish_x, top, xb, bottom, color)
    c.line(0, top, W - 1, top, GRID_MAJ)


def nav_footer(c, left="BACK", center="SELECT", extra=None):
    """Same strip on every screen: LEFT = back, CENTER = select, hold CENTER 2 s = home."""
    y = H - 12
    c.rect(0, y - 1, W, 13, PANEL)
    c.rect(0, y - 1, W, 1, GRID_MAJ)
    x = 3
    c.text(x, y + 2, "<", CYAN, None, 1); x += 7
    x = c.text(x, y + 2, left, WHITE, None, 1) + 10
    c.text(x, y + 2, "o", PINK, None, 1); x += 7
    x = c.text(x, y + 2, center, WHITE, None, 1) + 10
    c.text(x, y + 2, "o", PINK, None, 1); x += 7
    x = c.text(x, y + 2, "2s HOME", WHITE, None, 1)
    if extra:
        c.text(W - text_w(extra, 1) - 3, y + 2, extra, DIM, None, 1)


def status_bar(c, y=28):
    """A one-line status under the title on every screen: link, drive state, faults."""
    c.text(4, y, "USB", CYAN, None, 1)
    c.text(28, y, "RUN 150rpm", WHITE, None, 1)
    c.text(100, y, "CM 1/8", VIOLET, None, 1)
    c.text(W - 46, y, "OK", CYAN, None, 1)
    c.text(W - 30, y, "12.5V", YELLOW, None, 1)
    c.rect(0, y + 10, W, 1, GRID)


# ---------------------------------------------------------------- 1. main menu
def menu():
    c = Canvas()
    sunset_title(c, "SMC  F401")
    status_bar(c)
    items = ["Encoder", "Motion", "Driver", "Telemetry", "Images", "Trace", "Tasks", "Dispatcher",
             "Architecture", "Device Info", "Screen Care"]
    sel = 0
    y0, ih = 42, 18
    for i, name in enumerate(items):
        y = y0 + i * ih
        if i == sel:
            c.rect(6, y - 2, W - 12, ih - 2, PINK)
            c.text(12, y + 2, ">", WHITE, None, 3)
            c.text(26, y + 2, name, WHITE, None, 3)
        else:
            c.text(26, y + 2, name, WHITE, None, 3)
        c.rect(6, y + ih - 3, W - 12, 1, GRID)
    horizon_grid(c, y0 + len(items) * ih + 6, H - 14)
    # a low sun on the horizon line
    hy = y0 + len(items) * ih + 6
    for i, col in enumerate(reversed(SUN)):
        c.d.chord([204 - 18, hy - 18 - i, 204 + 18, hy + 18 - i], 180, 360, fill=col)
    for k in range(3):
        c.rect(204 - 18, hy - 10 + k * 4, 36, 1, BG)
    c.rect(0, hy, W, 1, GRID_MAJ)
    nav_footer(c, "BACK", "OPEN")
    return c.save("1_menu")


# ---------------------------------------------------------------- 2. encoder
def encoder():
    c = Canvas()
    sunset_title(c, "ENCODER")
    status_bar(c)
    cx, cy, r = 120, 128, 70
    # dial: glowing rings
    c.circle(cx, cy, r + 3, GRID_MAJ)
    c.circle(cx, cy, r, CYAN)
    c.circle(cx, cy, r - 1, CYAN)
    for deg in range(0, 360, 10):
        a = math.radians(deg - 90)
        major = deg % 90 == 0
        inner = r - (12 if major else 5)
        col = PINK if major else VIOLET
        c.line(int(cx + inner * math.cos(a)), int(cy + inner * math.sin(a)),
               int(cx + (r - 2) * math.cos(a)), int(cy + (r - 2) * math.sin(a)), col)
    # index mark at the top: filled once seen
    c.circle(cx, cy - r - 8, 3, YELLOW, fill=True)
    # needle at 123 degrees with a trailing sweep (last 30 degrees in dim pink)
    ang = 123
    for d in range(ang - 30, ang, 2):
        a = math.radians(d - 90)
        c.line(int(cx + (r - 20) * math.cos(a)), int(cy + (r - 20) * math.sin(a)),
               int(cx + (r - 14) * math.cos(a)), int(cy + (r - 14) * math.sin(a)), GRID_MAJ)
    a = math.radians(ang - 90)
    c.d.line([cx, cy, int(cx + (r - 14) * math.cos(a)), int(cy + (r - 14) * math.sin(a))], fill=PINK, width=3)
    c.circle(cx, cy, 5, PINK, fill=True)
    c.circle(cx, cy, 2, WHITE, fill=True)
    # centre readouts inside the dial
    c.text(cx - text_w("123.0", 2) // 2, cy + 18, "123.0", WHITE, None, 2)
    c.text(cx + text_w("123.0", 2) // 2 + 2, cy + 18, "o", WHITE, None, 1)
    # readouts below
    y = 212
    rows = [("POS", "1366 cnt", CYAN), ("REV", "0.342", CYAN), ("VEL", "10000 c/s", PINK), ("RPM", "150.0", PINK),
            ("IDX", "seen  4000", YELLOW), ("ERR", "-2 cnt", VIOLET)]
    for i, (k, v, col) in enumerate(rows):
        yy = y + (i // 2) * 16
        xx = 6 + (i % 2) * 120
        c.text(xx, yy, k, col, None, 1)
        c.text(xx + 26, yy, v, WHITE, None, 3)
    # velocity sparkline over the last 10 s
    sy0, sh = 264, 36
    c.rect(6, sy0, W - 12, sh, PANEL)
    for k in range(1, 4):
        c.line(6, sy0 + k * sh // 4, W - 7, sy0 + k * sh // 4, GRID)
    pts = []
    for i in range(W - 12):
        t = i / (W - 12)
        v = 0.5 + 0.35 * math.sin(t * 9) * math.exp(-t * 2) + 0.03 * math.sin(t * 80)
        pts.append((6 + i, sy0 + sh - 2 - int(v * (sh - 4))))
    c.d.line(pts, fill=CYAN)
    c.text(8, sy0 + 2, "vel 10 s", DIM, None, 1)
    nav_footer(c, "BACK", "ZERO")
    return c.save("2_encoder")


# ---------------------------------------------------------------- 3. telemetry graph
def graph():
    c = Canvas()
    sunset_title(c, "TELEMETRY")
    status_bar(c)
    gx, gy, gw, gh = 28, 46, W - 34, 200
    c.rect(gx, gy, gw, gh, BLACK)
    # minor grid every 10 px, major every 50 px
    for x in range(gx, gx + gw, 10):
        c.line(x, gy, x, gy + gh - 1, GRID_MAJ if (x - gx) % 50 == 0 else GRID)
    for y in range(gy, gy + gh, 10):
        c.line(gx, y, gx + gw - 1, y, GRID_MAJ if (y - gy) % 50 == 0 else GRID)
    c.rect(gx, gy, 1, gh, VIOLET); c.rect(gx, gy + gh - 1, gw, 1, VIOLET)
    # y labels
    for i, lab in enumerate(["200", "150", "100", "50", "0"]):
        c.text(2, gy + i * 50 - 3 if i else gy, lab, DIM, None, 1)
    c.text(gx, gy + gh + 2, "-10s", DIM, None, 1)
    c.text(gx + gw - 12, gy + gh + 2, "0", DIM, None, 1)
    # traces: setpoint (pink), measured (cyan), position (yellow)
    def trace(f, col, width=1):
        pts = [(gx + i, gy + gh - 2 - int(f(i / gw) * (gh - 4))) for i in range(gw)]
        c.d.line(pts, fill=col, width=width)
    trace(lambda t: 0.75 if 0.2 < t < 0.8 else 0.1, PINK)
    trace(lambda t: (0.1 + 0.65 * (1 - math.exp(-(t - 0.2) * 25))) if 0.2 < t < 0.8 else
          (0.1 + 0.65 * math.exp(-(t - 0.8) * 25) if t >= 0.8 else 0.1) + 0.012 * math.sin(t * 300), CYAN)
    trace(lambda t: (t * 0.9) % 1.0, YELLOW)
    # legend chips
    ly = gy + gh + 14
    for i, (lab, col) in enumerate([("SET", PINK), ("MEAS", CYAN), ("POS", YELLOW)]):
        x = gx + i * 64
        c.rect(x, ly + 1, 8, 6, col)
        c.text(x + 11, ly, lab, WHITE, None, 1)
    c.text(gx + 192, ly, "1/8 CM", VIOLET, None, 1)
    # live numbers
    c.text(6, ly + 14, "SET 150.0  MEAS 149.6  ERR -0.4 rpm", WHITE, None, 1)
    c.text(6, ly + 26, "UP/DN: scale   RIGHT: trace set", DIM, None, 1)
    nav_footer(c, "BACK", "PAUSE")
    return c.save("3_graph")


# ---------------------------------------------------------------- 4. architecture
def arch():
    c = Canvas()
    sunset_title(c, "ARCHITECTURE")
    status_bar(c)
    # six layer bands, warm at the top (host side) to cool at the bottom (hardware), tasks as columns
    layers = [("L1", "Transport", "UART DMA"), ("L2", "Protocol", "parser,json"), ("L3", "Services", "motion,enc,ui"),
              ("L4", "Drivers", "pStep,enc,lcd"), ("L5", "Board", "spi,tim,dma"), ("F", "Platform", "rtos,clock")]
    cols = [q565(c_) for c_ in [(255, 80, 140), (240, 110, 110), (200, 110, 200), (140, 100, 240), (60, 150, 255), (46, 242, 255)]]
    y, bh = 46, 34
    for i, ((tag, name, what), col) in enumerate(zip(layers, cols)):
        yy = y + i * (bh + 4)
        c.rect(8, yy, W - 16, bh, PANEL)
        c.rect(8, yy, 4, bh, col)
        c.text(18, yy + 3, tag, col, None, 2)
        c.text(50, yy + 3, name, WHITE, None, 3)
        c.text(50, yy + 19, what, DIM, None, 1)
        # live traffic counter per boundary on the right, as a bar
        n = [37, 180, 95, 64, 220, 12][i]
        c.rect(W - 70, yy + 8, 56, 8, BLACK)
        c.rect(W - 70, yy + 8, int(56 * n / 240), 8, col)
        c.text(W - 70, yy + 19, "%d/s" % n, col, None, 1)
    # arrows between bands
    for i in range(5):
        yy = y + i * (bh + 4) + bh
        c.line(30, yy, 30, yy + 4, CYAN); c.line(28, yy + 2, 30, yy + 4, CYAN); c.line(32, yy + 2, 30, yy + 4, CYAN)
    c.text(8, 276, "RIGHT: matrix / flow / tasks", DIM, None, 1)
    horizon_grid(c, 286, H - 14)
    nav_footer(c, "BACK", "PAUSE")
    return c.save("4_architecture")


# ---------------------------------------------------------------- 5. motion control
def motion():
    c = Canvas()
    sunset_title(c, "MOTION")
    status_bar(c)
    # big speed readout
    c.text(8, 46, "SPEED", CYAN, None, 1)
    c.text(8, 56, "150.0", WHITE, None, 2)
    c.text(78, 64, "rpm", DIM, None, 1)
    c.text(130, 46, "TARGET", PINK, None, 1)
    c.text(130, 56, "150.0", WHITE, None, 2)
    c.text(200, 64, "rpm", DIM, None, 1)
    # slider 0..250 rpm with band markers at the resonances (60, 110-120)
    sx, sy, sw = 8, 86, W - 16
    c.rect(sx, sy, sw, 10, PANEL)
    for lo, hi in [(55, 66), (105, 122)]:
        c.rect(sx + int(sw * lo / 250), sy, int(sw * (hi - lo) / 250), 10, q565((120, 30, 70)))
    c.rect(sx, sy + 3, int(sw * 150 / 250), 4, CYAN)
    c.rect(sx + int(sw * 150 / 250) - 2, sy - 2, 4, 14, PINK)
    c.text(sx, sy + 13, "0", DIM, None, 1); c.text(sx + sw - 18, sy + 13, "250", DIM, None, 1)
    c.text(sx + 70, sy + 13, "resonance bands", DIM, None, 1)
    # action list
    items = [("Run CW", True), ("Run CCW", False), ("Stop", False), ("Jog +100", False), ("Go to 0", False),
             ("Enable", False), ("Hi-Z", False), ("E-STOP", False)]
    y0, ih = 116, 18
    for i, (name, sel) in enumerate(items):
        yy = y0 + i * ih
        if sel:
            c.rect(6, yy - 2, 120, ih - 2, PINK)
            c.text(10, yy + 2, ">", WHITE, None, 3)
        c.text(22, yy + 2, name, WHITE if name != "E-STOP" else ORANGE, None, 3)
    # right side: current and coil state
    c.rect(134, 116, 100, 140, PANEL)
    c.text(140, 120, "DRIVE", CYAN, None, 1)
    c.text(140, 130, "current", WHITE, None, 1)
    c.text(140, 144, "RUN  1.72A", WHITE, None, 1)
    c.text(140, 154, "HOLD 0.39A", WHITE, None, 1)
    c.text(140, 168, "COILS", CYAN, None, 1)
    c.text(140, 178, "energised", PINK, None, 1)
    c.text(140, 192, "IDLE REL", CYAN, None, 1)
    c.text(140, 202, "3.0 s", WHITE, None, 1)
    c.text(140, 216, "POS", CYAN, None, 1)
    c.text(140, 226, "1366", WHITE, None, 2)
    c.text(140, 244, "ERR -2", VIOLET, None, 1)
    c.text(8, 266, "UP/DN: choose  RIGHT: +speed", DIM, None, 1)
    c.text(8, 276, "LEFT: back  CENTER: do", DIM, None, 1)
    horizon_grid(c, 288, H - 14)
    nav_footer(c, "BACK", "DO")
    return c.save("5_motion")


# ---------------------------------------------------------------- 6. screen care: saver + retention recovery
def saver():
    c = Canvas()
    # whole screen is the saver: a sun sinking into the grid, drifting left/right so no pixel holds
    horizon_grid(c, 170, H, GRID, vanish_x=150)
    for i, col in enumerate(SUN):
        c.d.chord([150 - 46, 170 - 46 - i * 2, 150 + 46, 170 + 46 - i * 2], 180, 360, fill=col)
    for k in range(3):
        c.rect(150 - 46, 150 + k * 7, 92, 2, BG)
    c.rect(0, 170, W, 1, GRID_MAJ)
    c.text(8, 8, "SMC", PINK, None, 2)
    c.text(8, 26, "idle 4m12s  move any way to wake", DIM, None, 1)
    return c.save("6_saver")


def care():
    c = Canvas()
    sunset_title(c, "SCREEN CARE")
    status_bar(c)
    items = [("Saver after", "2 min", True), ("Saver style", "sunset / blank", False), ("Recovery", "run 5 min", False),
             ("Brightness", "100%", False), ("Invert colours", "off", False)]
    y0, ih = 50, 22
    for i, (k, v, sel) in enumerate(items):
        yy = y0 + i * ih
        if sel:
            c.rect(6, yy - 2, W - 12, ih - 2, PINK)
        c.text(12, yy + 3, k, WHITE, None, 3)
        c.text(W - 12 - text_w(v, 1), yy + 6, v, WHITE if sel else CYAN, None, 1)
    # explanation panel
    y = 168
    c.rect(6, y, W - 12, 90, PANEL)
    for i, s in enumerate(["Recovery cycles the whole panel", "through white, black, inverted and",
                           "the saver for the time chosen, then", "returns here. Use it when a ghost",
                           "of the menu stays on a blank screen.", "", "The saver drifts so nothing holds."]):
        c.text(12, y + 6 + i * 11, s, WHITE if i < 5 else DIM, None, 1)
    horizon_grid(c, 270, H - 14)
    nav_footer(c, "BACK", "CHANGE")
    return c.save("7_screen_care")


ims = [menu(), encoder(), graph(), arch(), motion(), saver(), care()]
names = ["Main menu", "Encoder", "Telemetry", "Architecture", "Motion", "Screen saver", "Screen care"]
S = 2
sheet = Image.new("RGB", ((W * S + 16) * 4 + 16, (H * S + 40) * 2 + 16), (8, 8, 12))
d = ImageDraw.Draw(sheet)
for i, (im, nm) in enumerate(zip(ims, names)):
    x = 16 + (i % 4) * (W * S + 16)
    y = 16 + (i // 4) * (H * S + 40)
    d.text((x, y), nm, fill=(220, 220, 230))
    sheet.paste(im.resize((W * S, H * S), Image.NEAREST), (x, y + 16))
sheet.save(os.path.join(OUT, "lcd_mockups.png"))
print("wrote", OUT)
