"""Generate LVGL 8 image assets (RGB565 + alpha) from the design's SVG icon paths.

Design source: prototype/index.html (Material filled icons, 24x24 viewBox).
Each icon is rasterised by headless Chrome at 2x then downsampled for clean AA.

Output: firmware/main/ui/ui_icons.c / ui_icons.h  (TRUE_COLOR_ALPHA, LV_COLOR_DEPTH 16)
"""
import base64
import io
import os
import subprocess
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "firmware", "main", "ui")
OUT_C = os.path.join(OUT_DIR, "ui_icons.c")
OUT_H = os.path.join(OUT_DIR, "ui_icons.h")
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
TMP = os.path.join(ROOT, "design_ref", "_icons.png")

CELL = 72  # CSS px per cell, icon centred inside

# name -> (size, svg path d)
ICONS = {
    "music": (26, "M12 3v10.55c-.59-.34-1.27-.55-2-.55-2.21 0-4 1.79-4 4s1.79 4 4 4 4-1.79 4-4V7h4V3h-6z"),
    "folder": (26, "M20 6h-8l-2-2H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2zm0 12H4V8h16v10z"),
    "headset": (26, "M15 2H9c-1.1 0-2 .9-2 2v2H5c-1.1 0-2 .9-2 2v10c0 1.1.9 2 2 2h14c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-2V4c0-1.1-.9-2-2-2zm-6 2h6v2H9V4zm10 16H5V8h14v12z"),
    "gear": (26, "M19.14 12.94c.04-.3.06-.61.06-.94 0-.32-.02-.64-.07-.94l2.03-1.58c.18-.14.23-.41.12-.61l-1.92-3.32c-.12-.22-.37-.29-.59-.22l-2.39.96c-.5-.38-1.03-.7-1.62-.94l-.36-2.54c-.04-.24-.24-.41-.48-.41h-3.84c-.24 0-.43.17-.47.41l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96c-.22-.08-.47 0-.59.22L2.74 8.87c-.12.21-.08.47.12.61l2.03 1.58c-.05.3-.09.63-.09.94s.02.64.07.94l-2.03 1.58c-.18.14-.23.41-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.38 1.03.7 1.62.94l.36 2.54c.05.24.24.41.48.41h3.84c.24 0 .44-.17.47-.41l.36-2.54c.59-.24 1.13-.56 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32c.12-.22.07-.47-.12-.61l-2.01-1.58zM12 15.6c-1.98 0-3.6-1.62-3.6-3.6s1.62-3.6 3.6-3.6 3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z"),
    "prev": (26, "M6 6h2v12H6zm3.5 6l8.5 6V6z"),
    "next": (26, "M6 18l8.5-6L6 6v12zM16 6v12h2V6h-2z"),
    "play": (28, "M8 5v14l11-7z"),
    "pause": (32, "M6 19h4V5H6v14zm8-14v14h4V5h-4z"),
    "home": (18, "M10 20v-6h4v6h5v-8h3L12 3 2 12h3v8z"),
    "volume": (36, "M3 9v6h4l5 5V4L7 9H3zm13.5 3c0-1.77-1.02-3.29-2.5-4.03v8.05c1.48-.73 2.5-2.25 2.5-4.02zM14 3.23v2.06c2.89.86 5 3.54 5 6.71s-2.11 5.85-5 6.71v2.06c4.01-.91 7-4.49 7-8.77s-2.99-7.86-7-8.77z"),
    "note32": (32, "M12 3v10.55c-.59-.34-1.27-.55-2-.55-2.21 0-4 1.79-4 4s1.79 4 4 4 4-1.79 4-4V7h4V3h-6z"),
    "shuffle": (22, "M10.59 9.17L5.41 4 4 5.41l5.17 5.17 1.42-1.41zM14.5 4l2.04 2.04L4 18.59 5.41 20 17.96 7.46 20 9.5V4h-5.5zm.33 9.41l-1.41 1.41 3.13 3.13L14.5 20H20v-5.5l-2.04 2.04-3.13-3.13z"),
    "repeat": (22, "M7 7h10v3l4-4-4-4v3H5v6h2V7zm10 10H7v-3l-4 4 4 4v-3h12v-6h-2v4z"),
    "repeat_one": (22, "M7 7h10v3l4-4-4-4v3H5v6h2V7zm10 10H7v-3l-4 4 4 4v-3h12v-6h-2v4zm-4-2V9h-1l-2 1v1h1.5v4H13z"),
}


