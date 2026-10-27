# 网易云官方接口记录（历史探索）

> 本文记录官方开放平台直连实验。该路线当前被官方设备参数校验阻塞，不是项目接下来的实现路线。当前目标已转为在 ESP32-C3 上实现非官方客户端协议直连；实施计划见 `docs/netease-direct-client-plan.md`。本文保留为接口研究与历史决策依据，后续不得把此实验版描述为已验证可用。

## 来源

- 官方文档入口：`https://developer.music.163.com/st/developer/document`
- 本次用户给出的文档：`docId=2bb12a93e71a4be0842243b930c2f33c`，标题为“获取登录二维码”。
- 官方文档搜索接口：`https://developer.music.163.com/api/openplatform/apidoc/search`

## 已确认的官方文档条目

| 功能 | 官方文档标题 | docId |
| --- | --- | --- |
| 获取登录二维码 | 获取登录二维码 | `2bb12a93e71a4be0842243b930c2f33c` |
| 轮询二维码状态 | 轮询二维码状态 | `4d0301c68c5d45d8811d7787ee37d2f6` |
| 获取用户收藏歌单 | 获取用户收藏的歌单列表 | `1b3d86a47f2e45c7bd631bcd26052382` |
| 获取歌单歌曲 | 获取歌单里的歌曲列表v4 | `781b8d60178f428695e8ff3b464a3736` |
| 获取歌曲播放地址 | 获取歌曲播放url | `3d2c9f695ff24f4ea37611614b7f7856` |
| 批量获取歌曲播放地址 | 批量获取歌曲播放url | `70ada04216d64b0d88e80740dee23a77` |
| 全曲试听 | 全曲试听升级2.0 | `9e92a977bbb549948351b4dc3b0b0524` |

## 已按截图确认的业务接口

| 功能 | 路径 | bizContent | accessToken |
| --- | --- | --- | --- |
| 匿名登录 | `/openapi/music/basic/oauth2/login/anonymous` | `clientId` | 不需要 |
| 获取登录二维码 | `/openapi/music/basic/user/oauth2/qrcodekey/get/v2` | `type=2`、`expiredKey="300"` | 需要匿名 token |
| 轮询二维码状态 | `/openapi/music/basic/oauth2/device/login/qrcode/get` | `key`、`clientId` | 需要匿名 token |
| 刷新 AccessToken | `/openapi/music/basic/user/oauth2/token/refresh/v2` | `clientId`、`clientSecret`、`refreshToken` | 不需要 |
| 获取用户创建歌单 | `/openapi/music/basic/playlist/created/get/v2` | `limit`、`offset` | 需要用户 token |
| 获取用户收藏歌单 | `/openapi/music/basic/playlist/subed/get/v2` | `limit`、`offset`、`originalCoverFlag=false` | 需要用户 token |
| 获取歌单歌曲 | `/openapi/music/basic/playlist/song/list/get/v5` | `playlistId`、`limit`、`offset`、`qualityFlag=true` | 需要用户 token |
| 获取播放地址 | `/openapi/music/basic/song/playurl/get/v2` | `songId`、`bitrate` | 需要用户 token |

关键返回字段：

- 匿名登录返回 `data.accessToken`、`data.refreshToken`、`data.expireTime`。
- 登录二维码返回 `data.qrCodeUrl`、`data.uniKey`。
- 轮询二维码返回 `data.status`、`data.msg`，状态 `803` 时返回 `data.accessToken.accessToken`、`data.accessToken.refreshToken`、`data.accessToken.expireTime`。
- 刷新令牌返回 `data.accessToken`、`data.refreshToken`、`data.expiresTime`。
- 歌单列表返回 `data.records[]`，使用 `id`、`name`、`creatorNickName`、`trackCount`。
- 歌曲列表 v5 返回 `data.tracks[]`，使用 `id`、`name`、`duration`、`artists[0].name`。
- 播放地址返回 `data.url`、`data.br`、`data.level`、`data.freeTrial`；`subCode=10003/10004` 等限制类结果要作为明确失败处理。

## 当前限制

- 已能通过用户登录后的官方页面和飞书《API产品接入指南_1》截图确认主要接口字段。
- 飞书接入指南的 FAQ 明确说明：个人开发者暂不支持直接接入开放平台 API 的设备参数配置；个人场景仅可使用 `ncm-cli`。厂商入驻或正式设备接入需要联系云音乐商务。
- `channel`、`deviceType`、`os`、`brand` 不是通用固定值，而是由网易云分配或需线下联系确认。
- 官方直连版在 COM14 真机上已能启动、联网、校时并请求匿名登录接口，但当前返回 `code=400`，提示公共参数校验失败，需要检查 `os/channel/brand` 是否符合约定。
- 本地对照联调用正确/错误签名都会得到同一个 `code=400` 文案，因此该错误可能同时覆盖“签名不匹配”和“设备公共参数未授权”两类情况。下一步若继续官方直连，需要拿到官方 demo 中的签名代码或官方 RSA 签名生成规则全文，再结合网易云分配的设备参数验证。

## 项目方案

- 当前中转版保持不变：设备只访问本项目服务，网易云登录态保存在服务端。
- 另建 `netease-official-firmware/`：用于用户自己一台设备的开发验证，固件直接访问网易云官方接口并在设备 NVS 保存官方令牌。
- 两套固件目录、配置和构建产物相互独立；官方直连版不得修改或引用当前中转版源码。
- 官方直连版的完整设计见 `docs/superpowers/specs/2026-08-29-netease-official-standalone-firmware-design.md`。
- 根据飞书 FAQ，个人开发者更现实的路线是继续使用当前中转版，或在电脑侧使用官方允许的 `ncm-cli` 再给设备做本地中转；纯 ESP32 固件直连官方开放 API 需要网易云商务侧开放厂商参数。

## 官方直连版当前落地状态

- 已新增独立目录：`netease-official-firmware/`。
- 已固化官方域名：`https://openapi.music.163.com`。
- 已固化开发验证设备取值：`channel=iotapitest`、`deviceType=openapi`、`os=openapi`、`brand=iotapitest`。
- 已新增本机忽略的密钥文件模板：`netease-official-firmware/config/netease_secrets.h`。
- 已实现并测试：公共参数排序、URL 编码、设备 JSON、官方响应 envelope 解析、令牌有效期判断。
- 已实现并通过 ESP-IDF 构建：Wi-Fi-only 配网页、官方令牌 NVS 存储、官方客户端主应用入口、RSA-SHA256/Base64 签名函数、HTTPS 传输函数、官方业务 API 请求和基础解析。
- 已生成独立开发固件：`netease-official-firmware/firmware/ai-passport/build/FoloToy-NetEase-Official.bin`。
- 已生成独立合并包：`netease-official-firmware/firmware/ai-passport/build/merged-binary.bin`。
- 已烧录 COM14 并监控：登录页白屏重启已修复，未再出现栈崩溃或反复重启。
- 尚未完成：官方匿名登录、真实设备扫码、歌单和播放联调。当前阻塞点是网易云官方后台拒绝公共参数/签名校验；需要官方分配的 `channel`、`deviceType`、`os`、`brand` 和签名规则确认。
