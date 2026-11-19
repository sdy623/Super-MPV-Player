# Super-MPV-Player 2.3.0-preview.1｜Windows 免安装预览

解压整个目录到英文路径，双击 **MPV-Anime.exe**。续播和蓝光缓存留在这个目录里。保留旧安装即可切回。

## Dolby SDR Bridge

有 Windows Dolby 视频处理、但内屏开不了 Windows HDR？Bridge 让 HDR10 和 HLG 在 SDR 屏上也能交给 Windows Dolby 做亮度映射。它边播边生成动态元数据，不预扫、不转码。显示端仍接收 SDR。

播放时打开底栏 **HDR/DV**，或按 **Ctrl+Alt+H**。系统检测通过后可打开 **Dolby SDR Bridge**，默认关闭。看菜单里的已处理帧数确认是否生效。再次点击恢复原映射。

原片 Dolby Vision 继续使用自身 RPU；Windows Dolby 和 gpu-next Advanced 都保留。系统未提供兼容 Dolby 扩展或未确认 SDR 输出时，Bridge 不可用。

便携包不含厂商 Dolby 组件、个人 ICC、测试素材或续播记录。Dolby 组件须由 Microsoft Store 或设备厂商提供。

## 验证范围

- BOE0CFB 内屏：Ex-Otogibanashi 95、100 秒的同帧开关均通过；4K60 跨切镜连播中，P8.4 处理和转发各到 311 帧，解码和输出掉帧均为 0。
- Sol Levante 160.750 秒：生成 P8.1，1× 播放时处理和转发计数均从 7 增至 21。高亮镜头保留 Dolby 映射的亮度与反差。
- 原片 P8.4 保留原 RPU；gpu-next Advanced 的 BOE 预设仍为 616 / 0.203 / 270、darken。Original Backend 不提供 Bridge。
- P7 FEL 回归在 1.080 秒预读下到达 80.080 秒并完成配对。Bridge 没有改变 P7 直接跳转的已知限制。

D3D11 资源分配失败时会回到原映射。实验中曾在 Ex 95 秒遇到一次共享纹理分配失败；本机便携版短段实播未重现。其它设备和长时播放尚未验收。
