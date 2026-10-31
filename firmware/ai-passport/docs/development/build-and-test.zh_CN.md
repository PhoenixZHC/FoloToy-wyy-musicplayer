<p align="right">
  <strong>简体中文</strong> · <a href="build-and-test.md">English</a>
</p>

# 构建与验证（Build & Test）

> Folo Music 2.1 产品发布使用 ESP-IDF 5.5.4，并显式启用 sdkconfig.direct.defaults；本页 5.5.3/default 构建描述是上游基线，不作为 2.1 构建命令。正式直连构建、严格 8 MiB 打包和设置清空/保留条件见[产品部署指南](../../../../docs/deployment.md)。静态门禁另运行 4 个打包回归；3 个字体测试与 106 次预览仍单独运行。

使用 ESP-IDF 5.5.3。全新机器或缺少工具链时，先按
[环境引导](environment-setup.zh_CN.md)完成安装。

> 固件编译优先运行 `./tools/validate.sh --firmware`，烧录优先把验证通过的
> `build/FoloToy-Wangyiyun-full.bin` 写入 `0x0`。`idf.py build` 和
> `idf.py flash` 只作为增量开发命令，不作为默认交付方式。

```bash
source <ESP-IDF-v5.5.3-路径>/export.sh
idf.py --version             # 必须输出 ESP-IDF v5.5.3
./tools/validate.sh --firmware # 优先：编译并验证 0x0 合并固件
idf.py set-target esp32c3     # 配置目标芯片（fresh checkout 后/换 target 后运行）
idf.py build                  # 可选：增量 app 编译
idf.py flash monitor          # 可选：增量 app 烧录
idf.py fullclean              # 只清空过期生成状态（勿用于清理用户源码改动）
```

`idf.py fullclean` 不能让已有 `sdkconfig` 完整同步变更后的 defaults。需要重建
target 或已跟踪 defaults 时，先保留有意的本地设置，再运行
`idf.py set-target esp32c3`。

仓库提交 `dependencies.lock` 以固定 ESP-IDF Managed Components 的解析结果。修改 `idf_component.yml` 后必须使用 ESP-IDF 5.5.3 重新生成锁文件、review 版本变化并与 manifest 一起提交；普通构建不应产生未提交的锁文件差异。

固件门禁使用全新的临时构建目录，并从仓库 `sdkconfig.defaults` 生成隔离的 `sdkconfig`。它不会读取或覆盖开发者根目录的 `sdkconfig`，只把验证通过的合并镜像复制到 `build/FoloToy-Wangyiyun-full.bin`。

当前基线含一个可独立运行的纯逻辑测试：

```bash
cc -std=c11 -Wall -Wextra -Werror -Imain \
  tests/test_ui_pixel_math.c main/ui_pixel_math.c \
  -o /tmp/test_ui_pixel_math
/tmp/test_ui_pixel_math
```

统一验证入口：

```bash
./tools/validate.sh --static    # 仓库一致性、workflow、文档链接、敏感信息、host tests
./tools/validate.sh --firmware  # ESP-IDF build、merge-bin、固件偏移校验
./tools/validate.sh             # 完整验证
```

完整验证要求预先激活 ESP-IDF 5.5.3。CI 与本地使用同一脚本；若 CI 和本地行为不同，应先修复脚本或环境，而不是维护两份命令。

电脑版固件仿真运行 `node tools/run_passportsim.cjs <wasm> <merged.bin> <app.elf> /tmp/report.json baseline`。脚本记录镜像与模拟器哈希，遇到 tripwire 或尚未支持的生产场景时明确失败。当前 PassportSim 1.0.3 支持 IDF 5.5.3 Wi-Fi，但不支持 `esp_wifi_set_ps`；仅在独立临时诊断构建省略该调用后，可执行 `diagnostic-wifi` 正常连接及 `diagnostic-wifi-absent` 超时重新扫描。两者都不是发布验收。`diagnostic-wifi-wrong-password` 因模拟器未建模错误密码而返回 `E_HLE`。不得根据受限诊断结果烧录。

直连主机测试需要 mbedTLS 开发头文件与 `libmbedcrypto`。非系统路径通过
`CC`、`DIRECT_TEST_CFLAGS`、`DIRECT_TEST_LDFLAGS` 指定。静态门禁运行 13 个
测试程序，包括显示计时与日夜边界、两种配网模式和独立 HTTP/NVS 替身。设置
`KEEP_VALIDATION_ARTIFACTS=1` 保留临时测试与构建目录。此 fork 的直连构建按
仓库根 README 使用 ESP-IDF 5.5.4；本上游指南的 5.5.3 基线不改变该选择。

涉及物理外设的改动必须在真机运行硬件指南验收清单，并把“编译通过”与“硬件验证通过”分开记录。
