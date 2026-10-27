# 播放器开发过程记录（2026-10）

以下保留实施时的候选结果、沟通背景与诊断记录，仅供开发追溯，不代表当前产品状态。当前版本功能和限制见[发布说明](../releases/v2.0.md)。

## English

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

## 简体中文

### 历史实施记录

以下按实施时状态保留，旧候选未验收和故障描述不代表新的发布状态；当前验收边界见仓库根 docs/releases/v2.0.md。

- 动态界面字库保留完整 GB2312 并补充 11 个姓名用字，各含 6774 汉字、52 个英文大小写字母、数字和界面标点。用户明确仅需简体和英文，已撤回额外繁体及其他语种扩充。校验源字体真实覆盖、RLE 回读及 20bit 位图偏移上限，保留原行高基线。固件版本设为 2.0，移除顺序播放文案及播放/列表顶部品牌。直连应用 3,883,184 字节，7 MiB 分区剩 3,456,848 字节（约 3.30 MiB）。3 个字体测试、12 个主机程序、98 次日夜原生渲染、两种构建和合并镜像校验通过，真机显示及播放并发仍待验收。

- WEAPI在RSA前释放第一层AES结果，URL编码直接写入最终请求体。真实八首请求加密的3072字节受限堆回归先失败后通过，受测编码器峰值从3187降至1626字节；不包含共享mbedTLS内部申请，不能代替设备堆仿真。主机门禁现运行12个程序，修正此前过时计数。

- 固定TLS RX候选在真机复现I2S DMA申请失败后，恢复SDK默认的6个240帧DMA缓冲。启动双声道DMA数据从16,384降至5760字节，压缩输入仍为8 KiB；音频初始化前后记录内部堆。播放与列表并发仍待真机验收。

- 可复用WEAPI连接采用ESP-IDF官方握手后固定RX策略，保留完整16 KiB接收记录。直连TLS按1 KiB发送分片，HTTP请求头缓冲仍4 KiB；仅在验证完成后释放对端证书。此前短连接试验虽回收24,104字节，但重新握手仍耗尽堆，已撤回。固定RX策略回归通过，真机并发验收仍待完成。

- 增加可重复执行的PassportSim WASM诊断脚本。独立IDF 5.5.3构建仅在临时副本省略模拟器不支持的esp_wifi_set_ps后通过九项配网检查；生产联网和播放验收受阻，未再烧录。复用同源WEAPI连接，仅为明确只读查询在无正文的请求头失败后重建一次；回归排除EAPI、部分响应和无限重试。播放中TLS内存申请失败尚未解决。详见仓库根docs/loading-validation.md。

- 压缩音频低于2 KiB时批量补充，解码缺数据时强制读取；保留8 KiB输入容量，重复的HTTP RX缓冲缩至1 KiB。歌词按实际长度缩紧。直连版使用6个静态Wi-Fi RX缓冲及12个动态RX/TX缓冲，为TLS留内存。歌单编号请求n=0，再获取当前页歌曲信息。主机检查和直连固件验证通过；播放翻页及响应大小效果仍待真机验收。

- 直连构建启用动态 TLS 收发缓冲，保留 16 KiB 最大接收记录。真机确认缩小音频缓冲并重试后，播放中仍会发生 TLS 初始化内存分配失败。静态检查、10 项主机可执行测试、直连构建、合并镜像验证及应用烧录哈希通过，NVS 全部字节一致。启动时 Wi-Fi 认证超时，播放与列表并发验收仍待完成。

- 播放期间音频输入缓冲从 16 KiB 降至 8 KiB，为元数据 HTTPS 请求释放更多内存；歌曲请求遇到连接失败、内存不足和超时时，短暂重试两次后再显示错误。已验证应用写入哈希且 NVS 未变化；重启后 Wi-Fi 认证超时，并发播放/加载列表的验收仍待完成。

- 播放页歌词从 10px 放大至 14px，显示区域从 16px 增至 42px，可显示两行；歌曲信息上移、进度条下移，歌词使用日夜主题主文字色，避免挤压和控件重叠。

- 播放页接入封面及逐行歌词；封面缺失、下载或解码失败时不留占位，文字区域恢复全宽。歌名和歌手名仅在超出显示宽度时滚动。真机媒体和滚动验收待做。

- 每次请求八首歌曲元数据，界面仍显示四行。三页缓存和相邻页预取覆盖更大的窗口，减少翻歌时的可见等待；直连客户端按当前位置保留最多 512 个歌曲编号的滑动窗口，不缓存音频。

