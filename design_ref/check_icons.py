"""Render the generated LVGL icon assets back to a PNG sheet for visual QA."""
import os
import re

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "firmware", "main", "ui", "ui_icons.c")
OUT = os.path.join(ROOT, "design_ref", "_icon_check.png")

src = open(SRC, encoding="utf-8").read()
blocks = re.findall(
    r"static const uint8_t (ui_icon_\w+)_map\[\] = \{(.*?)\};\s*"
    r"const lv_img_dsc_t .*?header\.w = (\d+),\s*\.header\.h = (\d+)",
    src, re.S)
print("blocks:", [(b[0], b[2], b[3]) for b in blocks])

W = sum(int(b[2]) for b in blocks) + 10 * (len(blocks) + 1)
H = max(int(b[3]) for b in blocks) + 20
sheet = Image.new("RGBA", (W, H), (18, 18, 22, 255))
x = 10
for name, data, w, h in blocks:
    vals = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", data)]
    w, h = int(w), int(h)
    img = Image.new("RGBA", (w, h))
    px = img.load()
    for i in range(w * h):
        v = vals[i * 3] | (vals[i * 3 + 1] << 8)
        r = ((v >> 11) & 0x1F) * 255 // 31
        g = ((v >> 5) & 0x3F) * 255 // 63
        b = (v & 0x1F) * 255 // 31
        a = vals[i * 3 + 2]
        px[i % w, i // w] = (r, g, b, a)
    sheet.alpha_composite(img, (x, 10))
    x += w + 10
sheet = sheet.resize((W * 3, H * 3), Image.NEAREST)
sheet.save(OUT)
print("wrote", OUT, sheet.size)
