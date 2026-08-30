# 完整发布包设计

## 目标

为中转版本制作一个可以直接交付的完整发布包，同时把项目根目录和 `release/` 中的临时内容整理干净。

## 交付结构

最终目录为 `release/FoloToy-Wangyiyun-Complete-v1.2.2/`，包含：

- `firmware/FoloToy-Wangyiyun-full-8MB.bin`：从 flash 地址 `0x0` 开始写入、末尾以 `0xFF` 补齐到 8,388,608 字节的 ESP32-C3 固件。
- `server/FoloToy-Wangyiyun-Relay-Windows-x64-v1.2.2.zip`：可在普通 Windows x64 电脑解压运行的中转服务。
- `使用说明.md`：面向最终用户的烧录、启动和配网步骤。
- `SHA256SUMS.txt`：固件和中转服务 ZIP 的 SHA-256。

交付目录还会压缩为 `release/FoloToy-Wangyiyun-Complete-v1.2.2.zip`。

## 固件完整性

先用 ESP-IDF v5.5.4 重新构建当前中转版固件并生成合并镜像，再验证 bootloader、分区表和应用镜像位于构建参数指定的偏移。验证通过后，只在镜像末尾补 `0xFF`，使文件严格等于 8 MB；不得截断或移动任何镜像。

## 文档和整理范围

更新根目录 `README.md`、`docs/deployment.md` 和 `CONTEXT.md`，说明完整包结构、烧录地址和使用方式。删除已经确认可重新生成的 `release/FoloToy-Wangyiyun-Relay-Windows-x64-v1.2.2/` 展开目录，以及根目录两个 `.codex-figma-check*` 临时目录。保留所有源码、构建目录和官方直连版工程。

## 验收

- 8 MB 固件文件大小必须为 8,388,608 字节。
- 合并镜像各组成文件的偏移和内容必须一致。
- 完整发布 ZIP 能正常解压，且只包含规定的交付文件。
- `SHA256SUMS.txt` 中的校验值与实际文件一致。
- 文档中的文件名、版本和命令与实际交付物一致。
