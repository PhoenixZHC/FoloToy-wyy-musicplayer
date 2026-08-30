# ARCHITECTURE

## 服务端模块

- `server/src/app.ts`：创建 Fastify 应用，装配数据库、设备鉴权、会话存储和网易云路由。
- `server/src/db.ts`：打开 SQLite，并执行初始迁移。
- `server/src/services/deviceAuth.ts`：设备首次注册、设备密钥加密保存、请求签名校验。
- `server/src/services/sessionStore.ts`：按设备保存网易云登录会话，cookie 加密入库。
- `server/src/services/neteaseApi.ts`：网易云接口适配层，把网易云响应裁剪成设备需要的小结构；当前为临时兼容实现，正式方案应按 `docs/netease-official-api.md` 中确认的官方开放平台文档替换。
- `server/src/routes/deviceRoutes.ts`：设备注册和 `hello` 接口；注册入口带限流，并区分坏请求、重复注册和鉴权失败。
- `server/src/routes/neteaseRoutes.ts`：二维码登录、退出、歌单、歌曲列表、歌曲 URL 和本地音频中转接口；入口先校验 ID、分页和码率参数，歌曲每页最多返回 15 首，并按生成的设备字符集清洗文字。
- `server/src/config.ts`：集中读取并校验运行配置；`LOCAL_RELAY_MODE=true` 用于电脑本地中转服务，`ALLOW_STALE_DEVICE_TIMESTAMPS=true` 仅允许用于本地中转或本地模拟测试。
- `server/portable/`：Windows x64 便携包启动文件；CMD 入口保持 ASCII/CRLF 并调用 Windows PowerShell 5.1，使用内置 Node.js 24 LTS；网易云接口使用自动选择的本机端口，局域网中转固定使用 18080，并在启动前强制结束该端口的现有监听进程。
- `server/scripts/build-portable-release.ps1`：下载并校验官方 Node.js 运行时，安装生产依赖，并为便携包固定安装兼容 Node.js 24 的 `better-sqlite3` 后生成可直接分发的 ZIP。

## 安全边界

- 公开固件不包含共享设备密钥。
- 每台设备本地生成自己的 `device_id` 和 `device_secret`。
- 首次注册需要用户输入服务器侧注册码。
- 服务器数据库只保存加密后的设备密钥和网易云 cookie。
- 便携包不预置数据库或注册码；每台电脑首次运行独立生成。网易云接口只监听 `127.0.0.1`，只注册设备实际使用的登录、歌单和播放接口。

## 固件基线

- `firmware/ai-passport/` 保留上游 FoloToy AI Passport 工程结构和 BSP 基础，但产品入口是独立网易云设备应用。
- 当前基线目标是 ESP32-C3、8 MB Flash、无 PSRAM。
- 官方 demo 菜单不再作为启动入口，也不再承载网易云功能。
- `firmware/components/netease_client/` 负责本项目服务 HTTP 请求、设备 HMAC 签名和紧凑 JSON 解析，不包含 UI、不保存网易云 cookie；服务端分页上限保证响应适配设备的 4096 字节缓冲。
- `firmware/ai-passport/main/wifi_provisioning_page.c` 负责 SoftAP、附近 Wi-Fi 扫描接口、NVS 保存、首次注册和连接状态展示；联网、注册和本地冒烟验证在后台任务执行。
- `firmware/ai-passport/main/wifi_provisioning_portal.c` 负责手机配网页、扫描结果去重排序和安全 JSON 输出，页面支持点选网络、密码显隐、重新扫描及隐藏网络手动输入。
- `firmware/ai-passport/main/netease_app.c` 负责设备上的网易云页面：二维码登录、等待、歌单、歌曲列表、播放 URL 获取和错误展示。
- `firmware/ai-passport/main/netease_app_logic.c` 负责按键状态机，方便在不依赖硬件和 LVGL 的情况下测试页面流转。
- 动态歌曲信息使用由 `tools/generate_device_fonts.py` 生成的 10/14/18/24px 字库，覆盖 3827 个中文和常用符号；服务端字符集由同一脚本生成，仓库检查会阻止两边不一致。
- `firmware/components/audio_stream/` 负责 HTTP 音频拉流、MP3 解码、立体声转单声道、按 MP3 源采样率输出 PCM、播放进度统计和输出音量设置；I2S 使用较深 DMA 缓冲吸收界面刷新和 I2C 控制造成的短暂停顿。音频硬件由播放任务按需打开并在结束后释放，UI 只调用 start、pause、resume、stop、进度读取和音量接口。
- 服务端 `LOCAL_RELAY_MODE=true` 时作为电脑上的本地中转服务运行，设备只需要能访问电脑局域网地址；歌曲 URL 会变成短期有效的本地中转地址，减少设备直接访问外部音频 CDN 的失败面。`MOCK_NETEASE=true` 时不访问真实网易云，返回本地固定扫码、歌单、歌曲和播放 URL 数据，用于烧录后的本地链路测试。
