# Folo Music 2.2.0 架构

产品为 ESP32-C3 / 8 MiB Flash / 无 PSRAM 的设备端网易云 / QQ 音乐双平台直连播放器。无需电脑中转或官方开放平台设备参数；手机仅用于对应音乐 App 扫码授权。

## 产品入口与界面

- firmware/ai-passport/main/main.c 启动本地配网或播放器，不进入上游 demo 菜单。
- wifi_provisioning_page.c 扫描 2.4 GHz 网络，使用三键屏幕键盘输入密码，后台连接成功后保存配置；新连接失败不覆盖旧网络。连接/扫描任务不阻塞按钮回调。
- wifi_provisioning_logic.c 是可独立测试的键盘与连接逻辑；历史 wifi_provisioning_portal.c 不再编译进生产入口，不启动 SoftAP/HTTP 配网服务。
- netease_app_logic.c 管理歌单光标、歌曲浏览光标、当前播放索引及导航/设置/确认；浏览和播放状态分离。
- display_policy.c 独立计算无操作超时、唤醒和日夜边界；display_settings.c 在主入口统一管理所有页面的背光、自动熄屏及配色，使用单调时钟计时、北京时间 07:00/19:00 切换主题。欢迎、配网和播放器页面共用首键仅唤醒规则；联网 SNTP 校时不改变无操作计时。统一定时器驱动顶部居中 HH:mm 时钟，仅在分钟或校时状态变化时更新标签；页面清理时移除标签引用，时间刷新不唤醒背光。
- device_music_ui.c 和 device_music_ui_spec.c 实现方案 B 日夜界面、状态栏、硬件键提示和滚动文字。worker 发布结果时持 LVGL 锁，加载完成不抢回用户已离开的页面。

## 直连协议与持久化

- main/music_provider.c 统一平台生命周期、歌单、分页歌曲和取流映射；联网后选择平台，music_platform 保存上次选择。退出仅清当前平台会话，切换平台释放运行时资源并清 UI 和队列。
- firmware/components/qqmusic_direct_client/ 独立实现 QQ HTTPS、MQTT-over-WebSocket 登录、PNG 二维码及有界 JSON 投影；qqmusic NVS 与网易云会话隔离。正式入口使用公共配网；独立预检的 qq_test_wifi 不作为产品配网来源。
- main/music_playback_order.c 提供顺序/随机 peek/commit、轮次与上一轮历史；identity 包含平台、账号、歌单、内容版本。music_playback NVS 保存模式。随机当前轮、上一轮及待提交新轮合计每条目 12 字节；按实时内部堆和最大连续块限制，失败明确提示。
- firmware/components/netease_direct_client/ 实现 EAPI/WEAPI 请求、QR 状态、账号验证、歌单、歌曲与媒体接口；代码参考开源协议，正式版本不依赖 Node 服务。
- main/netease_direct_adapter.c 把直连结果映射为播放器使用的紧凑结构。会话保存在独立 NVS 命名空间，启动验证后恢复，失效时要求重新扫码。
- 单 worker 管理同源 WEAPI 连接；切换 EAPI 前释放，错误时销毁失效连接。启动音频前及播放中主动浏览请求结束后释放闲置 API TLS，避免与 CDN RX/解码/DMA 长期争用 RAM，不清登录或歌曲缓存。只读请求的空响应允许一次重建，不重放登录/有副作用请求或已收到部分正文的请求。
- 采用 ESP-IDF 握手后固定 16 KiB RX 策略，TLS 发送记录 1 KiB，HTTP 请求头缓冲 4 KiB；不关闭证书验证，仅在验证后释放对端证书。Wi-Fi 使用六个静态 RX、十二个动态 RX/TX 缓冲。
- WEAPI 请求准备在 RSA 前释放第一层 AES 结果，URL 编码直接写最终请求体；受测编码器峰值 1626 字节，不包含共享 TLS 内部申请。
- 显示偏好保存在 music_display NVS：亮度、自动熄屏秒数和校时前使用的上次配色；NVS 写入由 worker 执行，UI 线程只更新内存。日夜已改为按系统时间自动切换，未完成校时会明确提示。更换网络通过一次性 RTC_NOINIT 标记软重启进入配网，不擦除网易云会话。退出账号仅清会话。

## 缓存、音频与媒体

