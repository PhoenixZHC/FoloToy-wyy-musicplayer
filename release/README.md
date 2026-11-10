<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Firmware delivery directory

## Current local delivery: Folo Music 2.2.0

The complete image is FoloMusic-v2.2.0/FoloMusic-v2.2.0-full-8MB.bin, exactly 8,388,608 bytes. SHA-256: 2c27c232d353a73b8c2cf519affdc8c1be88d871e381907067d196328304de96.

The same directory contains the app-only image, manifest.json and SHA256SUMS.txt. Generated FoloMusic-v*/ directories are excluded from Git. A fresh checkout must build the files using the [deployment guide](../docs/deployment.md), including --version 2.2.0; see [2.2.0 notes](../docs/releases/v2.2.0.md) for changes, known defects and acceptance limits. This source push does not publish a GitHub Release.

The full image flashes at 0x0 and clears Wi-Fi, login and display settings. App-only flashing at 0x10000 preserves settings only with the matching 7 MiB factory partition. Public images contain blank NVS/PHY and no configured-device credentials.

## Earlier local artifacts

| Directory or file prefix | Purpose |
| --- | --- |
| FoloMusic-v2.1.1/ | Previous hotspot repair delivery; retain its dated validation record |
| FoloMusic-v2.1-hotspot/ | First hotspot test build; failed device playback, retained for comparison |
| FoloMusic-v2.1-hotspot-devicefix/ | Repaired playback build tested on hardware, still labeled 2.1; superseded by the 2.1.1 package |
| FoloToy-Wangyiyun-Complete-v1.2.2* | Historical relay bundle and firmware |
| FoloToy-Wangyiyun-Relay-Windows-x64-v1.2.2.zip | Historical Windows relay service |

Earlier artifacts are preserved for traceability and must not be mistaken for the current direct-access firmware. Private logs, local tools and backups belong in the ignored .local-backup/ directory at the repository root. Build products remain in ignored firmware build directories; source, fixtures and documentation remain in their existing component directories.
