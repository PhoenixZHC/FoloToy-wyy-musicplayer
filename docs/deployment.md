# 部署说明

当前主路径已改为电脑本地中转服务；公网部署只作为可选方案保留。

## 完整交付包

发给普通 Windows 用户时，直接使用项目根目录的：

```text
release/FoloToy-Wangyiyun-Complete-v1.2.2.zip
```

完整包内包含从地址 `0x0` 烧录的严格 8 MB 固件、Windows x64 中转服务 ZIP、中文使用说明和 SHA-256 校验文件。对方先解压完整包，再解压 `server/` 中的中转服务 ZIP，双击 `启动中转服务.cmd` 即可运行。电脑不需要安装 Node.js、npm 或 Docker。

完整固件会覆盖整块 8 MB Flash，包括设备上已有的 Wi-Fi、中转注册和网易云登录数据。烧录命令中的串口号必须改成实际设备串口：

```powershell
python -m esptool --chip esp32c3 --port COM14 --baud 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m 0x0 ".\firmware\FoloToy-Wangyiyun-full-8MB.bin"
```

## 最低配置

- 服务器：1 vCPU，1 GB RAM。
- 必需服务：Node app、网易云官方开放平台出口或临时兼容适配服务、HTTPS 反向代理。
- 生产环境必须走 HTTPS，设备注册和后续接口都不要直接暴露明文 HTTP。

## 环境变量

```text
DATABASE_PATH=/data/app.db
COOKIE_ENCRYPTION_KEY=32-byte-random-string
DEVICE_REGISTRATION_TOKEN=long-random-registration-code
NETEASE_API_BASE_URL=https://your-netease-api-gateway.example
```

`COOKIE_ENCRYPTION_KEY` 必须是 32 字节，用来加密设备密钥和网易云 cookie。
`DEVICE_REGISTRATION_TOKEN` 是用户给新设备绑定时输入的注册码，不要写进公开固件。
`NETEASE_API_BASE_URL` 非模拟模式必须显式设置；正式生产优先使用网易云官方开放平台接口，不要依赖本地默认值。

本地中转模式使用 `LOCAL_RELAY_MODE=true`，可由 `npm run start:local-relay` 自动设置。

## 启动

```powershell
Set-Location -LiteralPath "E:\code\folotoy-wangyiyun\server"
Copy-Item -LiteralPath ".env.example" -Destination ".env"
docker compose up -d
```

启动后用反向代理把公网 HTTPS 域名转到 Node app 的 `8080` 端口。

## 冒烟检查

```powershell
curl.exe -f "https://your-domain.example/health"
```

期望响应：

```json
{ "ok": true }
```

## 备份

备份 SQLite 前先停止服务，再复制数据库文件：

```powershell
docker compose down
```

然后备份挂载卷里的 `app.db`。

## 音频流量

本地中转模式会由电脑转发音频，设备播放期间电脑和中转窗口必须保持在线。公网模式可以直接返回音乐源地址，是否代理音频由部署方式决定。
