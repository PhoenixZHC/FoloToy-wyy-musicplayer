"""Real-source font coverage and LVGL small-descriptor regression checks."""
import importlib.util
import json
import random
import re
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("fontgen", Path(__file__).resolve().parents[1] / "tools/generate_scheme_b_fonts.py")
fontgen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fontgen)


class FontTests(unittest.TestCase):
    def test_expanded_coverage(self):
        chars = fontgen.expanded_characters(set("Folo Music"))
        self.assertTrue(fontgen.gb2312() <= chars)
        self.assertTrue(fontgen.SIMPLIFIED_NAME_ADDITIONS <= chars)
        self.assertTrue(set(map(chr, range(32, 127))) <= chars)
        self.assertEqual({ch for ch in chars if fontgen.han(ch)},
                         fontgen.gb2312() | fontgen.SIMPLIFIED_NAME_ADDITIONS)
        self.assertFalse(set("體漢樂龍臺灣禕滙裏裡凪éñüÅ") & chars)
        with tempfile.TemporaryDirectory(prefix="folo-charset-test-") as tmp:
            path = Path(tmp) / "deviceCharset.ts"
            fontgen.write_server_charset(chars, path)
            declaration = path.read_text(encoding="utf-8").splitlines()[-1]
            encoded = declaration.removeprefix("export const DEVICE_SUPPORTED_TEXT = ").removesuffix(";")
            self.assertEqual(set(json.loads(encoded)), {ch for ch in chars if ord(ch) > 0x7F})

    def test_real_glyphs_split_without_changing_line_height(self):
        chars = set("简体音乐祎喆Ǚ")
        with tempfile.TemporaryDirectory(prefix="folo-font-test-") as tmp:
            with patch.object(fontgen, "FONT_DIR", Path(tmp)), patch.object(fontgen, "MAX_BITMAP_BYTES", 256):
                info = fontgen.generate("test_font", 25, fontgen.SERIF, chars)
            self.assertGreater(info["bitmap_parts"], 1)
            self.assertLessEqual(info["max_part_bytes"], 256)
            self.assertEqual(info["line_height"], 29)
            self.assertGreater(info["fitted_glyphs"], 0)
            text = (Path(tmp) / "test_font.c").read_text()
            self.assertIn(".fallback=&test_font_part_1", text)
            self.assertIn(".line_height=29,.base_line=7", text)
            self.assertTrue(all(int(n) < 256 for n in re.findall(r"bitmap_index=(\d+)", text)))
            recovered = []
            for start, offsets in re.findall(r"range_start=(\d+).*?unicode_list=unicode_list_(\d+)", text):
                values = re.search(r"unicode_list_%s\[\]=\{([^}]+)\}" % offsets, text).group(1)
                recovered.extend(int(start) + int(n) for n in values.split(","))
            self.assertEqual(sorted(recovered), sorted(map(ord, chars)))

    def test_japanese_korean_coverage_only_in_metadata_sizes(self):
        chinese = fontgen.expanded_characters(set("Folo Music"))
        metadata = fontgen.metadata_characters(chinese)
        self.assertTrue(set("夜に駆ける宇多田ヒカル봄날방탄소년단보고싶다ㅋㅋ") <= metadata)
        self.assertEqual(sum(0xAC00 <= ord(ch) <= 0xD7A3
                             for ch in fontgen.korean_characters()), 2350)
        self.assertTrue(chinese <= metadata)
        for size in (9, 12, 24):
            self.assertEqual(fontgen.characters_for_size(size, set("音乐"), chinese, metadata), metadata)
        for size in (10, 14):
            self.assertEqual(fontgen.characters_for_size(size, set("音乐"), chinese, metadata), chinese)
        for size in (8, 13, 17, 18, 22):
            self.assertEqual(fontgen.characters_for_size(size, set("音乐"), chinese, metadata), set("音乐"))
        with tempfile.TemporaryDirectory(prefix="folo-multilingual-font-test-") as tmp:
            with patch.object(fontgen, "FONT_DIR", Path(tmp)):
                for size in (9, 12, 25):
                    info = fontgen.generate("metadata_%d" % size, size,
                        fontgen.SERIF if size == 25 else fontgen.SANS,
                        set("夜に駆ける宇多田ヒカル봄날방탄소년단보고싶다ㅋㅋ"))
                    self.assertEqual(info["fitted_glyphs"], 0)

    def test_compression_round_trip(self):
        rng = random.Random(20261028)
        for count in (0, 1, 2, 12, 73, 1000):
            for pixels in ([0] * count, [3] * count, [rng.randrange(4) for _ in range(count)]):
                self.assertEqual(fontgen.rle_decode(fontgen.rle_encode(pixels), count), pixels)


if __name__ == "__main__":
    unittest.main()
