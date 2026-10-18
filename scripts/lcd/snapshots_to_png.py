"""Convert the PPM screen snapshots that test_screen_snapshots writes into PNGs (3x, nearest
neighbour so the pixels stay crisp) and one contact sheet.

    python scripts/lcd/snapshots_to_png.py build-tests/screens [out_dir]
"""
import os
import sys

from PIL import Image, ImageDraw

src = sys.argv[1]
out = sys.argv[2] if len(sys.argv) > 2 else src
os.makedirs(out, exist_ok=True)
names = sorted(n[:-4] for n in os.listdir(src) if n.endswith(".ppm"))
ims = []
for n in names:
    im = Image.open(os.path.join(src, n + ".ppm")).convert("RGB")
    im.resize((im.width * 3, im.height * 3), Image.NEAREST).save(os.path.join(out, n + ".png"))
    ims.append((n, im))
if ims:
    s, w, h = 2, ims[0][1].width, ims[0][1].height
    cols = min(4, len(ims))
    rows = (len(ims) + cols - 1) // cols
    sheet = Image.new("RGB", (16 + cols * (w * s + 16), 16 + rows * (h * s + 36)), (8, 8, 12))
    d = ImageDraw.Draw(sheet)
    for i, (n, im) in enumerate(ims):
        x, y = 16 + (i % cols) * (w * s + 16), 16 + (i // cols) * (h * s + 36)
        d.text((x, y), n, fill=(220, 220, 230))
        sheet.paste(im.resize((w * s, h * s), Image.NEAREST), (x, y + 16))
    sheet.save(os.path.join(out, "sheet.png"))
print("wrote %d snapshots to %s" % (len(ims), out))
