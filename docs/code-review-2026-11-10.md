# 代码审阅与版本更新

时间：2026-11-10（Asia/Shanghai）；来源设备：Ubuntu 开发电脑。

后续交付：用户已授权 2.2.0 实机烧录、完整 BIN、文档/知识库同步及 main 源码推送。已生成严格 8 MiB 包并仅刷应用，verify_flash 和保留设置区域比对通过，启动版本确认为 2.2.0；首次原 Wi-Fi 不可见并超时；用户打开原热点后复测，联网、校时和平台选择通过，未重跑登录与完整播放。新增打包 CLI --version 及回归，当前为 21 个 C 程序和 14 项 Python 测试通过。最终交付见 [2.2.0 说明](releases/v2.2.0.zh_CN.md)及[当前状态](CONTEXT.md)。以下为审阅阶段记录，两项业务问题仍未修复。

本轮确认两项问题，尚未修复。按用户要求审阅并更新版本，不擅自修改播放行为。初次更新为 `2.2.0-dev.1`；用户随后明确要求采用正式版本号，现设为 `2.2.0`。未对外发布，设备验收状态不因版本号变化而改变。

## 审阅范围

以当前工作区（包括既有未提交和新增文件）为准，重点检查当前产品主路径：双平台登录与会话、歌曲分页、播放队列、模式保存、UI/worker 交互、音频下载/解码、版本及验证工具。抽查配网工具和历史中转服务入口。历史 `netease-official-firmware` 实验、生成字库和第三方依赖未逐行审计；本轮不是全库无缺陷保证，也未对真实账号和设备注入故障。

## 发现的问题

### 缓冲时暂停只改变界面，音频仍会继续播放

- 位置：`firmware/ai-passport/main/netease_app.c:2084`，相关接口 `firmware/components/audio_stream/audio_stream.c:335`。
- 触发：首次缓冲或网络断粮后的再次缓冲期间，短按 OK 暂停。
- 原因：状态机先把 `s_logic.playing` 改为 false，但控制分支仅在 `AUDIO_STREAM_PLAYING` 时调用 `audio_stream_pause()`，遗漏接口本来支持的 `AUDIO_STREAM_BUFFERING`。任务仍活跃，因此也不会进入停止分支；随后返回“Playback paused”。缓冲完成仍会输出声音，界面与真实状态不一致。
- 验证：从当前 `netease_app.c` 提取完整 TOGGLE_PLAYBACK 分支，在临时主机 C 程序中提供缓冲状态及接口桩，实际执行得到 `pause_calls=0 engine_still_buffering=1 ui_playing=0 message=Playback paused`。这是控制逻辑复现，不是实机测听。
- 修复方向：缓冲和播放状态均转交暂停接口，只有调用成功后确认 UI 状态；补充缓冲期间暂停/恢复的集成回归。

### 音频真正启动之前就消费播放队列

- 位置：`firmware/ai-passport/main/netease_app.c:1102`、`:1106`、`:1108`；相关实现 `firmware/components/audio_stream/audio_stream.c:313`，错误处理 `firmware/ai-passport/main/netease_app.c:1739`。
- 触发：切歌获取 URL 成功，音频任务创建成功，但后续下载连接/TLS、解码器分配或首帧解码失败。
- 原因：`audio_stream_start()` 的成功只代表 `xTaskCreate()` 成功；实际分配、连接及解码发生在另一个任务。调用方立即接纳候选队列、commit 目标并更新当前歌曲。稍后 tick 发现音频错误只设置重试信息，没有回滚队列。因此尚未成功播放的歌曲已成为队列当前项，破坏方案中“网络失败不消费目标”的事务约定，影响随机轮次及回退历史。
- 验证：静态追踪了启动、commit、异步错误和重试调用链。现有主机队列测试只验证独立 peek/commit 行为，不能覆盖应用何时 commit；本轮未做设备 TLS 故障注入。
- 修复方向：保留待提交目标，收到明确首帧/播放就绪信号后再 commit；启动失败保留旧队列和历史，重试同一待选目标。

## 本轮修改

- 更新产品 CMake 版本及中英文 Changelog；独立 QQ 预检版本 `2.1.1-qq-preflight` 保留。
- 更新 README、文档索引及当前状态，区分源码新版本与此前设备版本。
- 保留用户已有改动；未修复上述业务问题，未创建分支/worktree，未提交、推送、发布或烧录。

## 验证结果

- 静态门禁通过：仓库检查及 workflow lint；21 个 C 主机测试程序通过、0 失败；13 项 Python 测试通过、0 失败（9 项登录工具、4 项打包）。
- ESP-IDF 5.5.4 产品直连构建通过，实际配置 `CONFIG_NETEASE_DIRECT=y`。初次命令因已有 CMake Python 包未在路径中而失败；补充本机 `.local-backup/hotspot-tools` 的 `PYTHONPATH` 后重跑通过，未安装或升级依赖。
- 合并镜像校验通过：bootloader 21,024 字节，分区表 3,072 字节，应用 3,950,784 字节；合并文件 4,016,320 字节。此文件是构建目录的合并镜像，未生成新的严格 8 MiB 发布包。
- 初次构建核验版本为 `2.2.0-dev.1`；随后按用户要求改为 `2.2.0` 并重新构建、校验合并镜像及内嵌版本。
- 产品固件按 README 指定的直连配置构建，没有以 `validate.sh --firmware` 的上游默认中转配置代替产品验证。
- 设备测试未运行：长期播放/TLS 内存余量、网易云及平台切换回归、完整随机轮次/上一首仍待实机验收。旧服务端未运行动态测试。

本机临时证据：`/tmp/folomusic-review-static.log`、`/tmp/folomusic-review-build.log`、`/tmp/folomusic-review-merge.log`、`/tmp/folomusic-review-buffering.c`。临时文件不属于发布内容。
