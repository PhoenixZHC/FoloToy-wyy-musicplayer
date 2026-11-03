<p align="right">
  <a href="hotspot-playback.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Hotspot Playback Reliability

Development change: 2026-11-03, Asia/Shanghai, Ubuntu development computer. Based on Folo Music 2.1; not a published release or hardware acceptance.

## Fixes in review order

1. Playback configures WIFI_PS_NONE after initializing its own driver and checks the result. Provisioning checks it too. Leaving provisioning deinitializes Wi-Fi, so its setting cannot configure playback. Disabling modem sleep reduces receive latency but increases power consumption.
2. audio_download.c owns HTTP/TLS in a priority-5 task with a 6144-byte stack. The priority-6 decoder consumes a bounded 16 KiB FreeRTOS stream buffer without network blocking; its existing 8 KiB MP3 input remains. Playback waits for 8 KiB initially and after starvation, or for the producer to finish. AUDIO_STREAM_BUFFERING updates the UI even after playback has begun. Initial TLS finishes before codec/DMA allocation. Stop cancels and joins the producer before freeing its queue, without force-deleting a live network task.
3. The worker checks Wi-Fi every 250 ms when idle. Disconnect events log the reason and publish reconnection requests; they do not wait or touch LVGL. There are at most six attempts with backoff capped at 8 s. A new IP resets the budget; manual retry starts a fresh budget. HTTP recovery has at most six retries without 8 KiB of successful progress and at most 30 s without progress, with 250 ms to 4 s backoff. Connection/header timeouts are 3 s; read timeouts are 1 s. A blocking SDK call can extend the deadline. A timed-out socket is reopened after 3 s without data. Exhaustion remains an explicit error.
4. EOF requires complete HTTP framing and the expected byte count when known. Temporary EAGAIN does not mean EOF. Resume starts at the number of bytes successfully queued, preserving the decoder and its MP3 bit reservoir. A resumed response must be 206 with matching Content-Range, response length and total size. Available strong ETag or Last-Modified validators use If-Range; changed validators and ignored ranges fail explicitly. Requests use identity encoding, retain certificate verification and reject HTTPS-to-HTTP redirects. Unknown-length chunked responses require transport completion.

Speculative metadata prefetch is suspended while an audio task is active, including pause/buffering. Explicit browsing remains available. Wi-Fi RX/TX counts, audio bitrate and flash partitions are unchanged.

## Memory and diagnostics

The queue and producer stack add approximately 22 KiB plus context/RTOS bookkeeping. There is no PSRAM or silent smaller-buffer fallback. Allocation failure is explicit. At the requested 128 kbps, 8 KiB is about 0.51 s of compressed audio; it cannot compensate for indefinitely slow cellular service.

Logs separate decoder queue pulls from network reads: Playback Wi-Fi RSSI/channel/state; disconnect reasons and reconnect attempts; network received/queued bytes and longest read; Rebuffer audio starvation counts; free and largest heap blocks. URLs, passwords, cookies and raw headers are not logged. Correlate radio metrics with read timing: good Wi-Fi RSSI does not measure cellular or CDN performance.

## Automated validation

Use the [build/test instructions](build-and-test.md) and explicit product direct defaults:

    bash tools/validate.sh --static
    idf.py -B build-hotspot -D SDKCONFIG=build-hotspot/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.direct.defaults" build
    idf.py -B build-hotspot merge-bin -o FoloToy-Wangyiyun-full.bin
    python tools/verify_firmware.py build-hotspot
    python tools/package_release.py build-hotspot ../../release/FoloMusic-v2.1.1

The static gate runs 14 host executables. The new concurrent test compiles the production downloader against isolated pthread/HTTP/queue fakes, covering normal completion, temporary timeout, disconnect, premature EOF, ignored Range, invalid Range, retry exhaustion, chunked completion, changed ETag and cancellation with a full queue. Successful recovery checks every byte for omissions/duplicates, queue bounds and task/client cleanup. Policy tests cover buffering thresholds, completeness, range length/total/overflow and backoff. These tests do not emulate RF, MP3 decoding, power use or physical DMA timing.

The output directory distinguishes this development build; filenames still use the FoloMusic-v2.1 prefix. Do not confuse them with historical binaries. No tag or GitHub release is created. Record actual results and artifact hashes in the current delivery; a checklist is not passing evidence.