def build_html() -> str:
    cells = []
    for name, (size, d) in ICONS.items():
        cells.append(
            '<div class="cell"><svg width="%d" height="%d" viewBox="0 0 24 24" fill="#fff">'
            '<path d="%s"/></svg></div>' % (size, size, d)
        )
    return (
        "<!DOCTYPE html><html><head><meta charset='utf-8'><style>"
        "html,body{margin:0;padding:0;background:transparent;}"
        ".cell{width:%dpx;height:%dpx;display:flex;align-items:center;justify-content:center;}"
        "svg{display:block;}"
        "</style></head><body>%s</body></html>" % (CELL, CELL, "".join(cells))
    )


def render_sheet() -> Image.Image:
    html_path = os.path.join(ROOT, "design_ref", "_icons.html")
    with open(html_path, "w", encoding="utf-8") as f:
        f.write(build_html())
    height = CELL * len(ICONS)
    cmd = [
        CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars",
        "--force-device-scale-factor=2",
        "--window-size=%d,%d" % (CELL, height),
        "--virtual-time-budget=3000",
        "--default-background-color=00000000",
        "--screenshot=%s" % TMP,
        "file:///" + html_path.replace("\\", "/"),
    ]
    subprocess.run(cmd, capture_output=True, timeout=180)
    os.remove(html_path)
    return Image.open(TMP).convert("RGBA")


def to_lvgl_c(name: str, img: Image.Image) -> str:
    """Emit a LV_IMG_CF_TRUE_COLOR_ALPHA map (RGB565 LE + alpha)."""
    img = img.convert("RGBA")
    w, h = img.size
    px = img.load()
    out = []
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            # premultiply-free: white glyph, colour is baked as white
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            out.append(v & 0xFF)
            out.append((v >> 8) & 0xFF)
            out.append(a)
    body = []
    for i in range(0, len(out), 16):
        body.append("    " + ",".join("0x%02X" % b for b in out[i:i + 16]) + ",")
    return (
        "static const uint8_t %s_map[] = {\n%s\n};\n"
        "const lv_img_dsc_t %s = {\n"
        "    .header.always_zero = 0,\n"
        "    .header.w = %d,\n"
        "    .header.h = %d,\n"
        "    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,\n"
        "    .data_size = sizeof(%s_map),\n"
        "    .data = %s_map,\n"
        "};\n" % (name, "\n".join(body), name, w, h, name, name)
    )


def main() -> int:
    if not os.path.exists(CHROME):
        print("chrome not found")
        return 1
    sheet = render_sheet()
    print("sheet", sheet.size)
    parts = []
    decls = []
    idx = 0
    for name, (size, _d) in ICONS.items():
        # crop the centred icon from its cell
        cy = CELL * 2 * idx          # 2x screenshot: one cell = CELL*2 px tall
        left = CELL - size           # (CELL*2 - size*2) // 2
        top = cy + CELL - size
        tile = sheet.crop((left, top, left + size * 2, top + size * 2))
        tile = tile.resize((size, size), Image.LANCZOS)
        full = "ui_icon_%s" % name
        parts.append(to_lvgl_c(full, tile))
        decls.append("LV_IMG_DECLARE(%s);" % full)
        idx += 1

    header = (
        "/**\n"
        " * FAKE_POD_NANO UI icon assets (generated).\n"
        " * 由 design_ref/gen_icons.py 从 prototype/index.html 的 Material 图标 SVG 路径生成，\n"
        " * 格式：LV_IMG_CF_TRUE_COLOR_ALPHA + LV_COLOR_DEPTH=16（RGB565 + Alpha）。\n"
        " * 重新生成：python design_ref/gen_icons.py\n"
        " */\n"
        "#pragma once\n\n#include \"lvgl.h\"\n\n"
        "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n"
        + "\n".join(decls) +
        "\n\n#ifdef __cplusplus\n}\n#endif\n"
    )
    with open(OUT_H, "w", encoding="utf-8", newline="\n") as f:
        f.write(header)
    with open(OUT_C, "w", encoding="utf-8", newline="\n") as f:
        f.write(
            "/**\n * FAKE_POD_NANO UI icon assets (generated by design_ref/gen_icons.py)\n"
            " * LV_IMG_CF_TRUE_COLOR_ALPHA, RGB565 + alpha. White glyphs, use img_recolor.\n */\n"
            "#include \"ui_icons.h\"\n\n" + "\n".join(parts)
        )
    print("wrote", OUT_C, OUT_H)
    for f in (TMP,):
        if os.path.exists(f):
            os.remove(f)
    return 0


if __name__ == "__main__":
    sys.exit(main())
