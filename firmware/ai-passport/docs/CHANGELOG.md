<p align="right">
  <a href="CHANGELOG.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Changelog

## Unreleased

## 2.0 — 2026-10-28

- Release Folo Music 2.0: open-protocol direct access, on-device Wi-Fi keyboard, NetEase App QR authorization and session restore, Scheme B day/night UI, artwork, lyrics, three-key navigation, caching and simplified Chinese/English fonts. The user reports no new bugs in current testing; systematic long-duration and all network/catalog coverage remain unverified.

- Add a clean exact-8-MiB factory image, app-only upgrade, SHA-256 manifest and packaging regressions. Factory flashing clears settings; app-only upgrades preserve settings only with the matching 7 MiB partition layout. Organize current documentation, archive old deployment/development state and credit open-source authors.

### Implementation history

The following snapshots retain their original candidate results and failures; they are not the current release status. Current acceptance limits are in repository-root docs/releases/v2.0.md.

- Keep the full GB2312 set and add 11 name characters for 6774 Han glyphs in each dynamic UI font, plus all 52 English letters, digits and UI punctuation. The user requested simplified Chinese and English only, so additional traditional and other-language expansion was withdrawn. Verify source coverage, RLE round trips and the 20-bit bitmap-offset limit while retaining line metrics. Set version to 2.0 and remove the playback-order caption and top branding on player/list screens. Direct application: 3,883,184 bytes, leaving 3,456,848 bytes (about 3.30 MiB) in the 7 MiB partition. Font tests (3), host executables (12), native day/night renders (98), both builds and merged-image checks pass. Hardware display and playback concurrency acceptance remain pending.

- Free the first WEAPI AES result before RSA and URL-encode directly into the final request body. A real eight-song encoder regression first failed under a 3072-byte allocation budget, then passed; tracked encoder peak drops from 3187 to 1626 bytes. This excludes shared mbedTLS internal allocations and is not a device heap simulation. The host gate now runs 12 executables, correcting its stale count.

- Restore the SDK-default I2S DMA depth of six 240-frame buffers after hardware reproduced allocation failure with the static TLS RX candidate. Initial stereo DMA payload drops from 16,384 to 5760 bytes; compressed input stays at 8 KiB. Log internal heap before and after audio initialization. Concurrent playback and list loading remain unverified.

- Use ESP-IDF's official post-handshake static-RX strategy on the reusable WEAPI transport, preserving the full 16 KiB incoming record. Direct TLS writes fragment at 1 KiB while HTTP header storage remains 4 KiB; peer certificates are discarded only after verification. The prior short-connection experiment reclaimed 24,104 bytes but still exhausted heap on a fresh handshake and was withdrawn. Static-RX policy regression passes; hardware concurrency acceptance remains pending.

- Add a repeatable PassportSim WASM diagnostic runner. Nine provisioning checks pass in a separate IDF 5.5.3 build only after omitting unsupported esp_wifi_set_ps in a temporary source copy. Production networking/playback acceptance remains blocked; no new firmware is flashed. Reuse same-origin WEAPI connections; rebuild once after an empty-response header failure only for explicit read-only queries. Regression tests exclude EAPI, partial responses and unlimited retries. Playback-time TLS allocation failures remain unresolved. See repository-root docs/loading-validation.md.

- Batch compressed-audio refills below 2 KiB, forcing a read when the decoder needs more input; retain the 8 KiB input capacity and reduce duplicate HTTP RX storage to 1 KiB. Shrink retained lyrics to their actual length. Direct builds budget six static Wi-Fi RX buffers and twelve dynamic RX/TX buffers to leave TLS headroom. Request playlist IDs with n=0 before fetching page metadata. Host checks and direct firmware validation pass; hardware playback/paging and response-size acceptance remain pending.

- Enable dynamic TLS RX/TX buffers in direct builds while retaining the 16 KiB maximum incoming record. Hardware reproduced TLS setup allocation failures during playback even with the smaller audio buffer and retries. Static checks, 10 host executables, direct build, merged-image verification and application flash hash pass; all NVS bytes match. Concurrent playback/list acceptance remains pending after a Wi-Fi authentication timeout at boot.

- Reduce the active audio input buffer from 16 KiB to 8 KiB to leave more memory for metadata HTTPS requests during playback. Retry transient track-request connection and allocation failures twice before showing an error. The application write hash and unchanged NVS were verified; concurrent playback/list loading acceptance remains pending because Wi-Fi authentication timed out after restart.

- Enlarge player lyrics from 10 px to 14 px in a 42 px-high, two-line area. Move track information up and progress controls down to keep lyrics readable without overlapping controls, using the primary theme text color in both modes.