## Device acceptance

### Current Ubuntu validation record

On 2026-11-03, the ESP-IDF 5.5.4 direct build, merged-segment verification, exact-8-MiB packaging and artifact readback/hash verification passed. Fourteen host executables and four packaging regressions passed; the downloader executable includes ten concurrent fault scenarios. Repository consistency and whitespace checks passed. Independent source review reported no required fixes. The unified static gate stopped at workflow lint: actionlint is absent, the installer has no curl/gh, and attempts to retrieve the official release could not obtain a checksum-valid binary. Workflow lint is unverified, not passed. These are pre-flash results; device results are recorded below.

Development app: 3,905,664 bytes; application headroom: 3,434,368 bytes. Packaged files are under release/FoloMusic-v2.1-hotspot at the repository root. App SHA-256: 29c544ac641121a0bdd18fc3fe4cd271032d9d196f74ff6e160271a0b45a9190. Full 8,388,608-byte image SHA-256: 9fbb1eb9992e6bc3d5b735d304fbb1e8b9d71827f4f32113803d3a855757b3dc. This paragraph identifies the Ubuntu build artifacts; device flashing is recorded below.

### Full-device flash and boot check

On 2026-11-03 at 14:04 Asia/Shanghai, the Ubuntu development computer identified the only serial device, /dev/ttyACM0, as ESP32-C3 revision v1.1 with 8 MB flash. With explicit user authorization, esptool 4.12.0 wrote the above complete 8,388,608-byte development image at 0x0 in 23.7 seconds, reported Hash of data verified and exited with code 0. Existing Wi-Fi, account and display settings were cleared.

A reset followed by 40 seconds of serial capture confirmed DIO/80 MHz/8 MB and factory 0x10000/0x700000. Application version 2.1, compile time 2026-11-03 11:38:01 and ELF SHA-256 prefix c164545ea matched the development build. The 240×320 display, LVGL, three buttons, ES8311 codec and CW2017 gauge initialized and application startup completed. No panic, assertion, heap corruption or stack overflow markers appeared. The SDK boot version string is HEAD-HASH-NOTFOUND; the build tool version was separately verified as ESP-IDF 5.5.4. Initialization logs do not establish visual, physical-button or audio-output acceptance.

A subsequent 90-second runtime capture observed button-driven provisioning pages, two successful ESP_OK scans finding two/three networks and entry into the password screen. Set ps type: 0 confirmed power saving was disabled for provisioning. After the full erase, missing RF calibration data caused a full calibration and storage of fresh data; scanning then succeeded. No crash markers appeared during this window, but successful network connection and audio playback are not yet established.

The raw boot log remains local in ignored .local-backup/hotspot-device/boot-20261103-140422.log and must not be committed or published. Connection to a 2.4 GHz phone hotspot, QR login and playback are still required before testing download/decode, rebuffering, disconnection recovery and listening quality. Hotspot playback acceptance is not yet established.

### Playback failure and follow-up fix

The first device run successfully provisioned Wi-Fi, completed QR login and loaded 20 playlists. Audio returned HTTP 200 and decoded 44.1 kHz stereo 128 kbps MP3. However, after audio initialization only 4,340 heap bytes remained, with a largest block of 2,688 bytes. Reception stopped at 9,912 bytes, buffering began and all six recovery connections timed out in select. The stream explicitly failed with ESP_ERR_HTTP_CONNECT instead of incorrectly advancing the track. RSSI ranged from -46 to -60 dBm. This playback acceptance failed. No allocation-failure message was recorded, so the connection timeouts cannot be attributed entirely to low memory.

Code inspection confirmed that the last lyrics WEAPI request retained an idle TLS client and fixed RX buffer. Added netease_direct_client_release_connection, called by the same worker before audio starts and after explicit browsing requests finish during audio activity. Login, request configuration and metadata caches remain intact. Regression checks cover exactly-once cleanup, idempotence, unchanged account/NVS and reopening the next request. Three client host programs, the ESP-IDF build, merged segments and complete-image readback/hash verification passed.

