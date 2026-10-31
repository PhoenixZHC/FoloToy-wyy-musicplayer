# Folo Music firmware

Current source and local firmware: **Folo Music 2.1**, direct NetEase access on FoloToy AI Passport (ESP32-C3, 8 MiB Flash, no PSRAM). See the [product README](../README.md), [deployment guide](../docs/deployment.md) and [release notes](../docs/releases/v2.1.md).

Use ESP-IDF 5.5.4 for this product; upstream guides retain their separate 5.5.3 baseline. Activate the installed SDK, then run:

    cd firmware/ai-passport
    idf.py -B build-direct -D SDKCONFIG=build-direct/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.direct.defaults" build
    idf.py -B build-direct merge-bin -o build-direct/FoloToy-Wangyiyun-full.bin
    python tools/verify_firmware.py build-direct
    python tools/package_release.py build-direct ../../release/FoloMusic-v2.1

The default Kconfig retains historical relay compatibility. Always select sdkconfig.direct.defaults for 2.1. The full image is exactly 8,388,608 bytes, flashes at 0x0 and clears Wi-Fi/account/display settings. Only use the app-only image at 0x10000 after checking the existing 7 MiB factory layout. Never publish a configured device dump.

Product code and BSP are in ai-passport/, shared protocol/audio components in components/, host fixtures in ai-passport/tests/ and verification/packaging tools in ai-passport/tools/. Generated fonts are committed; build caches, private data and firmware binaries are excluded from source commits.
