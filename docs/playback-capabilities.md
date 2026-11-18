# Super-MPV-Player 2.2.0 播放验证范围

本页区分已在候选内核运行的样本、旧版本的实播记录和只确认过的代码路由。单个样本通过不代表同一 Profile 的所有封装、硬解、跳转和显示器都通过。

## Dolby Vision 视频

BL 是基础图像层，EL 是增强层，RPU 携带重建和显示映射元数据。开放路径使用 RPU 的图像重塑参数和可用的 L1 场景亮度，不承诺完整复现 Dolby 专有色彩管理或 L2/L8 等创作者微调。P7 MEL 的 EL 没有有效图像残差，仍需正确解释 RPU；P7 FEL 要用时间戳配对的 EL 和有效残差参数（NLQ）重建图像。视频被识别为 P7 或检测到 EL，并不能证明该帧完成了 FEL 重建。

| 输入 | 基础层与元数据 | 开放 gpu-next：2.2.0 候选 | Windows Dolby 路径 |
| --- | --- | --- | --- |
| P5 · HEVC | IPT-PQ-C2，BL + RPU，无普通 HDR10/SDR 基础层回退 | 有处理路径，本版未实播复测 | 走 `dvhe.05`；旧版有实际 SDR 输出 |
| P7 MEL · HEVC | UHD Blu-ray HDR 兼容 PQ 基础层，另有 EL 和 RPU；无有效 EL 像素残差 | 有处理路径，本版未用独立确认的 MEL 样本复测 | 适配到 `dvhe.08`；旧版 P7 导航和配对测试未独立验证 MEL |
| P7 FEL · HEVC | UHD Blu-ray HDR 兼容 PQ 基础层，另有 EL 和 RPU；有效残差须解码并配对 | 软件 HEVC + D3D11 验证一帧：预读后在 **80.080 秒**确认 FEL 配对与图像贡献 | 先用兼容 GPU 辅助模块重建，再量化为 P010 送往 `dvhe.08`；旧版有实播，辅助模块或帧条件不符时回退 |
| P8.1 · HEVC | HDR10/PQ 兼容 BL + RPU，无 EL | 有处理路径，本版未用独立源复测 | 走 `dvhe.08`；旧版短测有实际 SDR 输出 |
| P8.4 · HEVC | HLG 兼容 BL + RPU，无 EL | D3D11 硬解样本在 **87、95、100 秒**验证同帧输出；核对 HLG 源、DV/PQ 渲染输入及 BT.709/sRGB SDR 输出 | 走 `dvhe.08`，保留 HLG 输入语义；有实际 SDR 输出记录 |
| P9 · AVC | BT.709 SDR 兼容 BL + RPU，无 EL | 识别与路由有代码覆盖，本版未用实片验收 | 当前 HEVC 桥不支持 |
| P10 · AV1 | BL + AV1 内的 DV 元数据，无 EL；CCID 0 无通用回退，1 对应 HDR10/PQ，4 对应 HLG | 开放路径可识别，本版未验收三种变体的实播 | 当前 HEVC 桥不支持 |

**80.080 秒跳转仍有限制。** 曾复现直接跳到 80.080 秒时，BL 已到目标，但 EL 解码缺少更早的参考帧，首组可用配对落在 80.330 秒。该样本复测加了 1.080 秒解复用预读，才在目标时间取得配对。这个做法验证了原因与目标帧，不是所有片源通用的跳转修复。关闭 EL 贡献与关闭 RPU 是两种对照；P7 的 EL 开关会保留 RPU。

Windows 路径需满足[组件与显示条件](open-edition.md#windows-dependency)。可参考[历史 Windows SDR 输出验证](native-sdr-profile-verification.md)和 Dolby 的[公开 Profile 定义](https://ott.dolby.com/OnDelKits/Dolby_Vision_Online_Delivery_Kit/v1/Documentation/Specs/Visio_Profiles/help_files/topics/c_dovi_profiles.html)及 [MEL 判别规则](https://ott.dolby.com/OnDelKits/Dolby_Vision_Online_Delivery_Kit/v1/Documentation/Specs/Visio_Profiles/help_files/topics/c_differ_mel_non.html)。已测显示链路输出 SDR；日志中的处理状态不等于原生 Dolby Vision HDR 信号或厂商认证。

## Omniphony 对象音频

符合版本的外部引擎和解码桥可把 Dolby TrueHD/Atmos、Dolby Digital Plus（E-AC-3 JOC）/Atmos 渲染为耳机双耳或按音箱布局输出的 PCM。没有对象数据的普通 AC-3、TrueHD 或 E-AC-3 不标为 Atmos；此路径不提供 Dolby MAT 或 Atmos HDMI 位流直通。

| 输入 | 实播记录 |
| --- | --- |
| E-AC-3 JOC / Dolby Digital Plus Atmos | [2.0.0 安装版](release-2.0.0.md#verification)已验证与 Windows P8.4 处理同播，含暂停恢复。 |
| TrueHD / Atmos | 旧候选的 P7 蓝光片段同时记录 Windows 处理、EL 配对和 `LFE+11 objects`。测试有暂停恢复和跳转，未覆盖全片。2.2.0 未重测这组。 |
| 其他输入 | 核心有 AC-3、DTS 和 IAMF 的接入点；具体格式取决于引擎与桥，本版未做逐格式验收。PCM/DCP 保留原解码。 |

Windows 视频与对象音频可独立选择。未安装兼容引擎或桥时，有常规解码器的音轨继续走原解码；IAMF 需要兼容桥。安装方式见 [Omniphony 依赖说明](open-edition.md#omniphony-install-the-optional-components-yourself)。公开包不含测试影片、音轨或第三方解码桥。