Follow-up artifacts are in release/FoloMusic-v2.1-hotspot-devicefix: app 3,905,680 bytes, SHA-256 5906a6903a2f48636730daeb108a337ddf9e2d95e0c2137868e744f6679eb57a; full 8 MiB SHA-256 fa0e4a78696a89e031655e202adf07ec6818e83a45b378d8a4769886e965fae6. After the earlier full flash established the partition layout, this app was written at 0x10000 at 14:14, preserving the newly configured Wi-Fi/account. esptool took 13.8 seconds, reported Hash of data verified and exited with code 0. Startup ELF SHA prefix e153aba5a matched; Wi-Fi/account restoration and reloading 20 playlists succeeded. These artifacts supersede the earlier package, retained for fault comparison.

Reopening the USB serial port produced USB_UART_CHIP_RESET. Subsequent playback observation keeps one serial connection open; tool-induced resets must not be counted as firmware crashes. Private logs remain in ignored .local-backup/hotspot-device/ and were not committed or published. Follow-up retest: the same 128 kbps, 44.1 kHz stereo track played from device uptime 229,262 to approximately 441,199 ms (about 3 min 32 sec), ended normally and automatically advanced. The second track began at 444,871 ms. Every captured timing report for the first track retained cumulative underruns=0, with no HTTP recovery or playback failure. Heap after initialization was 31,492 bytes and typically about 27–34 KiB, compared with 4,340 bytes on the first build. The second track had 26,636 bytes after initialization and continued for about 110 seconds, with cumulative underruns still zero and typical free heap about 22–26 KiB, exercising track cleanup/recreation. Per-track peaks differ; this short run does not establish absence of long-term leaks. Logs establish PCM decoding/writes, not direct listening by the agent.

The user could not interrupt the hotspot during this run, so deliberate disconnection/Range recovery was not tested. Pause, concurrent browsing, prolonged outdoor weak-network behavior, power use and listening quality remain unaccepted. There was one gap between serial captures; subsequent cumulative underruns stayed at zero, but the missing interval was not verified frame by frame. The workflow lint limitation above still applies.

### Hardware checklist

Follow the [deployment guide](../../../../docs/deployment.md). App-only flashing at 0x10000 preserves settings only with the matching 7 MiB factory partition. Full 8 MiB flashing at 0x0 clears Wi-Fi, account and display settings. Confirm device and partition layout first.

1. Compare the same tracks on a router and a 2.4 GHz phone hotspot, screen on/off. Record phone model, cellular conditions and distance without credentials.
2. Briefly disable the hotspot during playback, then restore it. Check buffering, reconnection, continuing elapsed time, no repeated prefix and no skip due to an incomplete response.
3. Keep the hotspot absent past the retry budget. Check the explicit error and manual retry; normal completion must still advance exactly once.
4. Pause/resume, switch tracks repeatedly, browse while playing, and leave during buffering. Include disconnecting while an explicit list request is running: the business worker cannot maintain Wi-Fi until a synchronous API request returns. Check crashes, leaked tasks, falling free/largest heap and stuck controls.
5. Run continuous outdoor playback, comparing starvation counts, read latency, RSSI and battery consumption. Listening, radio performance and peak RAM remain pending hardware verification.

## Local 2.1.1 delivery

On 2026-11-03, bumped to 2.1.1 and built release/FoloMusic-v2.1.1/FoloMusic-v2.1.1-full-8MB.bin: 8,388,608 bytes, SHA-256 f1f9e6d002f5e0b045a58e7d770472417de51d5888c511aa8539ca9e03a252d6. App: 3,905,680 bytes, SHA-256 48c7973208387cf900c11fe6b26ce552a1e7dc907a097134426f685d226fbb05. ESP-IDF 5.5.4 build, merge, packaging, readback and four packaging regressions passed. Byte comparison with the device-tested follow-up build found changes only in the app descriptor and trailing integrity bytes. The 2.1.1 package has not been flashed; the device retains the same repaired code labeled 2.1. No new hardware acceptance is claimed. See [2.1.1 delivery](../../../../docs/releases/v2.1.1.md).

## Source-delivery verification

On 2026-11-03, the complete pre-commit static gate passed: repository checks, four packaging regressions, all 14 host executables and workflow lint. Pinned actionlint 1.7.12 was obtained from the official GitHub release API and verified against the pinned SHA-256. This closes the earlier tooling limitation without changing historical test records. Source packaging adds a bilingual release-directory index; generated firmware, local tools, backups and private device logs stay out of the source commit. Historical relay bundles and test artifacts are preserved.
