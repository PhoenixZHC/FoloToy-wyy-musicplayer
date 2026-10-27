# 加载问题：开源参考与自动化验收

正式发布状态（2026-10-28）：用户实测暂未发现新的 bug，并确认当前版本作为 Folo Music 2.0 发布；应用 3,883,184 字节。当前功能、容量与验收见[2.0 发布说明](releases/v2.0.md)。以下为历次故障、候选和仿真实验记录，保留失败证据；不据当前用户反馈改写历史失败，也不宣称完整生产仿真或系统性长期压力测试通过。

## 本次真机烧录与启动

- 重新构建生产直连版本（原5.5.4环境），未烧录省略省电API的仿真诊断版本；应用3,885,360字节，仅写入0x10000，写入哈希校验通过。
- 读取并比对设备分区表与已验证7 MiB布局一致；写入前后0x8000–0xffff逐字节一致，包含24,576字节NVS和PHY。未写分区表、bootloader或NVS，未整片擦除。
- 主机测试10项、静态检查及合并镜像逐段校验通过。重启后约5.7秒Wi-Fi ready；已有登录态账号请求成功（约1.07秒），20个歌单加载成功（约0.99秒）；当前启动日志未发现WDT或panic。
- 原始串口日志仅保存在本机/tmp/folo-device-test-tncd0i8j/serial.log，监控单次15分钟，不代表永久监听。用户随后复现播放中列表加载失败；该版未通过，后续实验见下节。
- 以上仅证明烧录、设置保留与启动联网，不证明TLS内存错误或播放卡顿已修复。下文原烧录前缺项仍作为完整验收要求，当前按用户新授权先做真机验证。

## 后续真机复现与内存预算调整

- 用户确认首版仍无法在播放中加载列表。首版预取offset=8约321毫秒且缓存命中，但未缓存页在空闲约21 KiB、最大连续块10 KiB时返回HTTP_WRITE_DATA；重建TLS又因4437字节申请失败而失败。
- 短连接实验在播放前释放连接，真机回收24,104字节；播放后空闲约44–47 KiB、最大连续块32 KiB，仍在新握手时申请4437/5896字节失败，最低空闲244字节。该策略已撤回，不作为修复结果。
- 后续候选使用ESP-IDF官方HTTP_TLS_DYN_BUF_RX_STATIC，在WEAPI握手后预留完整16 KiB接收记录；TLS发送记录1 KiB由SDK分片，HTTP请求头仍4 KiB，证书仅在握手验证后释放。只读空响应恢复覆盖FETCH_HEADER和WRITE_DATA，仍不重放EAPI、登录、部分响应或Cookie变化的请求。相关回归先失败后通过；提取真实SDK发送函数的三项分片/短写/错误传播检查通过。
- 固定RX版应用3,885,680字节，SHA-256 e07b18daf2fb00751b181d449f0a978302aec71b8770e20c4853b76b1d3a06ed，仅写0x10000，分区表/NVS/PHY前后一致。本机日志/tmp/folo-device-test-tlemkxzn/serial.log显示联网和列表成功，但三次音频启动均在I2S DMA分配时报ESP_ERR_NO_MEM，不能据此验收并发。
- 已核对真实BSP与SDK：BSP原8个512帧双声道16位DMA需要16,384字节；SDK默认6个240帧需要5760字节，启动峰值减少10,624字节。当前恢复该默认值，音频任务增加初始化前后free/largest/minimum日志；保留8 KiB压缩输入与证书校验。播放缓冲容忍时间会缩短，需同时验证真实播放、未缓存页请求及断音，不能只看能否启动。

- DMA修正版生产应用3,885,952字节，SHA-256 412356cc3ecf9374a3b13e4ff3a01043d6e6b1ea0d88537c6125ced45e030e89，已仅写0x10000；写入哈希及分区表/NVS/PHY前后比对通过，NVS 24,576字节一致。直连/中转构建与合并镜像逐段验证、静态检查和10个主机程序通过。启动联网、账号及20歌单请求成功；播放与并发验收待收。本机日志/tmp/folo-device-test-rx5lte0x/serial.log，单次采样15分钟。

- DMA修正版实测已解码44.1/48 kHz音频，播放中offset=16和24两批未缓存歌曲加载成功（API约1994/518毫秒，完整工作约2385/915毫秒），随后切歌和自然EOF进入下一首成功。仍记录最低空闲984字节、网络读取566毫秒和浏览期间帧吞吐下降，不能宣称无卡顿或长期稳定；尚未覆盖切换另一个歌单。
- 根据紧张堆余量继续降低请求准备峰值：在RSA前释放第一层AES结果，URL编码直接写最终请求体。真实八首请求的3072字节受限堆测试先失败后通过，编码器自有申请峰值3187→1626字节；已有固定密钥协议向量不变。该夹具不计共享mbedTLS内部申请，不是完整TLS堆模型；完整静态门禁实际运行12个主机程序，已修正旧汇总计数。新编码器版需重新烧录验收，前一候选的硬件结果不自动继承。

