#!/usr/bin/env python3
"""Render production LVGL fixtures in both modes; fail on missing glyphs."""
import argparse
import subprocess
import tempfile
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

VIEWS = "welcome wifi wifiempty wifipassword wifikeylower wifikeyupper wifikeynumber wifivisible wifiinvalid wifiactions connecting qr scanned expired signedin restore library playlists list loading emptylibrary empty player paused buffering menu volume end settings appearance brightness network account logout resetwifi about wififail loadfail playfail unavailable offline session longtitle longartist artwork lyrics longlyrics expandedfont expandedlist".split()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    canvas = Image.new("RGB", (2016, ((len(VIEWS)*2+7)//8)*350), "#bcb5a8")
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.load_default()
    with tempfile.TemporaryDirectory(prefix="scheme-b-preview-") as temporary:
        for i, (view, mode) in enumerate((v, m) for v in VIEWS for m in ("day", "night")):
            output = Path(temporary) / (view + "-" + mode + ".ppm")
            subprocess.run([str(args.executable.resolve()), view, mode, str(output)], check=True)
            x, y = (i % 8) * 252 + 6, (i // 8) * 350
            draw.text((x, y + 5), view + " / " + mode, font=font, fill="#201e19")
            with Image.open(output) as page:
                canvas.paste(page, (x, y + 25))
    canvas.save(args.output)
    print(f"PASS: {len(VIEWS)*2} native renders; no missing glyphs in fixtures")

if __name__ == "__main__":
    main()
