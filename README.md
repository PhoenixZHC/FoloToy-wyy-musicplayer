# Folo Music 2.3.0

把 FoloToy AI Passport 变成独立的网易云 / QQ 音乐双平台播放器。设备通过独立实现的开源协议直接访问音乐服务和音频 CDN，无需部署中转服务。支持设备选网、平台选择、扫码登录、歌单浏览、顺序/随机播放；两平台支持封面与同步歌词，并新增单页网络连通测试。QQ 播放、封面、同步歌词和网络检测的正常使用路径已由用户实机确认，长期及弱网验证仍待完成。

## 下载与安装

当前正式版本：**2.3.0（2026-11-11）**。新增单页网络连通测试、QQ 专辑封面与同步歌词、歌曲信息的日文与常用韩文字库，以及右上角电量百分比；修复媒体接入后的音频 TLS 内存不足。完整镜像位于 release/FoloMusic-v2.3.0/FoloMusic-v2.3.0-full-8MB.bin，严格 8,388,608 字节；校验值见同目录 SHA256SUMS.txt。

本轮功能已在 2.2.0 开发包上由用户确认：QQ 声音、封面、同步歌词、单页网络测试、日韩文字与统一字号、电量百分比正常。2.3.0 为相同功能的正式版本号构建，本次未重新烧录；设备仍运行已验收开发包。验收与未覆盖范围见[2.3.0 说明](docs/releases/v2.3.0.zh_CN.md)。

当前包与历史包的区别见[交付目录索引](release/README.zh_CN.md)。生成的二进制不提交到源码仓库，新克隆后按下文构建生成；本次源码推送不创建 GitHub Release。

[2.3.0 说明及已知问题](docs/releases/v2.3.0.zh_CN.md)。以下远端链接仅为历史已发布的 2.0（标签：v2.0-folo-music），不是 2.3.0 下载入口：