- 最新编码器候选已烧录：应用3,885,968字节，SHA-256 ea06019bfee5a431077b7c292d5ef6df0fb1487192155d504913606367064462；仅写0x10000，写入哈希以及0x8000–0xffff逐字节比对通过，NVS 24,576字节一致。直连/中转构建与合并镜像逐段校验、12个主机程序和静态检查通过。串口/tmp/folo-device-test-mhl7170s/serial.log，采样15分钟；新版本并发、换另一歌单及听感仍待验收。

## 已确认的问题

- 真机连接复用版在播放中加载歌曲页，收到 HTTP 200 后只读到 995 / 17214 字节，动态 TLS 申请 12947 字节失败，报 `ESP_ERR_HTTP_INCOMPLETE_DATA`。这发生在响应读取阶段，不能继续只按握手开销解释。
- 同次串口采样在自动进入下一页时，空闲堆 87328 字节、最大连续块 49152 字节，仍报 `ESP_ERR_HTTP_FETCH_HEADER`，无响应正文。这与前一个内存问题不同；空闲连接失效是待真机验证的解释。
- 音频批量补充后，每约五秒读网络 12–13 次；常见 44.1 kHz 日志有 192 个解码帧。这个计数不能证明没有短暂停顿。
- 歌单编号接口 `n=0` 实测仍返回约 385 KB，约 4.5 秒，不是已验证的冷加载提速方案。

原始串口仅保留在本机 `/tmp/folo-oct28-reuse.log`，不提交账号及原始日志。

## 本次核对的开源实现

通过 GitHub API 获取固定提交和源文件；没有直接替换生产依赖。

