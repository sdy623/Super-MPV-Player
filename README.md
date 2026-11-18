# Super-MPV-Player

基于 mpv 的 Windows 播放器。可使用系统 Dolby Vision 组件处理视频，也能在仅支持 SDR 的屏幕上用开放渲染路径播放 Dolby Vision。

**Windows 2.2.0 开放预览版**：[下载](https://github.com/sdy623/Super-MPV-Player/releases) · [版本说明与已知限制](docs/release-2.2.0.md) · [English](README_en.md)

- **[Windows Dolby Vision 处理](docs/open-edition.md#windows-dependency)。** 有兼容环境时优先使用。安装 Microsoft Store 或设备厂商提供的授权组件后，播放器会尝试处理符合条件的输入。未能接管时，视频留在 gpu-next。以 HDR 菜单里的处理帧数和回退状态为准。
- **[Advanced Dolby Vision SDR Rendering (gpu-next)](docs/advanced-dv-hdr-sdr.md)。** 面向 Windows 识别为仅支持 SDR 的屏幕，按显示能力和片源元数据映射亮度与色域。可选的自适应高光根据屏幕余量和场景决定何时加亮，也会约束大面积白和暗场小高光。无需 Dolby 扩展，显示链路只输出 SDR。
- **可选音频模式：[Omniphony 对象音频](docs/open-edition.md#omniphony-install-the-optional-components-yourself)。** 使用兼容解码桥，将受支持的 Atmos 等音轨渲染为双耳耳机或按音箱布局输出。引擎与解码桥须自行安装；音频可搭配任一视频模式。
- **蓝光原盘。** 打开光驱、ISO 或 BDMV 文件夹，可使用兼容的 BD-J 原盘菜单。加密光盘需要自行配置可用的解密后端；安装包不含密钥。
- **无加密 2D DCP。** 从 CPL 配对画面和 PCM 音轨，按 Reel 分章。XYZ 画面经 gpu-next 浮点处理和显示器 ICM 输出；不支持 KDM 或 DCP 字幕。
- **UOSC 播放界面。** 时间轴提供缩略图和章节，`i` 面板可查源色彩、场景亮度、DV 元数据状态与实际渲染路径。有兼容基础层的 DV 缩略图从基础层生成。

### Dolby Vision 支持一览

| Profile | Windows Dolby（优先） | gpu-next 开放路径 |
| --- | --- | --- |
| **P5** · HEVC | ✓ 旧版实测 | 有处理路径，本版未测 |
| **P7 MEL** · UHD Blu-ray | 有处理路径，未单独验证 MEL | 有处理路径，未单独验证 MEL |
| **P7 FEL** · UHD Blu-ray | ✓ 旧版实测 EL 重建 | ✓ 本版实测 EL 重建，需预读¹ |
| **P8.1** · HDR10 基础层 | ✓ 旧版实测 | 有处理路径，本版未测 |
| **P8.4** · HLG 基础层 | ✓ 本版实测，含 Atmos 同播 | ✓ 本版实测 |
| **P9** · AVC / SDR 基础层 | 不支持 | 可识别，本版未实播验收 |
| **P10** · AV1 | 不支持 | 可识别，本版未实播验收 |

✓ 表示对应路径有样本实播记录。¹ P7 FEL 的 80.080 秒同帧测试用了预读；直接跳转仍可能晚出帧。[BL／EL／RPU 与 Atmos 实测详情](docs/playback-capabilities.md)

---

> **Anime / Blu-ray plugin fork:** [安装、子模块和上游补丁 / Setup, submodules and upstream patch](FORK.md).

## 上游主题与独立配置包

以下为沿用的跨平台主题与配置说明。Windows 开放版的修改内核和功能请使用上方的安装包；上游 mpv 或单独的配置 ZIP 不包含这些修改。

## 简要说明

- 两套主题皮肤`modernz`、`uosc`，漂亮的现代化 UI 无边框设计
- 使用原生 mpv 播放器，配置/播放器分离，无需担心播放器无法更新到最新版
- 集成进度条缩略图预览
- 集成在线中文字幕搜索，全网弹幕在线加载
- 集成 Anime4K 超分画质，实时提升画质
- 支持多种视频着色器滤镜：[shaders 滤镜说明指南](https://github.com/akFace/mpv.config/wiki/%E8%A7%86%E9%A2%91%E6%BB%A4%E9%95%9C%E9%80%89%E6%8B%A9%E6%8C%87%E5%8D%97)
- 支持插帧模式功能，提升流畅度
- 视频滤镜、Anime4K 可右键菜单按需加载
- 支持 360°VR 全景视频
- 可视化调节 eq 均衡器、声道切换、自动 HDR、解码切换、调色等常用功能
- Windows、macOS、Linux 全平台一致的强大右键菜单功能
- 轻量级，超简单，只需简单几步骤即可完成享用
- [>>查看主题预览效果截图](https://github.com/akFace/mpv.config#%E9%A2%84%E8%A7%88%E6%95%88%E6%9E%9C%E5%9B%BE)
- [>>常见相关问题](https://github.com/akFace/mpv.config#一些常用设置文档可选)

## 独立主题配置的使用方法

### 1. 先安装播放器

- **Windows：**
  - [官方构建版本（推荐）](https://github.com/mpv-player/mpv/releases)
  - [每日构建版本（推荐）](https://github.com/zhongfly/mpv-winbuild/releases)
  - [shinchiro 版本](https://github.com/shinchiro/mpv-winbuild-cmake/releases)
  - [mpv.net 版本](https://github.com/mpvnet-player/mpv.net/releases)
- **macOS、Linux：** [mpv 官网下载](https://mpv.io/installation/)

### 2. 下载主题配置

[🎯 点击下载](https://github.com/akFace/mpv.config/releases) 你需要的主题皮肤，目前提供：

- `modernz`
- `uosc`

每个主题皮肤压缩包均包含完整功能的配置，下载后解压即可。

### 3. 配置播放器

- **以下案例以 Windows 系统为例： ① 和 ② 根据自己选择的播放器按对应教程来即可**
- **①. mpv 原生播放器**：解压/安装播放器后，在播放器根目录(`mpv.exe` 同目录)下新建名为 `portable_config` 文件夹，作为`配置文件夹`
- **②. mpv.net 播放器**：如图所示，右键>配置>打开配置文件夹或者`Ctrl + f`快捷键打开`配置文件夹`

  ![image](https://raw.githubusercontent.com/akFace/mpv.net.config/master/preview/Snipaste_2026-03-16_20-34-06.jpg)

- **将解压出的全部文件复制到`配置文件夹`(只能共存一个主题配置)，重启播放器即可**

- **注意目录结构**

```
一般的目录结构如下:
~/mpv/配置文件夹目录
      ├── fonts
      ├── scripts
      ├── script-opts
      ├── mpv.conf
      └── input.conf
```

一般 mpv 安装版`setup-install`（非 `Portable` 便携版）全局配置文件目录：

```
Linux:   ~/.config/mpv/
Windows: %APPDATA%/mpv/
macOS:   ~/Library/Application Support/mpv/
```

> ⚠️ **提示**：若你使用的是 mpv.net 播放器，出现偶尔无法加载缩略图的情况，请修改`script-opts/thumbfast.conf`目录中的`mpv_path=mpv`改为`mpv_path=mpvnet`或者播放器安装目录可执行文件 例如：`mpv_path=C:\Program Files\mpv.net\mpvnet.exe`
>
> - 其他平台相关： [uosc_danmaku-issues](https://github.com/Tony15246/uosc_danmaku/issues/194)，[右键菜单故障排查文档](https://github.com/akFace/mpv-menu-plugin-next/blob/main/doc/README.md#linux)

### **[👉 查看常用快捷键！推荐记住一些常用的即可](https://github.com/akFace/mpv.config/wiki/%E5%BF%AB%E6%8D%B7%E9%94%AE)**

### 一些常用设置&文档（可选）

- Uosc 主题自定义文档：[Uosc](https://github.com/tomasklaen/uosc#options)
- ModernZ 主题自定义文档：[ModernZ](https://github.com/Samillion/ModernZ#customization)
- 弹幕默认样式在配置文件夹`script-opts/uosc_danmaku.conf`下，要修改请使用文本编辑器打开编辑
- 弹幕相关配置：[查看文档](https://github.com/Tony15246/uosc_danmaku#%E7%9B%AE%E5%BD%95)
- **关于弹幕流畅度问题**，目前本人的设备屏幕是 4k60hz，要打开`video-sync=display-resample`才流畅，但有些用户的设备打开此设置开倍速播放会导致声音卡问题，因此现在默认关闭，需要设置的请打开 mpv.conf 文件编辑，删除此行代码最前面的 # 号
- 跳过片头片尾：在配置文件夹中`mpv.conf`,打开编辑，可看到注释的跳过片头片尾，把注释的#号去掉，填写上自定义的片头片尾时间重启播放器即可
- 直接播放 B 站、YouTube 视频或其他链接：安装 [yt-dlp](https://github.com/yt-dlp/yt-dlp/releases) ，进入下载 yt-dlp.exe，放到 `mpv.exe` 同目录下，即：播放器安装目录。重启即可直接粘贴视频页面链接，不过更推荐下边的浏览器插件
- **推荐：** 油猴脚本 👉 [play-with-mpv 使用 mpv 播放网页中的视频](https://github.com/akFace/play-with-mpv)
- **语言/language:** The language setting for the modernz theme is in `script-opts/modernz.conf`, and the language setting for the uosc theme is in `script-opts/uosc.conf`. You can see it by searching for the keyword `language` in the file.，Download [`input-en.conf`](https://github.com/akFace/mpv.config/blob/master/src/input-en.conf) and rename it to `input.conf`, then replace the original file

## 如何更新独立主题配置

- 播放器更新：直接下载最新版[🎬[mpv.net 播放器]](https://github.com/mpvnet-player/mpv.net/releases) 或者[[mpv 原生播放器]](https://mpv.io/)安装即可
- 配置主题皮肤更新：直接下载最新版[🎯 点击下载](https://github.com/akFace/mpv.config/releases) 解压覆盖即可

## 预览效果图：

### 主题皮肤 1（modernz）

![image](https://raw.githubusercontent.com/akFace/mpv.net.config/master/preview/Snipaste_2026-03-16_20-32-59.jpg)

### 主题皮肤 2（uosc）

![image](https://raw.githubusercontent.com/akFace/mpv.net.config/master/preview/Snipaste_2026-08-24_03-00-53.jpg)

### 功能强大的右键菜单

default：
![image](https://raw.githubusercontent.com/akFace/mpv.net.config/master/preview/Snipaste_2026-08-15_00-09-37.jpg)
macos-white：
![image](https://github.com/akFace/mpv.config/raw/master/preview/Snipaste_2026-08-16_01-04-38.jpg)
macos-dark：
![image](https://github.com/akFace/mpv.config/raw/master/preview/Snipaste_2026-08-16_01-10-14.jpg)

### 可视化均衡器 eq

![alt text](https://github.com/akFace/equalizer-gui/raw/main/images/Snipaste_2026-08-13_18-08-43.jpg)

## 相关链接

鸣谢以下开源项目提供的便利：

- [mpv](https://github.com/mpv-player/mpv)
- [mpv-winbuild](https://github.com/zhongfly/mpv-winbuild)
- [shinchiro](https://github.com/shinchiro/mpv-winbuild-cmake)
- [mpv.net](https://github.com/mpvnet-player/mpv.net)
- [Thumbfast](https://github.com/po5/thumbfast)
- [UOSC](https://github.com/tomasklaen/uosc)
- [ModernZ](https://github.com/Samillion/ModernZ)
- [mpv-sub-assrt](https://github.com/dyphire/mpv-sub-assrt)
- [uosc_danmaku](https://github.com/Tony15246/uosc_danmaku)
- [Anime4K](https://github.com/bloc97/Anime4K)
- [mpv360](https://github.com/kasper93/mpv360)
- [Equalizer-GUI](https://github.com/akFace/equalizer-gui)
- [mpv-menu-plugin-next](https://github.com/akFace/mpv-menu-plugin-next)
- [play-with-mpv](https://github.com/akFace/play-with-mpv)
- [recent-menu](https://github.com/natural-harmonia-gropius/recent-menu)
- [awesome-mpv](https://github.com/stax76/awesome-mpv)
- [mpv 中文配置手册](https://hooke007.github.io/official_man/index.html)