[正式版本页面](https://github.com/PhoenixZHC/FoloToy-wyy-musicplayer/releases/tag/v2.0-folo-music) · [完整 8 MB 固件](https://github.com/PhoenixZHC/FoloToy-wyy-musicplayer/releases/download/v2.0-folo-music/FoloMusic-v2.0-full-8MB.bin)。

- FoloMusic-v2.0-full-8MB.bin：完整出厂镜像，严格 8,388,608 字节，从 0x0 烧录。**会清空原 Wi-Fi、登录态和显示设置**。
- FoloMusic-v2.0-app.bin：仅适用于已有匹配 7 MiB factory 分区的设备，从 0x10000 升级以保留设置。
- manifest.json 和 SHA256SUMS.txt：容量、分段及 SHA-256 校验清单。
- FoloMusic-v2.0-licenses.zip：上游、组件与字体的原始许可证。

镜像由干净构建产物生成，NVS/PHY 空白区填充 0xFF，不含任何设备密码或登录态。旧 3 MiB 分区设备不能直接写 app。具体操作见[部署与构建](docs/deployment.md)。

## 操作

1. 设备扫描 2.4 GHz Wi-Fi，↑↓选网络、OK 确认。
2. 屏幕键盘：↑↓移动、OK 输入；长按↑↓切换字符组，长按 OK 打开连接、删除、显隐和返回菜单。连接成功后保存密码。
3. 联网后选择网易云或 QQ 音乐，使用对应音乐 App 扫码确认；已保存的账号先验证恢复。手机只用于扫码，不负责 Wi-Fi 配网。
4. 歌单与歌曲列表：↑↓选择、OK 打开或播放；长按↑↓快速移动。
5. 播放页：↑上一首、↓下一首、OK 暂停/继续；长按↑↓进入音量调节、OK 完成。
6. 长按 OK 打开导航，第一项为播放模式，可选顺序或随机；还可返回播放页、当前歌曲列表、歌单和设置。
7. 设置支持日夜配色、亮度、网络、当前平台账号退出和返回平台选择。切换平台保留其他平台账号，退出账号保留 Wi-Fi。断网/错误页按↓可进入网络设置重新选网。
8.「设置 → 网络 → 网络连通测试」单页显示“检测中 / 网络连通 / 网络未连通”，失败保留一句原因；OK 重试，长按 OK 取消并返回。后台检查 Wi-Fi、IP、时间、当前平台 DNS 与 HTTPS，不展示分项或 HTTP 状态码。播放时提示先暂停；资源不足时显示检测未完成。检测只判断平台服务可达，不验证账号、歌曲权限或音频 CDN。

当前源码在「设置 → 显示与日夜 → 熄屏」设置自动熄屏时间：永不、15 秒、30 秒、1/2/5 分钟，默认 1 分钟，重启后保留。无按键操作达到设定时间后关闭背光，音乐继续播放；任意功能键唤醒并恢复原亮度，唤醒这次按键不执行其他操作。日夜配色跟随系统时间，按北京时间 07:00-19:00 使用日间，其余时间使用夜间；联网校时前明确提示等待，并暂用上次配色。所有页面顶部中央持续显示北京时间 HH:mm，未校时显示 --:--；名称、时间、WiFi 和电量统一使用 10 像素字号、相同行高与文字基线，信号条底部对齐文字。这些改动已包含在 2.1 本地固件中。

## 功能与边界

- 日夜两种配色，支持 Wi-Fi 信号图标、播放进度、音量调节和三键操作提示。
- 网易云当前歌曲封面和同步歌词；QQ 音乐也支持专辑封面及按时间行同步的歌词。只缓存当前一张 60×60 封面及最多 8191 字节歌词；无歌词、纯音乐、无时间轴、超限或读取失败分别提示，不做逐字高亮。封面失败显示原因并恢复全宽文字，媒体失败不阻止可用音频播放。
- 歌曲列表支持分页缓存和相邻页面预取；首次加载速度受网络和设备可用内存影响。
- 歌曲信息使用的 9、12、24 号字库各含 12,601 字符，覆盖简体中文、英文、日文 JIS 汉字/假名及 2,350 个常用韩文音节。播放页歌曲名保持 24 号，歌手名和歌词统一 12 号；其他界面字号不扩展日韩字符。
- 右上角显示 CW2017 电量百分比，不可用时显示 `--%`。使用默认 4.2 V Li-Poly profile，未经完整充放电标定。
- 歌单列表有上限；顺序/随机使用全歌单索引队列，随机每条目需 12 字节并受实时堆预算限制，超限明确失败。无离线音频或双槽 OTA；不支持隐藏 SSID 和企业认证。
- 歌曲版权/会员权限、网络与非官方接口变化可能影响可播放性。不承诺所有歌曲可播或长期零卡顿。
- 当前应用大小与 7 MiB 分区余量见发布说明和 manifest；Flash 余量与运行 RAM 不同。
- 已知问题：缓冲时暂停可能只改变界面；音频任务创建后提前提交队列，启动失败不回滚。长期播放和完整平台切换验收未完成，详见[审阅记录](docs/code-review-2026-11-10.md)。

## 项目目录

    firmware/ai-passport/        产品入口、BSP、字库、测试与构建工具
    firmware/components/        直连协议、兼容中转客户端和音频流
    docs/                       使用、设计、验收及文档索引
    docs/prototypes/            HTML 设计与预览
    docs/releases/              正式版本说明
    docs/archive/               历史开发状态和旧部署说明
    release/FoloMusic-v2.3.0/    本机生成的固件产物（不提交二进制）
    server/                     旧中转服务源码，保留兼容
    netease-official-firmware/   官方平台历史实验，不是当前方案

历史中转服务与官方平台实验代码保留供参考，当前版本无需运行这些服务。

## 从源码构建

当前源码采用正式版本号 **2.3.0**；构建验证与实机功能验收分别记录，不以正式版本号代替验收。

使用 ESP-IDF 5.5.4，ESP32-C3，8 MiB Flash，无 PSRAM。生成字库已随源码提交，普通编译无需 Pillow；歌曲名使用 24 号，歌手名和歌词使用 12 号，仅为 9、12、24 三套字号加入日文和常用韩文字符。发布时显式使用直连配置，不复用旧中转 sdkconfig：

    cd firmware/ai-passport
    idf.py -B build-direct -D SDKCONFIG=build-direct/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.direct.defaults" build
    idf.py -B build-direct merge-bin -o FoloToy-Wangyiyun-full.bin
    python tools/verify_firmware.py build-direct
    python tools/package_release.py build-direct ../../release/FoloMusic-v2.3.0 --version 2.3.0

默认 Kconfig 保留旧中转兼容选项；2.3.0 始终按上述直连配置构建。验证、字体重生成和模拟器边界见[部署说明](docs/deployment.md)与[构建指南](firmware/ai-passport/docs/development/build-and-test.md)。

[文档索引](docs/README.md) · [2.3.0 说明](docs/releases/v2.3.0.zh_CN.md) · [架构](ARCHITECTURE.md) · [完整交互原型](docs/prototypes/scheme-b-complete.html)。

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
| [PassportSim / BlackHole1](https://github.com/BlackHole1/passportsim) | 开发期间使用的电脑端固件仿真与诊断工具；对当前生产 SDK 的支持有限。 |
| [Noto CJK / Noto contributors](https://github.com/notofonts/noto-cjk) | 简体中文字形来源，包括 Noto Sans/Serif CJK。 |
| [URW Base35 / Artifex 与贡献者](https://github.com/ArtifexSoftware/urw-base35-fonts) | Nimbus Roman 数字字形来源。 |
| [Pillow / python-pillow](https://github.com/python-pillow/Pillow) 与 [FreeType](https://freetype.org/) | 构建阶段的字形栅格化、字符覆盖检查与预览生成，不在设备上运行。 |

上游许可保留在 [firmware/ai-passport/LICENSE](firmware/ai-passport/LICENSE)，字体许可证保留在 firmware/ai-passport/main/fonts/font-licenses/，其余依赖遵守各自许可证。参考开源实现不代表获得音乐内容授权，账号和内容使用仍须遵守服务条款与版权要求。感谢开源作者让这台小设备成为可能，也欢迎提交问题、改进和真实测试反馈。
