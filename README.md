# FoloToy 网易云音乐播放器

把 FoloToy AI Passport 变成一台独立的网易云音乐播放器。设备支持首次配网、扫码登录、浏览个人歌单和歌曲，以及播放、暂停、切歌、音量调节和进度显示。

本仓库包含可用的中转版固件、中转服务源码、Windows 便携发布包，以及单独保留的官方开放平台直连实验版。

## 最快使用方式

不需要自行构建时，下载并完整解压：

[`release/FoloToy-Wangyiyun-Complete-v1.2.2.zip`](release/FoloToy-Wangyiyun-Complete-v1.2.2.zip)

完整包内包含：

- 可从地址 `0x0` 烧录的完整固件镜像；
- 无需另外安装 Node.js 的 Windows 中转服务；
- 中文使用说明和 SHA-256 校验文件。

解压后按照包内的 `使用说明.md` 操作。中转服务和设备需要连接同一个局域网。

## 项目目录

```text
firmware/ai-passport/          当前可用的中转版固件
firmware/components/           音乐服务客户端和播放组件
server/                        本地中转服务源码
release/                       可直接下载使用的发布包
docs/                          部署、接口和开发记录
netease-official-firmware/     官方开放平台直连实验版
一键启动中转服务.cmd           从源码启动本地中转服务
```

`netease-official-firmware/` 目前仅作为实验与接入验证记录保留。个人开发者场景缺少官方分配的设备参数，尚不能完成实际登录和播放；日常使用请选择 `firmware/` 中的中转版。

## 从源码构建固件

### 环境

- Windows PowerShell
- ESP-IDF v5.5.4
- Git

先打开已经激活 ESP-IDF 的 PowerShell，然后运行：

```powershell
Set-Location -LiteralPath '.\firmware\ai-passport'
idf.py --version
idf.py build
```

构建产物：

```text
firmware/ai-passport/build/FoloToy-Wangyiyun.bin
```

连接设备后烧录，把 `COM14` 换成设备实际端口：

```powershell
idf.py -p COM14 flash
```

完整构建、合并镜像和仓库检查：

```powershell
& 'C:\Program Files\Git\bin\bash.exe' '.\tools\validate.sh'
```

## 从源码启动中转服务

### 环境

- Windows PowerShell 5.1 或更高版本
- Node.js 20 或更高版本
- npm

安装、测试并构建：

```powershell
Set-Location -LiteralPath '.\server'
npm ci
npm test
npm run build
```

回到仓库根目录后，双击 `一键启动中转服务.cmd`。启动窗口会显示设备配网需要填写的 `Relay URL` 和 `Registration Code`。保持窗口打开，设备才能继续登录和播放。

也可以在 `server/` 目录手动运行：

```powershell
npm run start:local-relay
```

首次使用时：

1. 手机连接设备显示的 `AI-Passport-*` 热点；
2. 浏览器打开 `http://192.168.4.1`；
3. 选择家里的 2.4 GHz Wi-Fi；
4. 填写中转窗口显示的地址和注册码；
5. 提交后按设备提示扫码登录。

## 重新生成 Windows 发布包

```powershell
Set-Location -LiteralPath '.\server'
& '.\scripts\build-portable-release.ps1' -PackageName 'FoloToy-Wangyiyun-Relay-Windows-x64-v1.2.2'
```

发布包会生成到仓库根目录的 `release/`。不要把运行后生成的数据库或账号会话加入仓库。

## 安全说明

- 固件不会内置共享设备密钥，每台设备首次使用时生成自己的身份信息。
- 中转服务会加密保存设备密钥和网易云登录会话。
- 仓库不会提交数据库、真实注册码、登录 Cookie、私钥或开放平台密钥。
- 公开发布前应检查 `.env`、`server/data/` 和 `netease-official-firmware/config/` 中的本地文件。

## 更多文档

- [项目架构](ARCHITECTURE.md)
- [部署说明](docs/deployment.md)
- [官方接口记录](docs/netease-official-api.md)
- [配网开发经验](docs/AI%20Passport%20SoftAP%20配网开发经验.md)
- [网络音频开发经验](docs/AI%20Passport%20网络音频开发经验.md)

## 上游与许可

固件基于 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport) 开发。上游许可文件保留在 [`firmware/ai-passport/LICENSE`](firmware/ai-passport/LICENSE)。本仓库中的第三方组件分别遵循各自许可证。