- netease_app.c 按需分配三页 RAM 元数据缓存，每批八首、界面四行，五分钟过期；播放前及平台/账号切换释放缓存，工作页按请求分配。QQ 额外歌单索引只保留真实编号。
- 当前浏览歌单的歌曲编号最多保留 512 项滑窗。内存允许且播放器状态允许时尝试相邻页预取，失败退避；不缓存音频、不承诺离线播放。
- 当前歌单列表仍最多 20 项，未实现无限歌单分页。暂停不释放全部解码资源，低内存时可能跳过预取。
- firmware/components/audio_stream/ 处理 HTTP MP3 拉流、Helix 解码、立体声转单声道、源采样率 PCM 输出、音量与进度。2026-11-03 未发布修复将下载与解码分为独立任务，以有上限的 16 KiB 流队列衔接，保留 8 KiB 帧输入；首次及断粮后预缓冲 8 KiB，播放中也显示缓冲状态，HTTP RX 为 1 KiB。HTTP 完整性及 Range/If-Range 校验区分正常结束与断流，续传保持解码器状态；取消先等待生产者再释放队列。
- BSP 使用六个 240 帧 DMA 缓冲，启动双声道数据 5760 字节；音频硬件由播放任务按需打开/释放。网络读取、TLS 和显示仍竞争 C3 内存，Flash 余量不能代替 RAM 预算。
- 未发布热点修复在播放连接显式关闭 modem sleep，后台进行有上限的 Wi-Fi 重连和 HTTP 恢复；音频任务存续时暂停推测性列表预取。新增队列与下载栈约 22 KiB，另有上下文/RTOS 开销；首次 TLS 先于声卡/DMA 初始化。分配失败明确报错，实机峰值内存、功耗与户外听感待验，详见[开发与验收说明](firmware/ai-passport/docs/development/hotspot-playback.zh_CN.md)。
- music_artwork.c 下载并解码当前曲封面；失败不创建图片控件，文字恢复全宽。music_lyrics.c 解析 LRC 并按播放进度显示，保留文本按实际大小缩紧；歌词、封面需要独立网络与解码资源。

## 字库与 Flash

- tools/generate_scheme_b_fonts.py --full 生成 9/10/12/14/25 px 动态字库：各含 6,774 汉字（完整 GB2312 加 11 个姓名用字）、52 个英文大小写字母、数字及界面标点，不额外扩充繁体和其他语种。
- 字形为 Flash 中的 2bpp XOR/RLE，检查源字体真实 cmap、逐字压缩回读、原行高基线和 20bit 位图偏移；当前各字号一块位图，生成器支持必要时按 1 MiB 拆分为静态 fallback 字库。
- 同步生成 server/src/generated/deviceCharset.ts，仓库检查保证服务端允许字符均存在于动态字库。字体许可保留在 main/fonts/font-licenses/。
- factory 起点 0x10000、大小 0x700000；NVS 0x9000/0x6000，PHY 0xf000/0x1000 不变，末尾剩 960 KiB 未分配。无双槽 OTA。
- 2.2.0 构建应用 3,950,784 字节，应用分区剩 3,389,248 字节。发布打包器至少要求 512 KiB 余量，并以 --version 2.2.0 校验预期版本。
- tools/package_release.py 只读取构建文件，严格生成 8,388,608 字节全镜像和 app，并输出 SHA-256 清单；全镜像 NVS/PHY 必须为 0xFF。完整烧录清空设置，匹配分区时 app-only 升级可保留设置。

## 测试和发布

- 当前门禁覆盖 21 个 C 主机程序、9 项登录工具和 5 项打包回归。当前已知缺口为缓冲态暂停分支遗漏及异步启动前提前提交队列；两项未修，不能把独立队列测试解释为应用事务验收。见[代码审阅](docs/code-review-2026-11-10.md)及[2.2.0 说明](docs/releases/v2.2.0.zh_CN.md)。
- C 主机回归、字体测试、原生日夜渲染、打包测试及固件合并校验分开报告；生产入口不使用测试夹具。
- PassportSim 在独立诊断版本覆盖部分配网流程，但当前生产 SDK/API 未完整支持。它不替代真机听感、峰值内存与长时间并发验收。
- 用户实测暂未遇到新 bug，已于 2026-10-28 授权发布 2.0；不据此宣称所有曲库和网络条件均已覆盖。[2.0 历史发布说明](docs/releases/v2.0.md)保留当时的兼容性与验收限制；当前功能及 2.1 尚未刷机的边界见[2.1 说明](docs/releases/v2.1.md)。

## 历史代码边界

- server/ 保留 Fastify、SQLite、设备注册/鉴权、加密会话和音频代理；仅供旧中转设备使用，不是 2.1 的必需组件。
- firmware/components/netease_client/ 保留中转兼容客户端；默认 Kconfig 仍允许中转，正式发布显式使用 sdkconfig.direct.defaults。
- netease-official-firmware/ 是官方开放平台历史实验，不作为个人设备的产品接入路线。
- 旧方案、调试过程和容量实验保存在 docs/archive/、直连计划及加载诊断文档，不删除既有发布包或历史来源。
