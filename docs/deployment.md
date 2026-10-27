# Folo Music 2.0 部署与构建

当前正式方案为设备开源协议直连，无需 Node.js、Docker、中转服务或官方平台设备参数。[旧中转部署](archive/relay-deployment-v1.md)仅供历史兼容。

## 环境

FoloToy AI Passport：ESP32-C3、8 MiB Flash、无 PSRAM、240×320 屏幕、↑/OK/↓三键。网络需为可扫描的 2.4 GHz 个人或开放网络。设备直接访问 music.163.com、interface3.music.163.com 和音频/封面 CDN。手机仅用于网易云 App 扫码授权，不参与配网。

## 完整安装：会清空原设置

FoloMusic-v2.0-full-8MB.bin 严格为 8,388,608 字节，含 bootloader、7 MiB factory 分区表和应用，其他区域以 0xFF 填充。**从 0x0 烧录会清空原 Wi-Fi、网易云登录态和显示设置**。当前设备已可用时，不需要为了领取文件而重烧。

先按 SHA256SUMS.txt 校验；Windows 使用 Get-FileHash，Linux 使用 sha256sum。把 COM14 换为实际串口：

    python -m esptool --chip esp32c3 --port COM14 --baud 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m 0x0 FoloMusic-v2.0-full-8MB.bin

Linux 可使用 /dev/ttyACM0，确保当前用户具有读写权限。无需另外先执行 erase_flash。首次启动在设备选网、输入密码，再用网易云 App 扫码。

## 保留设置升级

仅适用于已有相同布局的设备：factory 0x10000/0x700000、NVS 0x9000/0x6000、PHY 0xf000/0x1000。先核对分区表，私有备份 NVS；旧 3 MiB 应用分区不能直接写本版 app。

    python -m esptool --chip esp32c3 --port COM14 --baud 460800 --before default_reset --after hard_reset write_flash --flash_mode keep --flash_size keep 0x10000 FoloMusic-v2.0-app.bin

只写 app，不写 bootloader、分区表和 NVS；备份禁止公开。没有双槽 OTA。

## 构建与打包

激活 ESP-IDF 5.5.4，目标 ESP32-C3。Windows 安装路径见[固件入口](../firmware/README.md)，按本机路径调整。不要复用中转 sdkconfig：

    cd firmware/ai-passport
    idf.py --version
    idf.py -B build-direct -D SDKCONFIG=build-direct/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.direct.defaults" build
    idf.py -B build-direct merge-bin -o build-direct/FoloToy-Wangyiyun-full.bin
    python tools/verify_firmware.py build-direct
    python tools/package_release.py build-direct ../../release/FoloMusic-v2.0

打包器检查版本、目标、直连/8 MB 配置、固定分区布局及至少 512 KiB 应用余量。它只读取构建产物，不读取设备；完整镜像 NVS/PHY 区必须全为 0xFF。输出完整镜像、app、manifest 和 SHA256SUMS；已有不同内容时拒绝覆盖。

## 验证

    bash tools/validate.sh --static
    python tests/test_package_release.py
    python tests/test_scheme_b_fonts.py
    cmake -S tests/ui_preview -B build-preview
    cmake --build build-preview
    python tests/ui_preview/render_gallery.py build-preview/scheme_b_preview ../../release/FoloMusic-v2.0/ui-gallery.png

静态门禁需要 C 编译器、actionlint、IDF cJSON 及 mbedTLS 开发库；非标准路径使用 CC、ACTIONLINT_BIN、DIRECT_TEST_CFLAGS、DIRECT_TEST_LDFLAGS。字体测试与图集需要 Pillow；普通编译无需它。重生成字库另外需要 FreeType 和脚本指定的 Noto CJK/Nimbus 字体：python tools/generate_scheme_b_fonts.py --full，并同步生成服务端字符白名单。

主机门禁尊重 TMPDIR；临时目录空间或用户配额不足时，将 TMPDIR 指向有空间的可写目录。KEEP_VALIDATION_ARTIFACTS=1 可保留验证产物，避免误删已有文件。

原生预览不替代真机验收；PassportSim 对当前生产 SDK/联网 API 存在限制，诊断版通过不代表生产仿真通过。正式版按用户当前实测反馈发布，边界见[发布说明](releases/v2.0.md)。

## 提交与发布

检查源码差异、敏感文件、测试及两种构建后提交。标签 v2.0-folo-music，发布标题 Folo Music 2.0；附件来自 release/FoloMusic-v2.0/。不提交二进制缓存、NVS、数据库或原始日志。GitHub 登录只在本机配置，不在文档或聊天传递 Token。
