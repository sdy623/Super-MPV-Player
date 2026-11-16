# Omniphony：用户自行安装的可选音频组件

本安装包只含 MPV 接口、菜单和布局示例，不分发 Omniphony 引擎或解码桥 DLL。视频 DV 与音频模式独立。

1. 从 [Omniphony v0.7.0 官方发布页](https://github.com/mgth/Omniphony/releases/tag/v0.7.0) 选择 `omniphony-studio-egui_0.7.0_x64-setup.exe`（Studio 单独安装器），安装后启动一次 Studio，让它部署引擎。不需要用上游播放器替换本播放器。
2. 从 [harletty-bridge v0.9.0 官方发布页](https://github.com/harletty/harletty-bridge/releases/tag/v0.9.0) 下载 Windows x64 解码桥，阅读其许可证后自行安装。将所需的 *_bridge.dll 放到 `%LOCALAPPDATA%\omniphony\lib\`，或使用自己的 `ad-orender-bridge-path` 配置。
3. 重启本播放器，在「HDR 与声音 → Omniphony 对象音频」选择耳机或音箱。

当前接口配对为 liborender ABI 0.12、bridge_api 0.6.x。不要只因文件存在就认为版本兼容；引擎负责握手与解码桥验证。上述官方发布页也可从播放器菜单打开；本包不会替用户下载或安装组件。

默认保留上游搜索顺序：明确指定的库 / ORENDER_LIBRARY、Studio 部署的用户库、播放器旁的用户安装库。未指定时 Studio 库优先。缺少或不兼容的组件保留原音频解码；安装或更新后需要重启播放器。

引擎和解码桥具有各自的许可证。不要无意叠加 Omniphony 耳机渲染与 Windows 耳机空间音效。PCM / DCP 使用原解码器；更改音频模式不会切换视频 DV。压缩音频由所装桥与引擎处理，不等于交给 Dolby Access 原码解码或 HDMI 直通。

Studio 控制连接默认关闭；用户启用时仅绑定本机回环地址。音箱示例为双声道，可按实际硬件修改布局。
