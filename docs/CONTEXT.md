# 当前状态

更新时间：2026-11-10T18:40:20+08:00（Asia/Shanghai）；来源设备：Ubuntu 开发电脑。

- 当前版本 Folo Music **2.2.0**，ESP32-C3 / 8 MiB / 无 PSRAM，网易云与 QQ 音乐设备直连；含顺序/随机、模式持久化、独立平台会话、统一设置与退出。保留 MIT，协议独立实现。
- 本地产物 `release/FoloMusic-v2.2.0/`：完整 BIN 8,388,608 字节，应用 3,950,784 字节，7 MiB 应用分区剩 3,389,248 字节。完整 SHA-256 `2c27c232d353a73b8c2cf519affdc8c1be88d871e381907067d196328304de96`；应用 SHA-256 `d2fed8aba1bd586a236e56a596dca31d8c2c04303bef52bc7e42c7aeb36710b1`。
- ESP-IDF 5.5.4 直连构建、合并镜像、内嵌版本、严格 8 MiB 打包/分段/空白 NVS/PHY/哈希与回读校验通过。静态门禁：21 个 C 主机程序、14 项 Python 测试（9 项登录工具、5 项打包）及仓库/workflow 检查通过。新增打包 CLI --version 回归先复现失败，再修正通过。
- 实机原版 2.2.0-dev，先做 8 MiB 私有备份，分区表与构建一致；仅写 app 0x10000 并独立 verify_flash 校验通过。写入后、启动前 0x0–0xffff 与备份逐字节一致，保留 bootloader、分区表、Wi-Fi、账号和显示 NVS/PHY。启动日志确认为 2.2.0，ELF 前缀 6d3a2c743。
- 首次 45 秒启动观察原 Wi-Fi 未找到（reason=201），最终超时；用户打开原热点后重启复测，约 2.6 秒取得 IP、完成校时，约 3.1 秒进入平台选择（connected=1），未见崩溃。当前烧录/启动/联网及平台入口通过，未重跑登录与完整播放。设备版本已更新；以本条新证据覆盖旧记录“设备仍为 2.0/2.1/开发版”的当前状态判断。
- 两项审阅问题仍未修：缓冲态暂停仅改 UI、异步播放成功前提前提交队列。QQ 封面/歌词未实现；长期播放/TLS、完整随机轮次/上一首、网易云与平台切换/退出、非空 QQ 收藏、弱网仍待验收。此前独立预检整曲/暂停继续/重启恢复及开发版随机跨页切歌均为历史证据，不算本次重测。
- 仓库源码交付提交 `fd7afeb1ec147984cefc96d3d9ec6f482dda8d95` 已推送现有 main，83 份源码/测试/文档；GitHub API 树和提交 SHA 与本地一致，非强制快进，未创建分支、标签或 GitHub Release。常规 Git HTTPS 推送无响应后使用已有 API 通道，未改全局配置。二进制、私有备份、原始日志与本机工具均未入提交；本状态页另作交付结果补记。
- Obsidian 八份项目档案、两份入口及本次任务记录共 11 份已更新；Syncthing 核验 Phoenix-PC（Windows 主库）持有当前全局版本，完成率 100%、待同步 0。最终提交结果补记后再次核验接收；不清理历史冲突文件、不推进全库周度归档。

## 入口与历史

- [2.2.0 中英说明](releases/v2.2.0.zh_CN.md)、[部署与构建](deployment.md)、[审阅问题](code-review-2026-11-10.md)。
- [交付前开发状态快照](archive/state-before-v2.2.0-delivery-2026-11-10.md)、[QQ 实机预检](qqmusic-device-validation.md)、[播放模式方案](playback-mode-plan.md)、[QQ 直连方案](qqmusic-direct-plan.md)。
- 完整镜像写 0x0 会清空两平台账号及 Wi-Fi/播放/显示偏好；仅匹配 7 MiB factory 布局时可用 app 保留设置升级。公开镜像只取构建产物，不读取设备 NVS。
- Ubuntu 本机 SDK `/home/phoenix/.espressif/esp-idf-v5.5.4`，IDF_TOOLS_PATH=`/home/phoenix/.espressif`，Python 环境 `idf5.5_py3.12_env`；构建 `firmware/ai-passport/build-hotspot`。本机 CMake/Ninja/Zig 在忽略目录 `.local-backup/hotspot-tools`，需要相应 PATH/PYTHONPATH。设备私有证据在 `.local-backup/delivery-2.2.0/`，禁止提交或复制到知识库。
