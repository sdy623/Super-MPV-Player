from pathlib import Path
import argparse,json,re,shutil
parser=argparse.ArgumentParser(description='Generate the Windows installer file lists.')
parser.add_argument('--core-source-tree',required=True,help='Git tree ID of core/mpv used to build the payload')
args=parser.parse_args()
if not re.fullmatch('[0-9a-f]{40}',args.core_source_tree):
 parser.error('--core-source-tree must be a full Git tree ID')
R=Path(__file__).resolve().parent;P=R/'payload'
(P/'portable_config/config-version').write_text('v2.2.0-open\n',encoding='utf8')
(P/'安装与启动说明.txt').write_text('''Super-MPV-Player 2.2.0 — 综合播放器开放版

使用英文安装路径（新装默认 D:\\Apps\\Super-MPV-Player）。本版本的蓝光 Java 运行时不支持中文安装路径，安装器会提前拒绝，原始媒体文件名不受此安装路径限制。
Windows Dolby Vision：标准启动器在检测到兼容组件时尝试 Windows 处理；请从 Microsoft Store 或设备厂商安装扩展。本包不含 Dolby 专有组件、激活材料或设备 ICC。当前色彩配置适配单显示器；多屏或缺少系统配置时回退到原映射。SDR 屏输出 SDR。
Advanced Dolby Vision SDR Rendering (gpu-next)：使用专用快捷方式，将 Dolby Vision 映射至兼容 SDR 屏，无需 Dolby 扩展。进阶亮度分配默认关闭。显示器参数与 ICC/3D LUT 设置见 docs/advanced-dv-hdr-sdr.md。
Omniphony 对象音频独立选择，默认关闭。引擎和解码桥请从「HDR 与声音」菜单的官方链接安装。保留 Studio 库优先，避免重复启用耳机虚拟环绕。
支持 BD-J 菜单与未加密 DCP CPL 画面、音轨配对。播放器启动不弹控制台，不自动安装专有组件，不关闭安全软件或添加排除。
安装不接管旧实例、不自动启动、不改文件关联；升级保留用户配置，卸载保留个人配置和未知文件。
新版快捷方式归入 Super-MPV-Player；旧名称的快捷方式保留，以免影响并存的旧安装。
libfreemkv 及其桥程序也由用户自行安装、配置和更新；本包仅保留调用接口，详见 EXTERNAL-COMPONENTS.md。
4K60 P8.4/HLG Windows 杜比路径使用GPU共享纹理及有界预缓冲。暂停恢复仍可能输出掉帧；不是零掉帧修复版。完整结果与限制见GitHub Release。
''',encoding='utf8')
shutil.copy2(R/'src/installer.nsi',P/'package-source/installer.nsi')
files=sorted(f for f in P.rglob('*') if f.is_file() and f.name!='package-manifest.json')
assert not any(f.suffix.lower() in {'.icc','.icm'} for f in files)
assert not any(f.name.lower()=='orender.dll' or (f.name.lower().startswith('harletty') and f.name.lower().endswith('_bridge.dll')) for f in files)
assert not any(f.name.lower() == 'mpv-freemkv-bridge.exe' for f in files)
assert not any(f.name.lower() in {'keydb.cfg','cookies.txt','recent.json','celebi.txt','skip-intro.json'} for f in files)
(P/'package-manifest.json').write_text(json.dumps({'version':'2.2.0','core_source_tree':args.core_source_tree,'files':[{'path':f.relative_to(P).as_posix(),'bytes':f.stat().st_size} for f in files]},indent=2),encoding='utf8');files.append(P/'package-manifest.json')
inst=[];uninst=[];dirs=set();current=None;overwrite=None
def esc(s):return s.replace('$','$$')
for f in files:
 rel=f.relative_to(P);d=str(rel.parent);keep=rel.parts[0]=='portable_config' and f.suffix.lower() in {'.conf','.json','.ini','.txt','.yaml'}
 if d!=current:inst.append('SetOutPath "$INSTDIR'+('\\'+esc(d) if d!='.' else '')+'"');current=d
 mode='off' if keep else 'on'
 if mode!=overwrite:inst.append('SetOverwrite '+mode);overwrite=mode
 inst.append('File "${PAYLOAD}\\'+esc(str(rel))+'"')
 if rel.parts[0]!='portable_config':uninst.append('Delete "$INSTDIR\\'+esc(str(rel))+'"');dirs.update(str(p) for p in rel.parents if str(p)!='.')
for d in sorted(dirs,key=lambda x:(x.count('\\'),len(x)),reverse=True):uninst.append('RMDir "$INSTDIR\\'+esc(d)+'"')
(R/'src/install-files.nsh').write_text('\n'.join(inst)+'\n',encoding='utf8');(R/'src/uninstall-files.nsh').write_text('\n'.join(uninst)+'\n',encoding='utf8')
print(len(files),'files')
