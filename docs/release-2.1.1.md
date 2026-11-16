# MPV Complete 2.1.1 — 界面字体与版本同步修正

- Windows 菜单和 UOSC 控制文字使用 Segoe UI；中文明确使用 Microsoft YaHei UI，避免日文系统混用游黑体和正黑体。保留图标字体、字幕样式和显式自定义 UI 字体，不打包微软字体。
- 修正公开安装包遗漏界面字体默认值，以及右键菜单默认微软雅黑的问题。
- 安装和升级写入正确的 config-version；公开仓库、配置构建、启动器文件版本与安装器同步到 2.1.1。
- 右键「工具 → 版本与新特性」或 Ctrl+Alt+F1 查看版本。私有版单独标记，不覆盖私有组件与设置。
- 修复兼容基础层的 DV P7/P8 进度条预览：缩略图子进程按基础层生成预览，避免 zscale 的 `no path between colorspaces`。主播放器的 DV、RPU、EL 不变；预览不是 DV 色彩参考。
- 修正 i 信息面板：通用 MaxRGB/Avg 场景统计显示为 `Scene luminance`，不再仅凭这些数值误标 HDR10+。源 DV Profile、RPU 和实际处理路径仍独立显示。

继承 2.1.0 的 GPU 纹理环、HLG FP16、DV 跳转、P7/蓝光菜单、DCP XYZ+ICM 和可选 Omniphony。没有新的解码器改动。
Windows DV 暂停恢复仍可能输出掉帧：此前完整 4K60 HLG 六次暂停恢复记录 30 帧输出丢帧、0 解码丢帧、无回退。P9/CB1 尚未完成。SDR 内屏仍输出 SDR。

2.1.0 因界面字体回归已撤回。2.1.1 在安装及字体验证完成前不作为公开 Release。安装仍使用 ASCII 路径；专有 Dolby、Omniphony 引擎/桥和 libfreemkv 仍为用户自行安装的依赖。

待解决：用户报告厂商 Dolby 组件官方 Logo 不再弹出；播放器状态计数或自绘文字不作为官方 Logo 验收。
