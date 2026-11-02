"""Build side-by-side comparison sheets: design | before | after for every view.

Usage: python design_ref/make_compare.py [view ...]
Outputs design_ref/compare_<view>.png and design_ref/compare_all.png
"""
import os
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DESIGN = os.path.join(ROOT, "design_ref")
OUT = os.path.join(ROOT, "sim", "out")
VIEWS = ["launcher", "home", "player", "files", "settings", "uac"]

# design file name -> sim render name
PAIRS = [
    ("launcher", "home"),
    ("player", "player"),
    ("files", "files"),
    ("settings", "settings"),
    ("uac", "uac"),
]

S = 460
PAD = 12
LABEL_H = 24
BG = (11, 12, 15)
FG = (230, 230, 235)


def load(path, size=S):
    if not os.path.exists(path):
        return None
    im = Image.open(path).convert("RGB")
    if im.size != (size, size):
        im = im.resize((size, size), Image.LANCZOS)
    return im


def sheet(design_name, sim_name):
    cols = [("DESIGN", os.path.join(DESIGN, design_name + ".png"))]
    cols.append(("BEFORE", os.path.join(OUT, "before", sim_name + ".png")))
    cols.append(("AFTER", os.path.join(OUT, sim_name + ".png")))
    imgs = [(t, load(p)) for t, p in cols]
    imgs = [(t, i) for t, i in imgs if i is not None]
    if not imgs:
        return None
    w = PAD + len(imgs) * (S + PAD)
    h = PAD + LABEL_H + S + PAD
    canvas = Image.new("RGB", (w, h), BG)
    d = ImageDraw.Draw(canvas)
    x = PAD
    for title, im in imgs:
        d.text((x + 4, PAD + 4), "%s  %s" % (title, sim_name), fill=FG)
        canvas.paste(im, (x, PAD + LABEL_H))
        x += S + PAD
    return canvas


def stack(paths, out_path, cols=1):
    ims = [Image.open(p).convert("RGB") for p in paths]
    w = max(i.width for i in ims)
    h = sum(i.height for i in ims) + PAD * (len(ims) + 1)
    canvas = Image.new("RGB", (w + PAD * 2, h), BG)
    y = PAD
    for i in ims:
        canvas.paste(i, (PAD, y))
        y += i.height + PAD
    canvas.save(out_path)
    return out_path


def main(argv):
    made = []
    for design_name, sim_name in PAIRS:
        sh = sheet(design_name, sim_name)
        if sh is None:
            print("skip %s (missing inputs)" % sim_name)
            continue
        p = os.path.join(DESIGN, "compare_%s.png" % sim_name)
        sh.save(p)
        made.append(p)
        print("wrote", p, sh.size)
    if made:
        allp = stack(made, os.path.join(DESIGN, "compare_all.png"))
        print("wrote", allp)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