- 头缓冲修复后的真机验证：账号校验、20 个歌单、歌曲列表、相邻页预取及 3 次音频地址请求均返回 `err=0`；监听未发现头警告、EAGAIN、看门狗或 panic。实际声音和长时间播放仍待用户确认。

- 修复直连网易云 HTTPS 请求头：EAPI 和 WEAPI 统一使用 4096 字节发送缓冲。此前 WEAPI 的 1024 字节缓冲无法容纳最大会话 Cookie，会产生请求头不完整警告；本次烧录后设备已成功验证账号并读取 20 个歌单。

- 修复设备 Wi-Fi 配网页卡死：密码键盘每个字符创建独立控件会耗尽 24 KiB LVGL 堆并触发绘图分配断言循环；改成三行按键矩阵。受限堆预览、直连/中转构建、主机测试和日夜渲染通过；直连版已烧录且 NVS 保持不变，实体日志确认输入、提交、获取 IP 和 Wi-Fi 连接成功，未再触发看门狗；后续网易云 HTTP 错误仍待修复。

- 密码输入改为可见屏幕键盘：QWERTY 字母、数字、符号和空格，当前键高亮，支持日夜、遮罩和长度提示。↑↓移动、OK 输入、长按↑↓换组、长按 OK 打开操作菜单。直连版仅烧录 0x10000，NVS 24,576 字节一致；实体输入和新网络连接尚待验收。

- 手机配网替换为设备扫描 2.4 GHz Wi-Fi 和三键密码编辑，支持字符组、删除、显隐和校验；生产入口移除 SoftAP/HTTP 配网。新网络连接成功后才保存，保留网易云会话。暂不支持隐藏网络和企业认证；中转模式需要已有注册配置。
- 歌曲改为四首分页和有内存门槛的相邻页预取；真实翻页及暂停音频内存验收仍待完成。
- Wi-Fi 信号改为三格图标，不显示 RSSI 数值。错误页提供网络设置入口，配网失败页支持重试及更换网络；进入配网仅重启到配置页，不清除已有 Wi-Fi 或网易云登录态。

- 底栏在动作前显示实体按键 ↑ / OK / ↓，日夜模式和 HTML 原型同步；无动作保持横线。重新构建并烧录直连版，NVS 逐字节保留，启动确认登录态恢复和 20 个歌单读取成功。

- 方案 B 移植进行中：日夜 LVGL、浏览播放快照、独立确认、有界元数据/编号缓存和中文扩库。补齐首次欢迎与认证/异常状态，修正长按后第一次短按失效并保留浏览光标；原生预览 70 个日夜夹具无标签缺字。主机检查通过；用户批准 factory 扩至 7 MiB 后，直连/中转构建及合并镜像校验通过。直连应用烧录哈希通过，重启前 NVS 24,576 字节完全一致；完整真机功能和视觉验收仍待完成。

- 在取新歌曲地址前停止旧音频流，避免 C3 上解码/CDN 与 API TLS 并存耗尽内存。真机已完成两首歌之间的切换、CDN HTTP 200 及 44.1 kHz MP3 解码，听感与较长稳定性测试仍待验收。

- 为 EAPI 请求内的真实会话令牌预留有界容量。真机已读取 20 个歌单及 15 首歌曲，音频播放尚未通过验收。

- 处理歌单接口返回超过请求上限的情况：校验完整响应、保留前 20 项，并在列表计数显示 `+` 表示还有更多。真机已确认重启复用登录态，歌单修正与播放待验收。

- 将扫码和 EAPI header/Cookie/域名对齐固定版本的开源 api-enhanced，实现真实设备扫码与账号验证成功；歌单处理仍报大小错误，正在排查。新增请求解密回归测试及脱敏诊断。

- 新增可选的 ESP32-C3 网易云直连客户端：扫码登录、本地 NVS 会话、歌单和歌曲流式解析、CDN URL 校验及仅 Wi-Fi 配网。默认仍使用中转版，直连功能待真机验收。

- 将网易云固件屏幕替换为独立的 240x320 设备 UI：深色音乐播放器布局、硬件按键底栏、扫码/登录/列表/播放/错误页面、生成的中文子集字库，以及用于对照 Figma 的本地预览渲染器。
- 新增独立网易云播放链路：固件可通过 HTTP 拉取 MP3 URL，使用 `chmorgan/esp-libhelix-mp3` 解码，转成单声道 PCM，并由网易云设备 UI 控制播放。
- 扫码登录页和等待确认页在二维码轮询任务忙碌期间也允许长按 OK，清除已保存的 Wi-Fi、中转注册信息并返回配网页。
- 修复设备已连接 Wi-Fi 时重复提交配网会直接失败的问题：网络相同时复用现有连接，网络变化时先断开再重新连接。
