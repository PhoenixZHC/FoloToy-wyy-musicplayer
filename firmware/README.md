# Firmware

`firmware/ai-passport/` is the upstream FoloToy AI Passport firmware baseline.

Build on this Windows machine:

```powershell
Set-Location -LiteralPath "E:\code\folotoy-wangyiyun\firmware\ai-passport"
$env:IDF_TOOLS_PATH = "C:\Users\User\.espressif"
$env:Path = "C:\Users\User\.espressif\python_env\idf5.5_py3.13_env\Scripts;$env:Path"
. "C:\esp\v5.5.4\esp-idf\export.ps1"
idf.py --version
idf.py build
```

Verified baseline:

- ESP-IDF v5.5.4
- target: ESP32-C3
- flash size: 8 MB
- no PSRAM