- Add artwork and timed lyrics to the player. Missing artwork, download failures and decode failures leave no placeholder and restore the full text width. Song and artist labels scroll only when wider than their display area. Hardware media and scrolling acceptance remain pending.

- Fetch eight song metadata records per page while keeping four visible rows. The three-page cache and adjacent prefetch now cover a wider window, reducing the visible loading pause when browsing; the direct client retains a 512-ID sliding window without caching audio.

- Hardware validation after the header-buffer fix: account verification, 20-playlist loading, track-page metadata, adjacent prefetch, and three audio URL requests all returned `err=0`; the monitor saw no header warnings, EAGAIN, watchdog, or panic events. Audible output and long-play stability remain user checks.

- Fix direct NetEase HTTPS request headers by using a 4096-byte transmit buffer for both EAPI and WEAPI requests. The previous 1024-byte WEAPI buffer could not hold the maximum session Cookie and emitted incomplete-header warnings; the flashed device now verifies the account and loads 20 playlists successfully.

- Prevent a provisioning-screen freeze caused by LVGL heap exhaustion on the password keyboard: replace per-key widgets with three button-matrix rows. The pointer-width-scaled bounded-heap preview, both firmware builds, host tests and day/night renders pass; the direct app was flashed while preserving NVS. Hardware logs confirm keyboard interaction, connection submission, IP acquisition and successful Wi-Fi connection without new watchdog events; subsequent NetEase HTTP failures remain unresolved.

- Show an on-device password keyboard with QWERTY letters, digits, symbols and space, highlighted selection, day/night themes, masking and length feedback. Use ↑/↓ to move, OK to enter, long ↑/↓ to switch groups and long OK for actions. Direct firmware flashed at 0x10000 with all 24,576 NVS bytes preserved; physical typing and new-network connection acceptance remain pending.

- Replace phone provisioning with device-side 2.4 GHz scanning and three-button password editing (character groups, delete, visibility and validation). Remove SoftAP/HTTP provisioning from production. Preserve saved Wi-Fi until the new connection succeeds and keep the NetEase session. Hidden SSIDs and enterprise authentication are unsupported; relay mode requires existing registration.
- Reduce track pages to four items and add bounded adjacent-page prefetch; hardware paging and paused-audio memory acceptance remain pending.
- Show Wi-Fi strength as three bars without a numeric RSSI value. Error screens offer a network-settings shortcut; provisioning failures offer retry and change-network actions. Entering setup preserves saved Wi-Fi and NetEase account data until a new network is submitted.

- Label every active footer action with its physical key (↑ / OK / ↓), in both themes and the HTML prototype; unavailable actions remain dashes. Rebuilt and flashed direct firmware, verified NVS byte preservation, and confirmed saved-login recovery and 20 playlists at boot.

- Scheme B migration in progress: day/night LVGL, browsing/playback snapshots, confirmation menus, bounded metadata/ID caches, expanded Chinese fonts. Also adds first-use welcome and distinct authentication/error states, fixes the first short click after a long press and preserves page cursors. Native preview renders 70 day/night fixtures without missing label glyphs. After explicit approval, the factory partition expands to 7 MiB without moving NVS or PHY data. Both builds and merged-image verification pass. Direct firmware write hashes pass and all 24,576 NVS bytes match before reboot; complete hardware and visual acceptance remain pending.

- Stop the previous audio stream before fetching a new track URL to avoid overlapping decoder/CDN and API TLS memory use on C3. Hardware verified a two-track switch with CDN HTTP 200 and 44.1 kHz MP3 decoding; audible output and longer stability checks remain pending.

- Allow bounded EAPI payloads to include longer real session tokens. Hardware now loads 20 playlists and 15 tracks; audio playback remains unverified.

- Handle playlist responses that exceed the requested limit: validate the entire stream, retain the first 20 entries, and show a `+` in the list count when more entries exist. Saved-login reuse was verified on hardware; playlist/playback acceptance is pending.

- Aligned QR login and EAPI header cookies/origins with the pinned open-source api-enhanced implementation. Real device QR/account verification now succeeds; playlist processing still fails with a size error and is under investigation. Added decrypted-request regression checks and sanitized diagnostics.

- Added an opt-in ESP32-C3 direct NetEase client with QR login, local NVS session storage, streaming playlist/song parsing, CDN URL validation, and Wi-Fi-only provisioning. The relay build remains the default.

