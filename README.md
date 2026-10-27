# Folo Music 2.0

把 FoloToy AI Passport 变成独立的网易云音乐播放器。正式版由设备通过开源 EAPI/WEAPI 协议直接访问网易云和音频 CDN，无需电脑中转，不使用网易云官方开放平台设备方案。

用户于 2026-10-28 反馈当前暂未发现新 bug，并确认发布 2.0。此反馈不等于已完成全部长期压力测试，验收边界见[发布说明](docs/releases/v2.0.md)。

## 下载与安装

正式版标签：v2.0-folo-music。附件统一生成到 release/FoloMusic-v2.0/，包含：

[正式版本页面](https://github.com/PhoenixZHC/FoloToy-wyy-musicplayer/releases/tag/v2.0-folo-music) · [完整 8 MB 固件](https://github.com/PhoenixZHC/FoloToy-wyy-musicplayer/releases/download/v2.0-folo-music/FoloMusic-v2.0-full-8MB.bin)。

- FoloMusic-v2.0-full-8MB.bin：完整出厂镜像，严格 8,388,608 字节，从 0x0 烧录。**会清空原 Wi-Fi、登录态和显示设置**。
- FoloMusic-v2.0-app.bin：仅适用于已有匹配 7 MiB factory 分区的设备，从 0x10000 升级以保留设置。
- manifest.json 和 SHA256SUMS.txt：容量、分段及 SHA-256 校验清单。

镜像由干净构建产物生成，NVS/PHY 空白区填充 0xFF，不含任何设备密码或登录态。旧 3 MiB 分区设备不能直接写 app。具体操作见[部署与构建](docs/deployment.md)。

## 操作

1. 设备扫描 2.4 GHz Wi-Fi，↑↓选网络、OK 确认。
2. 屏幕键盘：↑↓移动、OK 输入；长按↑↓切换字符组，长按 OK 打开连接、删除、显隐和返回菜单。连接成功后保存密码。
3. 用网易云音乐 App 扫描设备二维码并确认授权。手机只用于扫码，不负责 Wi-Fi 配网。
4. 歌单与歌曲列表：↑↓选择、OK 打开或播放；长按↑↓快速移动。
5. 播放页：↑上一首、↓下一首、OK 暂停/继续；长按↑↓进入音量调节、OK 完成。
6. 长按 OK 打开导航，快速返回播放页、歌曲列表、歌单和设置；子菜单长按 OK 返回。
7. 设置支持日夜配色、亮度、网络与账号。断网/错误页按↓可进入网络设置重新选网。

## 功能与边界

- 方案 B 日夜界面、Wi-Fi 信号图标、进度、音量、三键底栏提示；关于页版本 2.0。
- 当前歌曲封面和同步歌词；封面失败不占位，过长歌名/歌手滚动显示。
- 每批八首元数据、每屏四行、三页缓存及内存允许时的相邻预取；编号最多 512 项滑窗。冷请求仍受网络及 RAM 限制。
- 动态字库含 6,774 汉字（GB2312 全集加 11 个姓名用字）、英文大小写、数字及界面标点，不额外扩充繁体或其他语种。
- 当前最多显示 20 个歌单；无随机播放切换、离线音频或双槽 OTA；不支持隐藏 SSID 和企业认证。
- 歌曲版权/会员权限、网络与非官方接口变化可能影响可播放性。不承诺所有歌曲可播或长期零卡顿。
- 当前应用 3,883,184 字节，7 MiB 应用分区剩 3,456,848 字节（约 3.30 MiB）；Flash 余量与运行 RAM 不同。

## 项目目录

    firmware/ai-passport/        产品入口、BSP、字库、测试与构建工具
    firmware/components/        直连协议、兼容中转客户端和音频流
    docs/                       使用、设计、验收及文档索引
    docs/prototypes/            HTML 设计与预览
    docs/releases/              正式版本说明
    docs/archive/               历史开发状态和旧部署说明
    release/FoloMusic-v2.0/      本机生成的发布附件（不提交二进制）
    server/                     旧中转服务源码，保留兼容
    netease-official-firmware/   官方平台历史实验，不是当前方案

旧 v1.2.2 中转包、启动脚本和官方实验保留供历史使用，2.0 无需运行它们。生成物、私有日志、数据库、NVS 与依赖缓存不混入源码提交。

## 从源码构建

使用 ESP-IDF 5.5.4，ESP32-C3，8 MiB Flash，无 PSRAM。生成字库已随源码提交，普通编译无需 Pillow。发布时显式使用直连配置，不复用旧中转 sdkconfig：

    cd firmware/ai-passport
    idf.py -B build-direct -D SDKCONFIG=build-direct/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.direct.defaults" build
    idf.py -B build-direct merge-bin -o build-direct/FoloToy-Wangyiyun-full.bin
    python tools/verify_firmware.py build-direct
    python tools/package_release.py build-direct ../../release/FoloMusic-v2.0

默认 Kconfig 保留旧中转兼容选项；正式 2.0 始终按上述直连配置构建。验证、字体重生成和模拟器边界见[部署说明](docs/deployment.md)与[构建指南](firmware/ai-passport/docs/development/build-and-test.md)。

[文档索引](docs/README.md) · [2.0 发布说明](docs/releases/v2.0.md) · [架构](ARCHITECTURE.md) · [完整交互原型](docs/prototypes/scheme-b-complete.html)。

## 开源方案与致谢

Folo Music 建立在开源社区的工作之上，感谢以下项目的作者、维护者及贡献者：

| 项目 / 作者 | 在本项目中的用途 |
| --- | --- |
| [FoloToy AI Passport / FoloToy](https://github.com/FoloToy/ai-passport) | 固件工程、设备 BSP 和硬件基础，是播放器的直接上游。 |
| [ESP-IDF / Espressif](https://github.com/espressif/esp-idf) | ESP32-C3 系统、Wi-Fi、TLS、NVS、I2S 和构建工具链；托管音频及显示组件遵循各自许可。 |
| [LVGL / LVGL contributors](https://github.com/lvgl/lvgl) | 设备图形界面、文字排版、滚动及日夜页面渲染。 |
| [esp-libhelix-mp3 / Chris Morgan](https://github.com/chmorgan/esp-libhelix-mp3) 与 Helix 项目贡献者 | 通过托管组件提供设备端 MP3 解码；组件及底层解码器分别遵循其原始许可证。 |
| [api-enhanced / NeteaseCloudMusicApiEnhanced](https://github.com/neteasecloudmusicapienhanced/api-enhanced) | 网易云开源协议参考。设备端实现独立 C 客户端，不要求用户部署该 Node 服务。 |
| [leo-radio / leo0183](https://github.com/leo0183/leo-radio) | 联网事件驱动与音频资源管理的参考；未整库移植。 |
| [PassportSim / BlackHole1](https://github.com/BlackHole1/passportsim) | 电脑端固件仿真及自动化诊断工具；对当前生产 SDK 的支持有限，不宣称完整仿真验收已通过。 |
| [Noto CJK / Noto contributors](https://github.com/notofonts/noto-cjk) | 简体中文字形来源，包括 Noto Sans/Serif CJK。 |
| [URW Base35 / Artifex 与贡献者](https://github.com/ArtifexSoftware/urw-base35-fonts) | Nimbus Roman 数字字形来源。 |
| [Pillow / python-pillow](https://github.com/python-pillow/Pillow) 与 [FreeType](https://freetype.org/) | 构建阶段的字形栅格化、字符覆盖检查与预览生成，不在设备上运行。 |

上游许可保留在 [firmware/ai-passport/LICENSE](firmware/ai-passport/LICENSE)，字体许可证保留在 firmware/ai-passport/main/fonts/font-licenses/，其余依赖遵守各自许可证。参考开源实现不代表获得音乐内容授权，账号和内容使用仍须遵守服务条款与版权要求。感谢开源作者让这台小设备成为可能，也欢迎提交问题、改进和真实测试反馈。
