"""Generate real CJK LVGL fonts (ui_font_cjk_16 / ui_font_cjk_20) with lv_font_conv.

Why: the repo's original CJK fonts were built from a hand-picked symbol list, so any
new UI copy (lu jing / shou qu mu / jun heng qi / zhuang tai xin xi ...) rendered as
empty boxes. This script collects every character the UI sources and the design
prototype actually use, then renders two fonts with DengXian
(regular 16px body, bold 20px titles).

Usage: python design_ref/gen_cjk_font.py
"""
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UI_DIR = os.path.join(ROOT, "firmware", "main", "ui")
FONT_DIR = os.path.join(UI_DIR, "fonts")
OUT16 = os.path.join(FONT_DIR, "ui_font_cjk_16.c")
OUT20 = os.path.join(FONT_DIR, "ui_font_cjk_20.c")
NODE = r"C:\Program Files\nodejs\node.exe"
NPM_CMD = r"C:\Program Files\nodejs\npm.cmd"
CONV = os.path.join(ROOT, "design_ref", "node_modules", "lv_font_conv", "lv_font_conv.js")
FONT_REG = r"C:\Windows\Fonts\Deng.ttf"     # DengXian regular
FONT_BOLD = r"C:\Windows\Fonts\Dengb.ttf"   # DengXian bold

RANGES = [
    (0x0020, 0x007E),   # ASCII
    (0x00B7, 0x00B7),   # middle dot
    (0x2000, 0x206F),   # general punctuation
    (0x2190, 0x21FF),   # arrows
    (0x25A0, 0x25FF),   # geometric shapes
    (0x3000, 0x303F),   # CJK punctuation
    (0x4E00, 0x9FFF),   # CJK unified
    (0xFF00, 0xFFEF),   # fullwidth
]


def wanted(ch):
    cp = ord(ch)
    return any(lo <= cp <= hi for lo, hi in RANGES)


def collect():
    chars = set()
    sources = []
    for pat in ("*.c", "*.cpp", "*.h"):
        sources += glob.glob(os.path.join(UI_DIR, pat))
    proto = os.path.join(ROOT, "prototype", "index.html")
    if os.path.exists(proto):
        sources.append(proto)
    for path in sources:
        try:
            with open(path, "r", encoding="utf-8", errors="ignore") as f:
                text = f.read()
        except OSError:
            continue
        for ch in text:
            if wanted(ch):
                chars.add(ch)
    return "".join(sorted(chars))


def run_conv(font_path, size, out_c, symbols):
    cmd = [
        NODE, CONV,
        "--font", font_path,
        "--size", str(size),
        "--bpp", "4",
        "--format", "lvgl",
        "--lv-include", "lvgl.h",
        "--no-compress",
        "--no-kerning",
        "--symbols", symbols,
        "-o", out_c,
    ]
    subprocess.run(cmd, check=True, cwd=ROOT)
    print("wrote", out_c, os.path.getsize(out_c), "bytes")


def main():
    if not os.path.exists(CONV):
        print("installing lv_font_conv into design_ref/ ...")
        subprocess.run([NPM_CMD, "install", "--no-audit", "--no-fund", "--silent",
                        "lv_font_conv@1.5.3"], check=True,
                       cwd=os.path.join(ROOT, "design_ref"), shell=True)
    symbols = collect()
    cjk = [c for c in symbols if ord(c) > 0x7E]
    print("symbols total=%d non-ascii=%d" % (len(symbols), len(cjk)))
    print("cjk sample:", "".join(cjk[:60]))
    run_conv(FONT_REG, 16, OUT16, symbols)
    run_conv(FONT_BOLD, 20, OUT20, symbols)
    return 0


if __name__ == "__main__":
    sys.exit(main())
