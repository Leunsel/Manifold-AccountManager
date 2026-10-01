#!/usr/bin/env python3
"""Generate Resources/icon.ico from Resources/icon.svg (the SVG is the source).

Renders once at 1024 px with headless Edge or Chrome, then downsamples with
Lanczos to ten sizes from 16 to 256 px: up to 64 px as uncompressed DIB, larger
as PNG. Requires Pillow.
"""
import io
import os
import struct
import subprocess
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow fehlt:  python -m pip install Pillow")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
RES = os.path.join(ROOT, "Manifold-Vault", "Resources")
SVG = os.path.join(RES, "icon.svg")
ICO = os.path.join(RES, "icon.ico")

SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
PNG_FROM = 128
BASE = 1024

FILL_PERCENT = 92

BROWSERS = [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
]

PAGE = """<!doctype html>
<meta charset="utf-8">
<style>
  html, body { margin: 0; padding: 0; height: 100%%; background: transparent; }
  body { display: flex; align-items: center; justify-content: center; }
  img { height: %d%%; width: auto; display: block; }
</style>
<img src="icon.svg">
""" % FILL_PERCENT

def find_browser():
    for p in BROWSERS:
        if os.path.exists(p):
            return p
    sys.exit("Neither Edge nor Chrome found -- a rasteriser is required.\n"
             "Searched:\n  " + "\n  ".join(BROWSERS))

def render_base(work):
    """Render the SVG once at BASE x BASE, with an alpha channel."""
    with io.open(os.path.join(work, "icon.svg"), "wb") as f:
        f.write(io.open(SVG, "rb").read())
    with io.open(os.path.join(work, "render.html"), "w", encoding="utf-8") as f:
        f.write(PAGE)

    out = os.path.join(work, "base.png")
    if os.path.exists(out):
        os.remove(out)

    url = "file:///" + os.path.join(work, "render.html").replace("\\", "/")
    subprocess.run([find_browser(),
                    "--headless", "--disable-gpu", "--no-sandbox",
                    "--hide-scrollbars",
                    "--default-background-color=00000000",
                    "--window-size=%d,%d" % (BASE, BASE),
                    "--screenshot=" + out, url],
                   capture_output=True)
    if not os.path.exists(out):
        sys.exit("The rasteriser did not write an image.")

    im = Image.open(out).convert("RGBA")
    if im.size != (BASE, BASE):
        sys.exit("Unexpected size: %s" % (im.size,))
    if im.getchannel("A").getextrema()[0] != 0:
        sys.exit("The rendering has no transparent border -- "
                 "--default-background-color did not take effect.")
    return im

def dib_entry(im):
    """Uncompressed 32-bit DIB, as an ICO expects it."""
    w, h = im.size
    px = im.load()

    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, 0, 0, 0, 0, 0)

    xor = bytearray()
    for y in range(h - 1, -1, -1):
        for x in range(w):
            r, g, b, a = px[x, y]
            xor += bytes((b, g, r, a))

    return bytes(header) + bytes(xor) + bytes((((w + 31) // 32) * 4) * h)

def png_entry(im):
    buf = io.BytesIO()
    im.save(buf, format="PNG", optimize=True)
    return buf.getvalue()

def main():
    if not os.path.exists(SVG):
        sys.exit("Keine Quelle: %s" % SVG)

    work = os.path.join(HERE, "_icon_tmp")
    os.makedirs(work, exist_ok=True)
    base = render_base(work)

    blobs = []
    for s in SIZES:
        im = base.resize((s, s), Image.Resampling.LANCZOS)
        data = png_entry(im) if s >= PNG_FROM else dib_entry(im)
        blobs.append((s, data))

    out = bytearray()
    out += struct.pack("<HHH", 0, 1, len(blobs))
    offset = 6 + 16 * len(blobs)
    for s, data in blobs:
        out += struct.pack("<BBBBHHII",
                           0 if s == 256 else s,
                           0 if s == 256 else s,
                           0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for _, data in blobs:
        out += data

    with io.open(ICO, "wb") as f:
        f.write(bytes(out))

    for s, data in blobs:
        print("  %3d x %-3d  %-3s %7d Byte" % (s, s, "PNG" if s >= PNG_FROM else "DIB",
                                               len(data)))
    print("\n  %s -- %d Eintraege, %d Byte"
          % (os.path.relpath(ICO, ROOT), len(blobs), len(out)))

    for name in ("icon.svg", "render.html", "base.png"):
        p = os.path.join(work, name)
        if os.path.exists(p):
            os.remove(p)
    os.rmdir(work)
    return 0

if __name__ == "__main__":
    sys.exit(main())
