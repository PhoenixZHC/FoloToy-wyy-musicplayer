<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Native scheme B preview

This host executable links the production LVGL renderer and generated fonts. Fixed battery, songs and QR are isolated test fixtures, never production data. Preview QR codes cannot log in.

From the firmware directory configure with cmake -S tests/ui_preview -B /tmp/scheme-b-preview, then cmake --build /tmp/scheme-b-preview. Run /tmp/scheme-b-preview/scheme_b_preview player day /tmp/player.ppm. A host C/C++ compiler is required; no display server is needed.

The gallery covers 49 views in day and night modes (98 renders), including lyrics, artwork, long metadata, simplified/name characters and English letters. Every label is checked for missing glyphs before rendering. Output: 240 by 320 RGB PPM. This does not validate network, keys, battery, audio, timing or Flash capacity.

Font generation uses tools/generate_scheme_b_fonts.py --full with Pillow, the FreeType shared library and the Noto CJK/Nimbus Roman fonts named in the script. Each dynamic size contains 6774 Han glyphs (all GB2312 plus 11 name characters), all 52 English letters, digits and UI punctuation. Additional traditional and other-language expansion is excluded. The manifest records source hashes, glyph counts and bitmap parts. Current fonts each fit in one bitmap; the generator can split larger bitmaps at 1 MiB to fit LVGL small descriptors. Original line metrics are retained. Every compressed glyph is decoded and compared before writing. Source licenses are in main/fonts/font-licenses. The legacy font_24 symbol renders at 25 px. Run python3 tests/test_scheme_b_fonts.py for the three focused tests using a Python environment with Pillow.

Configure with -DPREVIEW_DEVICE_HEAP=ON to exercise a bounded LVGL heap similar to the ESP32-C3 build. This catches widget-allocation regressions in high-density screens; host pointer sizes make exact free-byte comparisons platform dependent.

Generation also updates server/src/generated/deviceCharset.ts at the repository root from the actual dynamic character set. Repository validation checks that every non-ASCII service character exists in all dynamic fonts; do not bypass that contract when reducing language coverage.

Reproduce the gallery with python3 tests/ui_preview/render_gallery.py /tmp/scheme-b-preview/scheme_b_preview /tmp/scheme-b-gallery.png (requires Pillow). The process exits on any rendering or missing-glyph failure.