- Replaced the Wangyiyun firmware screens with the independent 240x320 device UI: dark music-player layout, hardware-key footer, QR/login/list/player/error screens, generated CJK subset fonts, and a local preview renderer for Figma comparison.
- Added the standalone Wangyiyun playback path: the firmware can fetch an MP3 URL over HTTP, decode it through `chmorgan/esp-libhelix-mp3`, downmix to mono PCM, and control playback from the NetEase device UI.
- Allowed a long OK press on the QR-login and confirmation-wait screens to clear saved Wi-Fi/relay registration and return to provisioning even while the QR polling worker is busy.
- Fixed repeated provisioning submissions while Wi-Fi is already connected: reuse the current network when it matches, otherwise disconnect before connecting to the requested network.
- Documented a release-title convention for multi-app releases: name tags as `v<version>-<app-name>` (e.g. `v0.1.0-voice-keychain`) so the release title carries the version and the app, and confirm the title after the release is published so a release list is scannable by app.
- Added a post-release follow-up workflow: an `issue-suggestions` skill for filing user feedback as issues against the upstream project, an `experience-pr` skill for submitting reusable development experience as a documentation PR, a `docs/experiences/` directory for per-entry experience files, and supporting `after-release`, `file-issues`, and experience-index documents.
- Simplified the tracked repository root: moved GitHub-recognized community documents into `.github/`, moved the changelog into `docs/`, updated every reference, and added a root-document allowlist to repository checks.
- Repository-wide language policy: every maintained Markdown default `.md` file is English, Simplified Chinese uses a paired `.zh_CN.md`, and both provide language switches. Static checks reject missing peers, missing switches, and Chinese prose in English defaults.
- Phase one of the AI development workflow: streamlined task-based context routing, unified local/CI validation, added PR checks and a template, and committed the dependency lock for reproducible builds.
- PR review fixes: pinned GitHub Actions to full commit SHAs, split build/release jobs by least privilege, disabled persisted sync checkout credentials, added Feature Request and Usage Question forms, clarified private security-report fallback, and corrected stale README, CI-trigger, and branch descriptions.
- Changed commit titles, PR titles, and PR bodies from Chinese-default to English; updated the Chinese punctuation rule so it no longer applies to PR descriptions.
- Reworked `build-firmware.yml` to pass `SDKCONFIG_DEFAULTS=sdkconfig.defaults`, enable `partitions.csv`, preserve the 8 MB image header, merge a flashable `FoloToy-Wangyiyun-full.bin`, publish only that artifact, and use Actions cache v5.
- Integrated upstream PR #6 to resolve PR #4 conflicts: Wi-Fi, Bluetooth LE, radio lifecycle, and low-power demos; a 3 MB factory partition; build/menu/configuration updates; hardware-guide coverage; and bilingual capability tables.
- Defined English imperative Conventional Commit formatting for both commits and PR titles.
- Removed stale sync-workflow template comments and generalized an irrelevant Redis TTL rule to cache components.
- Added Chinese punctuation, credential safety, and recoverable file-deletion conventions.
- Expanded source-comment requirements for functions, state, ownership, concurrency, timing, registers, and magic values.
- Removed AI execution instructions from product READMEs so they remain human-facing product and repository overviews.
- Added `docs/development/agent-guide.md` as the focused AI workflow guide.
- Updated `AGENTS.md`, `docs/INDEX.md`, and the development index for the agent guide.
- Documented why the root README path is reserved for fork owners and how GitHub README precedence supports it.
- Created `main-update` from the upstream-aligned baseline and combined the repository-structure, firmware-CI, and upstream-sync work.
- Corrected the merged documentation index, workflow path, project tree, and CI references.
- Moved CI documentation from software design to `docs/development/`.
- Moved fork-only documentation assets from `assets/docs/` to `docs/assets/`.
- Moved the upstream English/Chinese project READMEs under `docs/` and renamed the documentation catalog to `docs/INDEX.md`.
- Initialized `AGENTS.md`, `CLAUDE.md`, and `CHANGELOG.md`.
- Standardized the initial project README language filenames.
- Added the `docs/`, `assets/`, and `skills/` directory structure.
- Moved the upstream hardware guide into `docs/hardware-design/`.
- Standardized subdirectory README capitalization and introduced fork conventions.
- Allowed fork-owned root README and supplemental documentation content on fork `main`.
- Added and documented the fork-only supplemental-document directory.
- Moved the build CI document to its dedicated CI branch before consolidation.
- Documented clean-`main` reasons, the direct-development exception, and Actions enablement for forks.
- Split the original agent rules into contribution, development, and fork documents with a compact root index.
- Updated software-design and project README references for the new documentation structure.
- Added the documentation catalog and task-triggered routing based on the earlier repository model.
- Added bilingual contribution, code-of-conduct, security, and support documents tailored to this ESP-IDF and fork workflow.
