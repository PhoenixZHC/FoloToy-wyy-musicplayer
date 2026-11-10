# 文档索引

当前源码和本地完整包为 **Folo Music 2.2.0**。本轮交付、设备与仓库核验见[当前状态](CONTEXT.md)，功能与限制见[2.2.0 说明](releases/v2.2.0.zh_CN.md)及[代码审阅](code-review-2026-11-10.md)。历史方案及诊断材料不代表当前部署方式。

## 使用与开发

- [项目说明](../README.md)：功能、目录和按键。
- [2.2.0 固件说明](releases/v2.2.0.zh_CN.md)：双平台、顺序/随机、完整镜像、验证及已知问题。
- [2.1.1 历史固件说明](releases/v2.1.1.zh_CN.md)：热点稳定性修复及当时验收边界。
- [2.1 历史说明](releases/v2.1.md)：显示功能更新与当时验收。
- [2.0 发布说明](releases/v2.0.md)：下载文件、兼容性与已知限制。
- [部署与构建](deployment.md)：8 MiB 出厂烧录和保留设置升级。
- [架构](../ARCHITECTURE.md)：设备、网络、媒体、字库与历史中转边界。
- [当前项目状态](CONTEXT.md)：精简状态和后续限制。
- [界面设计规范](scheme-b-ui-design.md)及[完整交互原型](prototypes/scheme-b-complete.html)。
- [界面实现记录](scheme-b-implementation.md)：当前实现与历史验收。
- [顺序播放与随机播放切换方案](playback-mode-plan.md)：已实现主机顺序模块、全歌单随机队列、模式持久化及播放器入口；实机验收待进行。
- [QQ 音乐直连与双平台账号方案](qqmusic-direct-plan.md)：设备扫码、独立会话保存/刷新、整曲 MP3 和重启恢复预检已验证，见[设备预检记录](qqmusic-device-validation.md)；双平台 UI 已接入，正式实机整体验收进行中。
- [固件工程文档](../firmware/ai-passport/docs/INDEX.md)：上游硬件、BSP、贡献与构建参考。

## 历史与研究

- [直连开发计划](netease-direct-client-plan.md)。
- [体验实施评估](music-player-experience-plan.md)与[四版概念图](prototypes/music-player-options.html)。
- [加载问题和开源参考](loading-validation.md)：历史故障、实测和模拟器限制。
- [官方开放平台调查](netease-official-api.md)：已停止作为产品接入路线。
- [历次开发状态](archive/development-status-2026-10.md)：保留过程记录供追溯。
- [播放器实施过程](archive/player-implementation-notes-2026-10.md)：候选版本、调试及开发沟通记录。

旧中转服务源码位于 server/，官方平台实验位于 netease-official-firmware/，均为历史兼容材料，不是 2.2.0 必需组件。
