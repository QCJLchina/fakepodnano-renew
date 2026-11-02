"""Render each prototype view as an exact 460x460 PNG (design source of truth).

The HTML prototype in prototype/index.html is the design mock. This script strips the
device chrome / control panel and activates one view at a time, then screenshots the
460x460 screen with headless Chrome at 2x for crisp inspection.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "prototype", "index.html")
OUT = os.path.dirname(os.path.abspath(__file__))
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"

VIEWS = [
    ("launcher", "pageLauncher", 460),
    ("player", "pagePlayer", 460),
    ("files", "pageFiles", 460),
    ("uac", "pageUac", 460),
    ("settings", "pageSettings", 460),
    ("about", "pageAbout", 460),
]

OVERRIDES = """
<style id="refshot">
  html, body { background:#000 !important; margin:0 !important; padding:0 !important;
               display:block !important; min-height:0 !important; overflow:hidden !important; }
  .top-controls, .ruler-note { display:none !important; }
  .device-body { background:#000 !important; padding:0 !important; border-radius:0 !important;
                 box-shadow:none !important; transform:none !important; width:460px !important;
                 height:460px !important; overflow:hidden !important; }
  .btn-hw { display:none !important; }
  .screen { border-radius:0 !important; box-shadow:none !important; width:460px !important;
            height:460px !important; }
  .screen-sleep-overlay, .vol-hud, .toast-msg { display:none !important; }
</style>
"""


def build_page(html: str, page_id: str) -> str:
    # Drop the `active` class from every view page, then re-add it to the target view.
    html = re.sub(r'class="view-page([^"]*)"', r'class="view-page\1"', html)
    html = html.replace('class="view-page active"', 'class="view-page"')
    html = html.replace('id="%s"' % page_id, 'id="%s" data-target="1"' % page_id, 1)
    html = re.sub(r'class="view-page([^"]*)"([^>]*data-target="1")',
                  r'class="view-page\1 active"\2', html)
    inject = (
        "<script>window.addEventListener('load',function(){"
        "document.querySelectorAll('.view-page').forEach(function(e){e.classList.remove('active');});"
        "var t=document.getElementById('%s'); if(t){t.classList.add('active');}"
        "var s=document.getElementById('sleepOverlay'); if(s){s.style.opacity=0; s.style.display='none';}"
        "});</script>" % page_id
    )
    html = html.replace("</head>", OVERRIDES + "</head>")
    html = html.replace("</body>", inject + "</body>")
    return html


def main() -> int:
    if not os.path.exists(CHROME):
        print("chrome not found: %s" % CHROME)
        return 1
    with open(SRC, "r", encoding="utf-8") as f:
        html = f.read()
    made = 0
    for name, page_id, size in VIEWS:
        if 'id="%s"' % page_id not in html:
            print("skip %-9s (no element %s)" % (name, page_id))
            continue
        page = build_page(html, page_id)
        tmp = os.path.join(OUT, "_%s.html" % name)
        with open(tmp, "w", encoding="utf-8") as f:
            f.write(page)
        png = os.path.join(OUT, "%s.png" % name)
        cmd = [
            CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars",
            "--force-device-scale-factor=2",
            "--window-size=%d,%d" % (size, size),
            "--virtual-time-budget=3000",
            "--default-background-color=00000000",
            "--screenshot=%s" % png,
            "file:///" + tmp.replace("\\", "/"),
        ]
        subprocess.run(cmd, capture_output=True, timeout=120)
        ok = os.path.exists(png) and os.path.getsize(png) > 1000
        print("%-9s -> %s%s" % (name, png, "" if ok else "  FAILED"))
        made += 1 if ok else 0
        try:
            os.remove(tmp)
        except OSError:
            pass
    print("rendered %d/%d" % (made, len(VIEWS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