| 项目及固定版本 | 已核对内容 | 本项目适用范围 |
| --- | --- | --- |
| [leo-radio](https://github.com/leo0183/leo-radio/tree/e28b8adafb2ca95e386e8b6e7db0db042cf09523) | `main/radio_player.cc`、`components/esp-wifi-connect/wifi_station.cc`、README | 同板 C3；事件驱动联网、单播放任务、1 KiB 输入及 8 KiB 输出可参考。它播放电台，不能证明网易云 HTTPS 和列表并发已解决。解码器也与当前 Helix 不同。MIT 项目，组件各有许可。 |
| [api-enhanced](https://github.com/neteasecloudmusicapienhanced/api-enhanced/tree/984cc021b8598cf94c22134f32a8426887fb2de7) | 已有协议记录中的 EAPI/WEAPI、歌词、歌曲详情 | 保持开源协议直连的参考；Node 服务不能直接作为设备上的 C3 组件。 |
| [ESP8266Audio](https://github.com/earlephilhower/ESP8266Audio/tree/e1f2a1a34aa460f5cca7a60b69d9a350fd099938) | `AudioFileSourceBuffer.cpp`、`AudioGeneratorMP3.cpp`、README | 有输入环形缓冲、机会性填充及欠载反馈，可以借鉴算法。Arduino 接口与 GPL/各解码器许可需另行审查，未整库引入，也未验证本板兼容。 |
| [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S/tree/bd873ce90e1c9580df1660a845b6e7363a520dd5) | README | 当前说明要求 PSRAM，明确不支持 ESP32-C3；不作为替换方案。 |
| [PassportSim](https://github.com/BlackHole1/passportsim/tree/71288fa9ceda84ac406c23996ff880c2cf9ec795) | 本机 1.0.3 WASM、ABI 3；Wi-Fi 绑定规则与虚拟 LAN 实现 | 可执行真实 RISC-V 固件及物理按键输入。Wi-Fi profile 固定 IDF 5.5.3；原版不支持当前使用的 `esp_wifi_set_ps`。模拟器堆模型含估计，无法替代真机峰值验收。 |

没有找到经过本项目 C3、无 PSRAM、网易云扫码及播放中翻页验证的一键替代库。现有开源项目提供局部实现和方法，不能以同类项目能播放就推断本问题已经解决。

## 已执行的电脑版仿真

新增 `firmware/ai-passport/tools/run_passportsim.cjs`，直接调用发布版 WASM 的正式 ABI；使用合并镜像及 ELF，不读取设备 NVS、闪存备份或真实账号。测试输入仅为虚拟 AP 及合成密码。

1. **生产 IDF 5.5.4 构建**：启动、欢迎页通过；按 OK 扫网触发 `E_TRIPWIRE`，`disabled_feature:esp_wifi_init (wifi disabled)`。receipt 明确记录 `unsupported image`。
2. **独立 IDF 5.5.3 / 配套 20251107 工具链构建**：Wi-Fi profile 确认 `bound`；按 OK 后仍触发 `E_TRIPWIRE`，`blob_internal:esp_wifi_set_ps`。未伪造版本或关闭 profile 校验。
3. **受限诊断构建**：仅在 `/tmp/folo-sim-project/` 源码副本省略 `esp_wifi_set_ps`；生产源码不变。自动点击 OK、扫描虚拟 WPA2 网络、选择网络、逐键输入八字符、长按 OK 打开菜单、提交连接、检查成功页面，**9 项检查通过，0 失败**，无 tripwire。省电行为、真实 TLS 和播放不在该结果范围内。
4. **热点消失与换网**：同一受限构建中，输入密码后移除虚拟热点，等待连接超时，检查失败页面，再恢复热点并按 ↓ 重新扫描；**10 项检查通过，0 失败**。这些检查与正常连接场景存在重叠，不相加为独立用例数。
5. **错误密码**：在提交连接时模拟器返回 `E_HLE`，未到固件失败页面。核对 `crates/pemu-radio/src/wifi/driver.rs` 的 `admits` / `unexercised`，当前模型明确拒绝错误密钥，不能将其计为固件通过或固件故障。该场景保持未验收。

工具链归档 SHA-256 按 ESP-IDF 5.5.3 的 `tools/tools.json` 校验：`1d3a1b6a064686d9b77c4db7731f82e26c072e312e27969c45fe96410ecb2671`。SDK 与工具链安装在 `/tmp/folo-idf553`、`/tmp/folo-toolchain553`；不替换原 5.5.4 环境，源仓库锁文件保持原版本。

本机报告：

- `/tmp/folo-passportsim-current.json`：当前生产候选构建，联网前 tripwire。
- `/tmp/folo-passportsim-553.json`：未修改业务源码的匹配版本，省电 API tripwire。
- `/tmp/folo-passportsim-553-keyboard.json`：受限诊断，9 项通过。`releaseAcceptance=false`，不是发布验收。
- `/tmp/folo-passportsim-553-absent.json`：受限热点消失诊断，10 项通过，发布验收仍未通过。
- `/tmp/folo-passportsim-553-wrong-password.json`：模拟器 `E_HLE`，错误密码场景未验收。

执行方式，路径按本机安装位置替换：

```sh
node firmware/ai-passport/tools/run_passportsim.cjs /path/pemu_wasm.wasm /path/full.bin /path/app.elf /tmp/report.json baseline
```

受限诊断增加参数 `diagnostic-wifi`，必须使用上述单独构建；不得把测试副本当作待烧录固件。生产验收不满足时脚本以非零状态退出，报告保留明确错误。
热点消失场景使用 `diagnostic-wifi-absent`，错误密码兼容性检查使用 `diagnostic-wifi-wrong-password`。脚本记录 WASM、镜像与 ELF 的 SHA-256，并在任意命令返回失败、tripwire、panic 或 watchdog 时终止，不允许后续成功页面覆盖中途失败。

## 本次候选代码及主机回归

- 单 worker 复用同源 WEAPI 连接；EAPI 前释放连接，错误时销毁，不保留栈上的请求指针。获取音频 URL 提前到封面歌词之前，URL 失败不再等待媒体。
- 对明确列举的只读 WEAPI 查询，复用连接遇到 `ESP_ERR_HTTP_FETCH_HEADER` 且未收到正文、Cookie 未变化时，销毁并重建一次。EAPI/登录不重放，部分响应不重放，第二次失败保留错误。
- 自动测试先确认旧实现出现断言失败，再确认修正后通过；覆盖重建一次、持续失败、部分响应不重放、EAPI 不重放。
- 完整静态门禁和 10 个主机可执行程序通过。直连及中转构建、合并镜像逐段校验通过。**主机 stub 不模拟真实 TLS 堆或网络，因此不能视为内存问题验收**。

## 烧录前尚缺的验收

- 原版模拟器实现或明确支持 `esp_wifi_set_ps` 后，生产等价镜像通过启动、配网和按键场景。
- 虚拟 LAN 默认只提供本地 DNS/HTTP，不直接访问网易云公网。播放验收需额外的隔离 TLS 服务、可回放响应和音频夹具，并保留证书校验；不得修改生产 API 域名或使用真实账号备份制造通过。
- 在受限堆、完整 TLS 记录、音频解码同时运行下，自动覆盖歌曲第 8/16/24 页、预取、返回歌单、切歌、暂停、连接失效和断网恢复；记录 API 耗时、最大连续块和 PCM 欠载。
- 所有失败必须可见，不能靠停止音频或返回空列表冒充无感加载。仿真通过后才烧录生产候选，应用只写 `0x10000`，继续保留 NVS 并逐字节比对。

上述播放验收尚未完成，当前不宣称加载问题已修复。
