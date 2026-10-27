<p align="right">
  <strong>简体中文</strong> · <a href="CHANGELOG.md">English</a>
</p>

# Changelog

## Unreleased

## 2.0 — 2026-10-28

- Folo Music 2.0 正式发布：开源协议直连、设备本地 Wi-Fi 键盘、网易云 App 扫码和会话恢复，方案 B 日夜界面、封面歌词、三键导航、缓存与简体英文字库。用户当前反馈暂无新 bug；不宣称全部长期压力和网络/曲库场景已覆盖。

- 增加干净的严格 8 MiB 完整出厂镜像、app-only 升级包、SHA-256 清单和打包回归。全镜像清空原设置；只有匹配 7 MiB 分区的 app-only 升级保留设置。整理正式文档入口、归档旧部署/开发状态，并补充开源作者致谢。

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
- 规定多应用发布的 Release 标题约定：tag 按 `v<版本>-<应用名>`（如 `v0.1.0-voice-keychain`）命名，让 Release 标题同时带版本与应用名；发布成功后核对标题，保证一眼扫 Release 列表就能区分是哪个应用。
- 新增发布后收尾流程：`issue-suggestions` skill 用于把用户反馈作为 issue 提交到上游项目；`experience-pr` skill 用于把可复用的开发经验作为文档 PR 提交；新增 `docs/experiences/` 目录保存单条经验文件；并配套 `after-release`、`file-issues` 与经验索引文档。
- 精简仓库根目录：将 GitHub 可识别的社区治理文档迁入 `.github/`，将变更记录迁入 `docs/`，同步全部引用，并在仓库检查中加入根目录文档白名单。
- 全仓库文档语言规范：所有维护中的 Markdown 默认 `.md` 文件使用英文，简体中文使用配对的 `.zh_CN.md`，双方提供语言切换；静态检查会阻止缺失配对、缺失切换链接或英文默认页混入中文正文。
- AI 开发流程一期：精简按任务加载的上下文入口，统一本地/CI 验证脚本，新增 PR 自动构建与模板，并提交依赖锁文件以提高构建可复现性。
- PR 审查修复：GitHub Actions 固定到完整 commit SHA，构建与发布 job 按最小权限拆分，同步 checkout 关闭凭证持久化；补充 Feature Request / Usage Question issue 表单；启用并修正私密安全报告兜底说明；清理 README 路径、CI 触发条件与历史分支描述漂移。
- 语言规范变更：commit 标题、PR 标题与 body 由"默认中文"改为**使用英文**（`docs/contribution/commit-and-pr.md` 更新）；中文写作规范（全角标点）适用范围剔除 PR/MR 描述（`doc-conventions.md` 更新）。
- CI 构建改造：`build-firmware.yml` 显式传入 `SDKCONFIG_DEFAULTS=sdkconfig.defaults` 再 `idf.py build`，由 defaults 启用自定义分区表（`CONFIG_PARTITION_TABLE_CUSTOM=y`，文件名为 `partitions.csv`）；`CONFIG_ESPTOOLPY_HEADER_FLASHSIZE_UPDATE` 改为 `n`，再用 `idf.py merge-bin -o build/FoloToy-Wangyiyun-full.bin` 合并可直刷完整固件；产物精简为仅 full.bin；`actions/cache` 升级到 v5 以消除 GitHub Actions Node.js 20 弃用警告；CI 文档同步更新。
- 合并上游 PR #6（wireless-low-power-demos）以解决 PR #4 冲突：引入无线/低功耗 demo（`main/demo_wifi.c`、`demo_ble.c`、`demo_radio.c`、`demo_low_power.c`）、`partitions.csv`（NVS/PHY/3 MB factory-app 分区）、`main/CMakeLists.txt`/`main.c`/`demo.h`/`sdkconfig.defaults` 更新；同步硬件指南的 Wi-Fi/BLE/低功耗章节；README 能力契约表补充 Wi-Fi/Bluetooth LE/Low power 三项（中英双语）。
- 提交规范补充：`docs/contribution/commit-and-pr.md` 明确 PR 标题与 commit 标题使用相同的 Conventional Commit 格式和英文祈使句，不用名词短语当标题。
- CI 与文档清理：`sync-main.yml` 移除 `test_mode` 残留模板注释；`docs/development/coding-conventions.md` 将「Redis TTL」条目泛化为「缓存组件」条目（当前固件无 TTL 约束需求，消除从模板带入的无关约定）。
- 补充通用规范（借鉴 Shinku）：`docs/contribution/doc-conventions.md` 新增中文全角标点规范（正文 `，`；`（`）`，代码/命令/路径保留英文原样）、凭证不入仓规范（token/密钥/私钥绝不入仓，提交前 git diff 扫描敏感前缀）、文件删除安全规范（删除走系统回收站，不用 rm -rf/git clean -fd）。
- 代码注释规范强化：`docs/development/coding-conventions.md` 补充完善注释要求——函数说明（用途/参数/返回值/副作用/线程上下文/内存所有权/初始化顺序）、变量说明（语义/取值范围/生命周期/同步要求）、逻辑注释（状态机/时序/寄存器/魔数依据），覆盖范围宁多勿少，中文注释保留英文技术术语。
- 文档去 AI 化：`docs/README.md` / `docs/README.zh_CN.md` 移除 AI 专属章节（Entry point、Source-of-truth、提需求格式、BSP 边界、Runtime invariants、验收交付格式、构建命令），README 只保留给人看的项目介绍、硬件能力契约、demo 案例与项目结构；构建命令章节删除（与 `docs/development/build-and-test.md` 重复）。
- 新增 `docs/development/agent-guide.md`：集中承载"AI 如何在本仓库工作"（上下文建立顺序、事实来源优先级、提需求格式、BSP 边界、运行时规则、交付格式），并链接 build-and-test 与硬件指南，不重复构建命令与验收矩阵。
- 同步更新索引：`AGENTS.md` 规则索引新增 agent-guide 条目；`docs/INDEX.md` 与 `docs/development/README.md` 新增 agent-guide 索引行。
- 文档补充：`docs/fork-guide.md` 说明「为什么根目录不放置 README」——根目录 README 预留给 fork 开发者自行放置（上游留空），fork 后可将自己的内容写入根目录 `README.md` 介绍 fork 后的项目；GitHub 显示优先级（根 README > docs/README.md）契合该预留意图。
- 分支合并：创建 `main-update` 分支（基于与上游一致的 main），将 `feature/repo-structure`、`ci/build-firmware`、`ci/sync-main` 三个分支合并进来，统一 docs 结构（CI 文档归入 `docs/development/`，workflow 文件随 ci 分支引入 `.github/workflows/`）；解决 development/software-design README 的 add/add 冲突。
- 合并后审查修复：`docs/INDEX.md` 补充 CI 文档索引；`docs/fork-guide.md` 修正 workflow 引用为 `.github/workflows/sync-main.yml`；`docs/README` 双语项目结构块补充 `.github/workflows/` 与 CI 文档说明。
- ci 分支 CI 文档路径调整：`ci/build-firmware` 的 `docs/software-design/CI-build-and-release.md` 与 `ci/sync-main` 的 `docs/software-design/CI-sync-main.md` 均移入各分支的 `docs/development/`（CI 属工程规范）；`docs/software-design/README.md` 保留为软件设计索引；feature 分支的 software-design 索引同步更新引用。
- fork 补充文档目录迁移：`assets/docs/` 移至 `docs/assets/`（文档素材归入 docs/ 更合理），新增 `docs/assets/.gitkeep` 空目录占位；同步更新 AGENTS.md / INDEX / doc-conventions / fork-guide 的路径引用。
- 文档结构调整：根目录不再放 README——上游英文 README 移入 `docs/README.md`、中文移入 `docs/README.zh_CN.md`（GitHub 从 docs/ 识别主 README）；原 `docs/README.md` 根总索引更名为 `docs/INDEX.md`；同步更新 AGENTS.md / CONTRIBUTING / SUPPORT / fork-guide / doc-conventions 的路径引用。
- 初始化项目文档：新增 `AGENTS.md`、`CLAUDE.md` 和 `CHANGELOG.md`。
- 仓库结构规整：上游英文 `README.md` 更名为 `README.en_US.md`，保留 `README.zh_CN.md`。
- 新增目录骨架：`docs/`（software-design / hardware-design）、`assets/`（fonts / images / music，各含 `README.md`）、`skills/`。
- 将上游硬件开发指南归位到 `docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md`。
- 文档规范：子目录 readme 统一为大写 `README.md`；补充 fork 用户约定（main 只动根 README）。
- 扩展 fork 用户约定：`main` 分支允许修改根目录 `README.md` 和 `assets/docs/`（README 不足以说明项目时存放补充文档与素材）。
- 新增 `assets/docs/` 目录约定：上游 main 只保留空目录 `.gitkeep`，内容文件仅存在于 fork；使用方法规范写入 AGENTS.md「给 fork 用户」约定。
- CI 文档迁移：`docs/software-design/CI.md` 从本分支移除，迁至 `ci/build-firmware` 分支并改名为 `docs/software-design/CI-build-and-release.md`。
- 补充 `main` 分支策略说明：解释 `main` 保持干净的两大原因（与上游同步无冲突 + 多小项目按分支整理）；例外——执意 main 开发需停用 CI 自动同步；提醒 fork 用户默认 action 关闭需手动启用（此条为整个 CI 的通用要求，统一写入 AGENTS.md）。
- 文档拆分：将 `AGENTS.md` 按主题拆为公共文档——新增 `docs/contribution/`（doc-conventions.md、commit-and-pr.md）与 `docs/development/`（build-and-test.md、coding-conventions.md），新增 `docs/fork-guide.md`；`AGENTS.md` 精简为简介 + 项目概述 + 必读文档索引。
- 同步更新索引：`docs/software-design/README.md`、`README.en_US.md` / `README.zh_CN.md` 的 `docs/` 目录说明。
- 参考 cindy 仓库文档组织完善索引：新增 `docs/README.md` 根总索引；AGENTS.md 规则索引按触发场景改写（附触发条件）；`docs/contribution/` 与 `docs/development/` 的 README 补充收录标准。
- 引入社区治理文档（参照 cindy 改写，放仓库根目录）：新增 `CONTRIBUTING.md` / `.zh_CN.md`（贡献指南，针对 ESP-IDF/AI agent/fork 场景改写）、`CODE_OF_CONDUCT.md` / `.zh_CN.md`（贡献者公约）、`SECURITY.md` / `.zh_CN.md`（安全报告流程）、`SUPPORT.md` / `.zh_CN.md`（支持渠道）；AGENTS.md 与 docs/README.md 同步引用。
