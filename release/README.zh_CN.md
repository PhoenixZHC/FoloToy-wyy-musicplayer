<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 固件交付目录

## 当前本地交付：Folo Music 2.3.0

完整镜像为 FoloMusic-v2.3.0/FoloMusic-v2.3.0-full-8MB.bin，严格 8,388,608 字节。SHA-256：6c251108afe22251074e422fd436ef7a0a3e41708d5cf21facb81c32b99610b9。

同目录包含 app 升级包、manifest.json 和 SHA256SUMS.txt。生成的 FoloMusic-v*/ 目录不进入 Git；新克隆的仓库需按[部署指南](../docs/deployment.md)构建，打包时加 --version 2.3.0；变更、已知问题及验收边界见[2.3.0 说明](../docs/releases/v2.3.0.zh_CN.md)。本次源码推送不等于创建 GitHub Release。

完整镜像从 0x0 烧录，会清空 Wi-Fi、登录和显示设置；仅在匹配 7 MiB factory 分区时，只写 app 0x10000 才保留设置。公开镜像 NVS/PHY 为空白，不含已配置设备的凭据。

## 之前的本地产物

| 目录或文件前缀 | 用途 |
| --- | --- |
| FoloMusic-v2.1.1/ | 前次热点修复交付，保留当时验证记录 |
| FoloMusic-v2.1-hotspot/ | 首轮热点测试包，实机播放失败，保留作故障对照 |
| FoloMusic-v2.1-hotspot-devicefix/ | 已实测的补修包，版本号仍为 2.1；当前交付已由 2.1.1 替代 |
| FoloToy-Wangyiyun-Complete-v1.2.2* | 历史中转完整包和固件 |
| FoloToy-Wangyiyun-Relay-Windows-x64-v1.2.2.zip | 历史 Windows 中转服务 |

历史产物保留追溯，不作为当前直连固件。私有日志、本机工具和备份放在仓库根目录被忽略的 .local-backup/；构建产物留在被忽略的固件 build 目录，源码、测试夹具及文档仍按原组件目录管理。
