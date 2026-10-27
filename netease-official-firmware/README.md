> 历史实验目录，不是当前正式方案。Folo Music 2.0 使用开源协议设备直连，见[产品入口](../README.md)；不再推进官方设备参数方案。

# FoloToy NetEase Official Firmware

This directory contains a separate ESP32-C3 firmware build that talks to the
NetEase Cloud Music official Open Platform directly.

It is intentionally separate from the existing relay-based firmware:

- Existing relay firmware source: `E:\code\folotoy-wangyiyun\firmware`
- Existing relay server source: `E:\code\folotoy-wangyiyun\server`
- Official direct firmware source: `E:\code\folotoy-wangyiyun\netease-official-firmware`

Flashing either firmware replaces the program on the device. It does not modify
the other firmware source directory.

## Build the existing relay firmware

```powershell
$env:IDF_TOOLS_PATH = "C:\Users\User\.espressif"
$env:Path = "C:\Users\User\.espressif\python_env\idf5.5_py3.13_env\Scripts;$env:Path"
. "C:\esp\v5.5.4\esp-idf\export.ps1"
Set-Location -LiteralPath "E:\code\folotoy-wangyiyun\firmware\ai-passport"
idf.py build
```

## Build this official direct firmware

```powershell
$env:IDF_TOOLS_PATH = "C:\Users\User\.espressif"
$env:Path = "C:\Users\User\.espressif\python_env\idf5.5_py3.13_env\Scripts;$env:Path"
. "C:\esp\v5.5.4\esp-idf\export.ps1"
Set-Location -LiteralPath "E:\code\folotoy-wangyiyun\netease-official-firmware\firmware\ai-passport"
idf.py build
```

## Local credentials

Official Open Platform credentials are kept in:

```text
E:\code\folotoy-wangyiyun\netease-official-firmware\config\netease_secrets.h
```

That file is ignored by `.gitignore`. Do not paste real AppSecret, private key,
access token, or refresh token into tracked files or chat logs.

For a long PrivateKey, paste the complete key into an ignored local PEM file,
then generate the C header string automatically:

```powershell
Set-Location -LiteralPath "E:\code\folotoy-wangyiyun\netease-official-firmware"
.\tools\update_netease_private_key.ps1 -PrivateKeyPath ".\config\private_key.pem"
```

The temporary development device values currently built into the firmware are:

```text
channel=iotapitest
deviceType=openapi
os=openapi
brand=iotapitest
```

The official documentation says these values are assigned by NetEase Cloud Music
and must match the app's backend agreement. The current app still receives
`code=400` from anonymous login with the message that public parameters should
check `os/channel/brand`.

The Feishu integration guide FAQ says personal developers currently cannot use
the full Open Platform API device parameter flow directly. Personal scenarios
are limited to `ncm-cli`; vendor/device integrations need NetEase business-side
configuration.

## Current status

Implemented:

- Separate source tree.
- Local ignored credential template.
- Official client public types and endpoint constants.
- Parameter ordering, URL encoding, device JSON construction.
- Official response envelope parsing.
- Token lifetime decision logic.
- Wi-Fi-only provisioning page.
- Official OAuth token NVS storage.
- Main app entry migrated to the official client shape.
- RSA-SHA256/Base64 signing function for ESP-IDF.
- Bounded HTTPS transport for ESP-IDF.
- Official business API requests/parsers for anonymous login, QR login, token refresh, created/subscribed playlists, playlist songs v5, and play URL.
- PowerShell host test runner.
- ESP-IDF build output:
  `E:\code\folotoy-wangyiyun\netease-official-firmware\firmware\ai-passport\build\FoloToy-NetEase-Official.bin`
- Merged flash image:
  `E:\code\folotoy-wangyiyun\netease-official-firmware\firmware\ai-passport\build\merged-binary.bin`
- Flashed to COM14 and monitored.
- Login-page white-screen reboot fixed: large request buffers now live on the
  heap, the app worker task has a larger stack, and HTTP response bodies are
  captured through the ESP-IDF HTTP event callback.
- Device JSON now follows the official IOT public-parameter shape:
  `channel`, `deviceId`, `deviceType`, `appVer`, `os`, `osVer`, `brand`,
  `model`, `clientIp`, and `netStatus`.
- `appSecret` is included as a required public parameter for the newer official
  IOT parameter set.

Not complete yet:

- RSA signing self-test with a fixed fixture.
- Successful official anonymous login, QR login, playlist loading, and playback.
- Confirmation of the app-assigned `channel`, `deviceType`, `os`, and `brand`
  values, plus the exact official RSA signing rule page.
- If this remains a personal-developer app, continue with the existing relay
  firmware or an `ncm-cli` based local relay instead of pure ESP32 direct
  official API access.
