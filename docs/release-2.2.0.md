# Super-MPV-Player 2.2.0 Windows 开放预览版

## 本版功能

- **Windows Dolby Vision 处理。** 播放器默认尝试 Windows 路径，需有兼容的 Microsoft Store 或厂商授权组件、单屏和系统 ICC。输入需同时符合组件与桥接的处理条件；未能接管时继续用 gpu-next。HDR 菜单可查处理帧数与回退状态。测试屏幕输出 SDR。
- **Advanced Dolby Vision SDR Rendering (gpu-next)。** 把 Dolby Vision 映射到 Windows 识别为仅支持 SDR 的屏幕。无需 Dolby 扩展，输出 SDR。可选的自适应高光按显示余量和场景分配亮度，避免大面积白和暗场小高光被一起推亮。
- **Omniphony 对象音频。** 有兼容引擎和解码桥时，可将受支持的 Atmos 等音轨渲染到双耳耳机或指定音箱布局。引擎与桥分开安装，音频模式可搭配任一视频路径。
- **蓝光与影院包。** 光驱、ISO 和 BDMV 文件夹可使用兼容的 BD-J 菜单；加密光盘须自备后端。无加密 2D DCP 按 CPL 组合画面与 PCM 音轨，XYZ 画面经 gpu-next 浮点处理与显示器 ICM 输出。不支持 KDM 或 DCP 字幕。
- **UOSC 播放界面。** 时间轴保留缩略图和章节，`i` 面板显示源色彩、场景亮度、DV 元数据状态与渲染路径。

[查看各 Dolby Vision Profile 与对象音频的验证范围](playback-capabilities.md)。

## 安装与启用

1. 从 [GitHub Releases](https://github.com/sdy623/Super-MPV-Player/releases) 选择 `Super-MPV-Player-2.2.0-x64-Setup.exe`，安装到独立的英文路径，例如 `D:\Apps\Super-MPV-Player`。现有播放器可保留。修改过的播放内核只在完整安装包中。
2. 播放 Dolby Vision，按 `Ctrl+Alt+H` 查看 Windows 处理状态。在仅支持 SDR 的屏幕上，也可手动选择 **Advanced DV to SDR（独立动态映射）**。Windows 回退到普通 gpu-next 不会自动打开这个预设。
3. 可选：将 `portable_config/script-opts/advanced_color_management.conf` 中的 `advanced_adaptive_highlights` 设为 `yes` 并重启。初始仍为 `no`。设备直出、系统 ICC 与专业 3D LUT 三选一，见[使用指南](advanced-dv-hdr-sdr.md)。

## 这个预览版验证了什么

- P8.4 的同帧软件 A/B 覆盖中亮、高占比白与暗场点光；P7 FEL 覆盖配对增强层下的局部彩色高光。截图和渲染缓冲对比验证 SDR 信号码值，不是屏幕实测亮度，也不等于厂商 Dolby 映射。
- 本版带入 2.1.1 的 Windows 界面字体修正。P7/P8 缩略图读取兼容基础层；`i` 面板不再把通用场景亮度误标为 HDR10+。
- HDR-capable、HDR-active 或 Windows 尚未识别的显示器，以及普通 HDR10/HLG，交由常规 gpu-next 路径。其他屏幕、长时播放下的观感和完整测色流程尚需验证。
- 已知限制：P7 FEL 随机跳转若从没有足够 EL 参考帧的 BL 随机访问点开始，首个可用配对帧可能晚于目标。Windows Dolby 可选路径仍可能在暂停恢复时掉帧。此版本没有把两者标记为已修复。

发行包不含厂商 Dolby 组件、设备 ICC、个人 LUT、视频样本、Omniphony 引擎或 libfreemkv。可选依赖、许可和源码构建入口见[开放版说明](open-edition.md)。旧稳定 release 与撤回的历史版本状态不变。
