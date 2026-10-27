# Folo Music 2.0 架构

正式产品为 ESP32-C3 / 8 MiB Flash / 无 PSRAM 的设备端开源协议直连播放器。2.0 不需要电脑中转或官方开放平台设备参数；手机仅用于网易云 App 扫码授权。

## 产品入口与界面

- firmware/ai-passport/main/main.c 启动本地配网或播放器，不进入上游 demo 菜单。
- wifi_provisioning_page.c 扫描 2.4 GHz 网络，使用三键屏幕键盘输入密码，后台连接成功后保存配置；新连接失败不覆盖旧网络。连接/扫描任务不阻塞按钮回调。
- wifi_provisioning_logic.c 是可独立测试的键盘与连接逻辑；历史 wifi_provisioning_portal.c 不再编译进生产入口，不启动 SoftAP/HTTP 配网服务。
- netease_app_logic.c 管理歌单光标、歌曲浏览光标、当前播放索引及导航/设置/确认；浏览和播放状态分离。
- device_music_ui.c 和 device_music_ui_spec.c 实现方案 B 日夜界面、状态栏、硬件键提示和滚动文字。worker 发布结果时持 LVGL 锁，加载完成不抢回用户已离开的页面。

## 直连协议与持久化

- firmware/components/netease_direct_client/ 实现 EAPI/WEAPI 请求、QR 状态、账号验证、歌单、歌曲与媒体接口；代码参考开源协议，正式版本不依赖 Node 服务。
- main/netease_direct_adapter.c 把直连结果映射为播放器使用的紧凑结构。会话保存在独立 NVS 命名空间，启动验证后恢复，失效时要求重新扫码。
- 单 worker 管理同源 WEAPI 连接；切换 EAPI 前释放，错误时销毁失效连接。只读请求的空响应允许一次重建，不重放登录/有副作用请求或已收到部分正文的请求。
- 采用 ESP-IDF 握手后固定 16 KiB RX 策略，TLS 发送记录 1 KiB，HTTP 请求头缓冲 4 KiB；不关闭证书验证，仅在验证后释放对端证书。Wi-Fi 使用六个静态 RX、十二个动态 RX/TX 缓冲。
- WEAPI 请求准备在 RSA 前释放第一层 AES 结果，URL 编码直接写最终请求体；受测编码器峰值 1626 字节，不包含共享 TLS 内部申请。
- 显示偏好保存在 music_display NVS；更换网络通过一次性 RTC_NOINIT 标记软重启进入配网，不擦除网易云会话。退出账号仅清会话。

## 缓存、音频与媒体

- netease_app.c 保留三页 RAM 元数据缓存，每批八首、界面四行，五分钟过期；热页直接展示。账号切换清缓存。
- 当前浏览歌单的歌曲编号最多保留 512 项滑窗。内存允许且播放器状态允许时尝试相邻页预取，失败退避；不缓存音频、不承诺离线播放。
- 当前歌单列表仍最多 20 项，未实现无限歌单分页。暂停不释放全部解码资源，低内存时可能跳过预取。
- firmware/components/audio_stream/ 处理 HTTP MP3 拉流、Helix 解码、立体声转单声道、源采样率 PCM 输出、音量与进度。8 KiB 压缩输入低于 2 KiB 或解码需要更多数据时批量补充，HTTP RX 为 1 KiB。
- BSP 使用六个 240 帧 DMA 缓冲，启动双声道数据 5760 字节；音频硬件由播放任务按需打开/释放。网络读取、TLS 和显示仍竞争 C3 内存，Flash 余量不能代替 RAM 预算。
- music_artwork.c 下载并解码当前曲封面；失败不创建图片控件，文字恢复全宽。music_lyrics.c 解析 LRC 并按播放进度显示，保留文本按实际大小缩紧；歌词、封面需要独立网络与解码资源。

## 字库与 Flash

- tools/generate_scheme_b_fonts.py --full 生成 9/10/12/14/25 px 动态字库：各含 6,774 汉字（完整 GB2312 加 11 个姓名用字）、52 个英文大小写字母、数字及界面标点，不额外扩充繁体和其他语种。
- 字形为 Flash 中的 2bpp XOR/RLE，检查源字体真实 cmap、逐字压缩回读、原行高基线和 20bit 位图偏移；当前各字号一块位图，生成器支持必要时按 1 MiB 拆分为静态 fallback 字库。
- 同步生成 server/src/generated/deviceCharset.ts，仓库检查保证服务端允许字符均存在于动态字库。字体许可保留在 main/fonts/font-licenses/。
- factory 起点 0x10000、大小 0x700000；NVS 0x9000/0x6000，PHY 0xf000/0x1000 不变，末尾剩 960 KiB 未分配。无双槽 OTA。
- 2.0 已验证应用 3,883,184 字节，应用分区剩 3,456,848 字节（约 3.30 MiB）。发布打包器至少要求 512 KiB 余量。
- tools/package_release.py 只读取构建文件，严格生成 8,388,608 字节全镜像和 app，并输出 SHA-256 清单；全镜像 NVS/PHY 必须为 0xFF。完整烧录清空设置，匹配分区时 app-only 升级可保留设置。

## 测试和发布

- C 主机回归、字体测试、原生日夜渲染、打包测试及固件合并校验分开报告；生产入口不使用测试夹具。
- PassportSim 在独立诊断版本覆盖部分配网流程，但当前生产 SDK/API 未完整支持。它不替代真机听感、峰值内存与长时间并发验收。
- 用户实测暂未遇到新 bug，已于 2026-10-28 授权发布 2.0；不据此宣称所有曲库和网络条件均已覆盖。[发布说明](docs/releases/v2.0.md)保留兼容性与验收限制。

## 历史代码边界

- server/ 保留 Fastify、SQLite、设备注册/鉴权、加密会话和音频代理；仅供旧中转设备使用，不是 2.0 的必需组件。
- firmware/components/netease_client/ 保留中转兼容客户端；默认 Kconfig 仍允许中转，正式发布显式使用 sdkconfig.direct.defaults。
- netease-official-firmware/ 是官方开放平台历史实验，不作为个人设备的产品接入路线。
- 旧方案、调试过程和容量实验保存在 docs/archive/、直连计划及加载诊断文档，不删除既有发布包或历史来源。
