#!/usr/bin/env python3
"""Package clean build images without reading a device or copying its NVS."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

FLASH_BYTES = 8 * 1024 * 1024
APP_OFFSET = 0x10000
APP_BYTES = 0x700000
IMAGES = ((0, "bootloader/bootloader.bin"),
          (0x8000, "partition_table/partition-table.bin"),
          (APP_OFFSET, "FoloToy-Wangyiyun.bin"))

def package(build, output, version="2.1"):
    description = json.loads((build / "project_description.json").read_text())
    if description["project_version"] != version or description["target"] != "esp32c3":
        raise ValueError("Unexpected project version or target")
    config = (build / "sdkconfig").read_text()
    for expected in ("CONFIG_NETEASE_DIRECT=y", "CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y"):
        if expected not in config.splitlines():
            raise ValueError("Required release configuration missing: " + expected)
    app = (build / IMAGES[-1][1]).read_bytes()
    if len(app) > APP_BYTES - 512 * 1024:
        raise ValueError("Application must leave at least 512 KiB headroom")
    table = (build / IMAGES[1][1]).read_bytes()
    entries = {}
    for pos in range(0, len(table), 32):
        if table[pos:pos + 2] != b"\xaa\x50":
            break
        magic, typ, subtype, offset, size, label, flags = struct.unpack("<HBBII16sI", table[pos:pos + 32])
        entries[label.split(b"\0", 1)[0].decode()] = (offset, size)
    for label, expected in {"nvs": (0x9000, 0x6000), "phy_init": (0xf000, 0x1000),
                            "factory": (APP_OFFSET, APP_BYTES)}.items():
        if entries.get(label) != expected:
            raise ValueError("Unexpected partition: " + label)
    image = bytearray(b"\xff" * FLASH_BYTES)
    segments = []
    for offset, name in IMAGES:
        data = (build / name).read_bytes()
        limit = {0: 0x8000, 0x8000: 0x9000, APP_OFFSET: APP_OFFSET + APP_BYTES}[offset]
        if not data or offset + len(data) > limit:
            raise ValueError("Image overlaps a partition boundary: " + name)
        image[offset:offset + len(data)] = data
        segments.append(dict(offset=hex(offset), bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
    if image[0x9000:APP_OFFSET] != b"\xff" * 0x7000:
        raise ValueError("NVS/PHY must be blank in a public factory image")
    if image[APP_OFFSET + len(app):] != b"\xff" * (FLASH_BYTES - APP_OFFSET - len(app)):
        raise ValueError("Unexpected data after application")
    output.mkdir(parents=True, exist_ok=True)
    full_name = "FoloMusic-v" + version + "-full-8MB.bin"
    app_name = "FoloMusic-v" + version + "-app.bin"
    files = {full_name: bytes(image), app_name: app}
    manifest = dict(version=version, target="esp32c3", mode="direct", flash_bytes=FLASH_BYTES,
                    application_bytes=len(app), application_free_bytes=APP_BYTES-len(app),
                    factory_flash_offset="0x0", application_flash_offset=hex(APP_OFFSET),
                    nvs_included=False, factory_image_clears_settings=True, segments=segments,
                    files={name: dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
                           for name, data in files.items()})
    files["manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    sums = "".join(hashlib.sha256(data).hexdigest() + "  " + name + "\n" for name, data in files.items())
    files["SHA256SUMS.txt"] = sums.encode()
    for name, data in files.items():
        target = output / name
        if target.exists() and target.read_bytes() != data:
            raise FileExistsError("Refusing to replace a different release artifact: " + str(target))
    for name, data in files.items():
        target = output / name
        target.write_bytes(data)
        if target.read_bytes() != data:
            raise ValueError("Artifact read-back failed: " + name)
    return manifest

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    manifest = package(args.build.resolve(), args.output.resolve())
    print(json.dumps(manifest, indent=2))

if __name__ == "__main__":
    main()
