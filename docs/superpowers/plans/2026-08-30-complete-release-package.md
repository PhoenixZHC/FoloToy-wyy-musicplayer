# Complete Release Package Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Build and verify one complete v1.2.2 delivery ZIP containing an exact 8 MB relay firmware image, the portable Windows relay service, instructions, and checksums.

**Architecture:** Rebuild the existing ESP-IDF relay firmware, verify its merged image, then append only `0xFF` bytes to the 8 MB flash boundary. Assemble immutable delivery inputs into one clean directory, hash them, archive that directory, and remove only the explicitly approved generated directories.

**Tech Stack:** ESP-IDF v5.5.4, esptool, Windows PowerShell 5.1-compatible commands, SHA-256, ZIP.

**Spec:** `docs/superpowers/specs/2026-08-30-complete-release-package-design.md`

## Global Constraints

- The firmware target is ESP32-C3 with 8 MB flash and no PSRAM.
- The padded firmware must be exactly 8,388,608 bytes and be flashed at `0x0`.
- Preserve source code, build directories, and `netease-official-firmware/`.
- Delete only the expanded v1.2.2 relay release and the two confirmed `.codex-figma-check*` temporary directories.
- Use the existing portable relay ZIP without injecting runtime `data/` files.

---

### Task 1: Rebuild and verify the relay firmware

**Files:**
- Produce: `firmware/ai-passport/build/FoloToy-Wangyiyun-full.bin`
- Consume: `firmware/ai-passport/build/flash_args`
- Consume: component binaries referenced by `flash_args`

**Interfaces:**
- Consumes: ESP-IDF v5.5.4 project configuration and source tree.
- Produces: a verified merged raw image whose embedded bytes match every flash component.

- [x] Activate the configured ESP-IDF v5.5.4 environment.
- [x] Run `idf.py build` and require exit code zero.
- [x] Run `idf.py merge-bin -o build/FoloToy-Wangyiyun-full.bin` and require exit code zero.
- [x] Run `tools/verify_firmware.py build` with the project Python interpreter and require exit code zero.

### Task 2: Create the exact 8 MB firmware and delivery directory

**Files:**
- Create: `release/FoloToy-Wangyiyun-Complete-v1.2.2/firmware/FoloToy-Wangyiyun-full-8MB.bin`
- Copy: `release/FoloToy-Wangyiyun-Complete-v1.2.2/server/FoloToy-Wangyiyun-Relay-Windows-x64-v1.2.2.zip`

**Interfaces:**
- Consumes: verified merged firmware and existing clean portable server ZIP.
- Produces: exact-size firmware and stable delivery inputs.

- [x] Confirm the merged firmware is smaller than or equal to 8,388,608 bytes.
- [x] Copy it to the delivery directory and append `0xFF` until its size is exactly 8,388,608 bytes.
- [x] Re-run component-offset byte comparisons against the padded firmware.
- [x] Copy the existing portable server ZIP into the delivery directory.

### Task 3: Update release documentation and checksums

**Files:**
- Create: `release/FoloToy-Wangyiyun-Complete-v1.2.2/使用说明.md`
- Create: `release/FoloToy-Wangyiyun-Complete-v1.2.2/SHA256SUMS.txt`
- Modify: `README.md`
- Modify: `docs/deployment.md`
- Modify: `CONTEXT.md`

**Interfaces:**
- Consumes: final artifact names, sizes, and hashes.
- Produces: user instructions and project records that match the artifacts.

- [x] Write concise instructions for flashing at `0x0`, starting the relay, and provisioning the device.
- [x] Generate SHA-256 entries for the firmware and portable server ZIP.
- [x] Document the complete package structure and commands in project documentation.
- [x] Record the completed release in `CONTEXT.md`.

### Task 4: Archive, clean, and verify the release

**Files:**
- Create: `release/FoloToy-Wangyiyun-Complete-v1.2.2.zip`
- Delete: `release/FoloToy-Wangyiyun-Relay-Windows-x64-v1.2.2/`
- Delete: `.codex-figma-check/`
- Delete: `.codex-figma-check-independent/`

**Interfaces:**
- Consumes: completed delivery directory.
- Produces: the final distributable ZIP and a cleaned project root.

- [x] Resolve and verify every deletion target is inside the workspace and exactly matches the approved path.
- [x] Delete the three approved generated directories.
- [x] Compress the complete delivery directory without adding an extra unrelated root.
- [x] Extract the ZIP to a temporary directory and verify its file list, firmware size, and SHA-256 values.
- [x] Review all documentation changes and ensure no unrelated source files were modified.
