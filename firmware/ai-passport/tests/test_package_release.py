"""Release layout regression fixtures, isolated from production builds and devices."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("release", Path(__file__).resolve().parents[1] / "tools/package_release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)

class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="folo-release-test-")
        self.addCleanup(self.temp.cleanup)
        self.build = Path(self.temp.name) / "build"
        self.output = Path(self.temp.name) / "output"
        (self.build / "bootloader").mkdir(parents=True)
        (self.build / "partition_table").mkdir()
        self.description = dict(project_version="2.1", target="esp32c3")
        (self.build / "project_description.json").write_text(json.dumps(self.description))
        (self.build / "sdkconfig").write_text("CONFIG_NETEASE_DIRECT=y\nCONFIG_ESPTOOLPY_FLASHSIZE_8MB=y\n")
        (self.build / "bootloader/bootloader.bin").write_bytes(b"test-bootloader")
        self.app = b"isolated-test-app-not-production"
        (self.build / "FoloToy-Wangyiyun.bin").write_bytes(self.app)
        self.table()

    def table(self, nvs_offset=0x9000):
        data = b"".join(struct.pack("<HBBII16sI", 0x50aa, typ, subtype, offset, size, name, 0)
                        for name, typ, subtype, offset, size in
                        [(b"nvs", 1, 2, nvs_offset, 0x6000),
                         (b"phy_init", 1, 1, 0xf000, 0x1000),
                         (b"factory", 0, 0, 0x10000, 0x700000)])
        (self.build / "partition_table/partition-table.bin").write_bytes(data + b"\xff" * (3072-len(data)))

    def test_full_layout_blank_nvs_and_checksums(self):
        manifest = release.package(self.build, self.output)
        data = (self.output / "FoloMusic-v2.1-full-8MB.bin").read_bytes()
        self.assertEqual(len(data), 8388608)
        self.assertEqual(data[0x10000:0x10000+len(self.app)], self.app)
        self.assertEqual(data[0x9000:0x10000], b"\xff" * 0x7000)
        self.assertEqual(data[0x10000+len(self.app):], b"\xff" * (8388608-0x10000-len(self.app)))
        self.assertFalse(manifest["nvs_included"])
        for line in (self.output / "SHA256SUMS.txt").read_text().splitlines():
            digest, name = line.split("  ", 1)
            self.assertEqual(digest, hashlib.sha256((self.output / name).read_bytes()).hexdigest())
        self.assertEqual(release.package(self.build, self.output), manifest)

    def test_wrong_release_configuration_rejected(self):
        for key, wrong in (("project_version", "1.0"), ("target", "esp32s3")):
            with self.subTest(key=key):
                desc = {**self.description, key: wrong}
                (self.build / "project_description.json").write_text(json.dumps(desc))
                with self.assertRaises(ValueError): release.package(self.build, self.output)
        (self.build / "project_description.json").write_text(json.dumps(self.description))
        (self.build / "sdkconfig").write_text("CONFIG_NETEASE_DIRECT=n\nCONFIG_ESPTOOLPY_FLASHSIZE_8MB=y\n")
        with self.assertRaises(ValueError): release.package(self.build, self.output)
        self.assertFalse(self.output.exists())

    def test_partition_and_headroom_rejected(self):
        self.table(nvs_offset=0xa000)
        with self.assertRaises(ValueError): release.package(self.build, self.output)
        self.table()
        (self.build / "FoloToy-Wangyiyun.bin").write_bytes(b"x" * (0x700000-512*1024+1))
        with self.assertRaises(ValueError): release.package(self.build, self.output)
        self.assertFalse(self.output.exists())

    def test_existing_artifact_is_not_overwritten(self):
        release.package(self.build, self.output)
        full = self.output / "FoloMusic-v2.1-full-8MB.bin"
        before = full.read_bytes()
        sums = self.output / "SHA256SUMS.txt"
        sums.write_text("existing-different-manifest\n")
        with self.assertRaises(FileExistsError): release.package(self.build, self.output)
        self.assertEqual(full.read_bytes(), before)
        self.assertEqual(sums.read_text(), "existing-different-manifest\n")

if __name__ == "__main__":
    unittest.main()
