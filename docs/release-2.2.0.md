# MPV Complete 2.2.0 — Windows 开放预览版

**Advanced Dolby Vision SDR Rendering (gpu-next)** 把 Dolby Vision 视频映射到 Windows 明确认定为 SDR-only 的屏幕。开源路径不依赖 Dolby 扩展，也不需要开启 Windows HDR；显示链路输出的仍是 SDR。亮度分配可选根据屏幕余量和场景亮度调整：中亮画面的高光有腾挪空间时才增强，大面积白或暗场点光不会一起被推亮。

## 安装与启用

1. 用 Windows x64 安装包安装到独立的英文路径。现有播放器可保留；仅下载配置 ZIP 不会获得修改过的播放内核。
2. 播放 Dolby Vision，按 `Ctrl+Alt+H` 选择 **Advanced DV to SDR（独立动态映射）**。若已选择官方 Windows 处理，先切回原 gpu-next。
3. 可选：将 `portable_config/script-opts/advanced_color_management.conf` 中的 `advanced_adaptive_highlights` 设为 `yes` 并重启。初始仍为 `no`。设备直出、系统 ICC 与专业 3D LUT 三选一，见[使用指南](advanced-dv-hdr-sdr.md)。

## 这个预览版验证了什么

- P8.4 的同帧软件 A/B 覆盖中亮、高占比白与暗场点光；P7 FEL 覆盖配对增强层下的局部彩色高光。截图和渲染缓冲对比验证 SDR 信号码值，不是屏幕实测亮度，也不等于厂商 Dolby 映射。
- 快速预览和默认设置沿用原有流程；2.1.1 的字体、兼容基础层缩略图和场景亮度标签修正纳入本版。
- HDR-capable、HDR-active 或 Windows 尚未识别的显示器，以及普通 HDR10/HLG，交由常规 gpu-next 路径。其他屏幕、长时播放下的观感和完整测色流程尚需验证。
- 已知限制：P7 FEL 随机跳转若从没有足够 EL 参考帧的 BL 随机访问点开始，首个可用配对帧可能晚于目标。Windows Dolby 可选路径仍可能在暂停恢复时掉帧。此版本没有把两者标记为已修复。

发行包不含厂商 Dolby 组件、设备 ICC、个人 LUT、视频样本、Omniphony 引擎或 libfreemkv。可选依赖、许可和源码构建入口见[开放版说明](open-edition.md)。旧稳定 release 与撤回的历史版本状态不变。
