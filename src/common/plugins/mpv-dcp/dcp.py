"""Read-only SMPTE/Interop DCP composition -> native mpv EDL timeline.
No media writes, no remuxing, no subprocesses, no network, no KDM processing.
"""
import json
import sys
import xml.etree.ElementTree as ET
from fractions import Fraction
from pathlib import Path
from urllib.parse import unquote, urlsplit

def name(e): return e.tag.rsplit('}',1)[-1]
def child(e,k): return next((c for c in e if name(c)==k),None)
def text(e,k,default=None):
    c=child(e,k)
    if c is None or c.text is None:
        if default is not None:return default
        raise ValueError('CPL 缺少 '+k)
    return c.text.strip()
def xml(p):
    if p.stat().st_size>8*1024*1024:raise ValueError('DCP XML 过大')
    data=p.read_bytes()
    if b'<!DOCTYPE' in data.upper() or b'<!ENTITY' in data.upper():raise ValueError('不接受 XML 外部实体')
    return ET.fromstring(data)
def quote(s):return f'%{len(s.encode("utf-8"))}%{s}'
def seconds(v):return format(float(v),'.12f').rstrip('0').rstrip('.') or '0'
def prepare(source):
    source=Path(source).resolve(strict=True)
    root=source if source.is_dir() else source.parent
    maps=[p for p in root.iterdir() if p.name.upper() in ('ASSETMAP','ASSETMAP.XML')]
    if len(maps)!=1:raise ValueError('需要一个 ASSETMAP；请拖入完整 DCP 文件夹')
    amap=xml(maps[0]); al=child(amap,'AssetList')
    if name(amap)!='AssetMap' or al is None:raise ValueError('无效 ASSETMAP')
    assets={}
    for a in al:
        id=text(a,'Id').lower()
        if id in assets:raise ValueError('ASSETMAP 资产 ID 重复')
        assets[id]=a
    def resolve(id):
        a=assets.get(id.lower())
        if a is None:raise ValueError('ASSETMAP 缺失资产：'+id+'（VF 需要对应 OV）')
        cl=child(a,'ChunkList'); chunks=list(cl) if cl is not None else []
        if len(chunks)!=1:raise ValueError('暂不支持分块/跨卷 DCP 资产')
        c=chunks[0]
        if text(c,'VolumeIndex','1')!='1' or text(c,'Offset','0')!='0':raise ValueError('暂不支持分块/跨卷 DCP 资产')
        raw=unquote(text(c,'Path')).replace('\\','/')
        if urlsplit(raw).scheme or raw.startswith('/') or ':' in raw:raise ValueError('资产路径位于 DCP 目录外')
        p=(root/raw).resolve()
        if not p.is_relative_to(root):raise ValueError('资产路径位于 DCP 目录外')
        if not p.is_file():raise ValueError('DCP 资产缺失：'+raw)
        return p
    if source.is_dir() or source.name.upper() in ('ASSETMAP','ASSETMAP.XML'):
        cpls=[]
        for p in sorted(root.iterdir()):
            if p.suffix.lower()=='.xml':
                e=xml(p)
                if name(e)=='CompositionPlaylist':cpls.append((p,e))
        if len(cpls)!=1:raise ValueError(f'找到 {len(cpls)} 个 CPL，请直接拖入要播放的 CPL XML')
        cp,cpl=cpls[0]
    else:
        cp=source; cpl=xml(cp)
        if name(cpl)!='CompositionPlaylist':raise ValueError('这不是 CPL；请拖入 DCP 文件夹或 CPL XML')
    title=text(cpl,'ContentTitleText',cp.stem)
    rl=child(cpl,'ReelList')
    if rl is None or not len(rl):raise ValueError('CPL 没有 Reel')
    configs={e.text.strip() for e in cpl.iter() if name(e)=='MainSoundConfiguration' and e.text}
    if len(configs)>1:raise ValueError('分卷声道配置不同，暂不支持')
    config=next(iter(configs),''); pan=None
    if config:
        labels=config.split('/',1)[-1].split(',')
        maps={'L':'FL','R':'FR','C':'FC','LFE':'LFE','Ls':'SL','Rs':'SR','Lss':'SL','Rss':'SR','Lrs':'BL','Rrs':'BR'}
        active=[(maps.get(v),i) for i,v in enumerate(labels) if v!='-']
        if any(v is None for v,i in active):raise ValueError('暂不支持此 DCP 声道配置：'+config)
        layouts={frozenset(['FL','FR']):'stereo',frozenset(['FC']):'mono',frozenset(['FL','FR','FC','LFE','SL','SR']):'5.1(side)',frozenset(maps.values()):'7.1'}
        layout=layouts.get(frozenset(v for v,i in active))
        if not layout or len(set(v for v,i in active))!=len(active):raise ValueError('暂不支持此 DCP 声道配置：'+config)
        pan='pan='+layout+'|'+ '|'.join(f'{v}=c{i}' for v,i in active)
    reels=[]; chapters=[]; total=Fraction(0); warnings=[]; video=[]; audio=[]
    def track(e):
        if child(e,'KeyId') is not None:raise ValueError('这是加密 DCP，需要授权 KDM 播放器；未读取加密媒体')
        rate=Fraction(*map(int,text(e,'EditRate').split()))
        intrinsic=int(text(e,'IntrinsicDuration')); entry=int(text(e,'EntryPoint','0')); duration=int(text(e,'Duration',str(intrinsic-entry)))
        if rate<=0 or entry<0 or duration<=0 or entry+duration>intrinsic:raise ValueError('CPL EntryPoint/Duration 超出资产范围')
        p=resolve(text(e,'Id')); start=entry/rate; length=duration/rate
        return {'path':p.as_posix(),'start':float(start),'duration':float(length),'edit_rate':str(rate),'entry_point':entry,'frames':duration},length,quote(p.as_posix())+','+seconds(start)+','+seconds(length)
    for n,reel in enumerate(rl,1):
        a=child(reel,'AssetList')
        if a is None:raise ValueError('Reel 缺少 AssetList')
        pic=child(a,'MainPicture'); snd=child(a,'MainSound')
        if pic is None:raise ValueError('只支持二维 MainPicture；没有画面或这是立体 DCP')
        p,pdur,pline=track(pic); row={'picture':p,'sound':None}; video.append(pline)
        if snd is not None:
            s,sdur,sline=track(snd)
            if sdur!=pdur:raise ValueError('CPL 画面和声音时长不一致')
            row['sound']=s; audio.append(sline)
        if any(name(e) in ('MainSubtitle','MainClosedCaption','ClosedCaption') for e in a):warnings.append('DCP XML/字幕 MXF 尚未加载；当前播放画面和声音')
        chapters.append({'time':float(total),'title':f'Reel {n}'})
        reels.append(row); total+=pdur
    if audio and len(audio)!=len(video):raise ValueError('部分 Reel 缺少声音，不能保证对齐')
    edl='edl://'+';'.join(video)
    if audio:edl+=';!new_stream;'+';'.join(audio)
    return dict(title=title,cpl=str(cp),duration=float(total),reels=reels,chapters=chapters,edl=edl,audio_pan=pan if audio else None,audio_configuration=config,warnings=sorted(set(warnings)))
if __name__=='__main__':
    try:result={'ok':True,**prepare(sys.argv[1])}
    except (ValueError,OSError,ET.ParseError,ZeroDivisionError) as e:result={'ok':False,'error':str(e)}
    print(json.dumps(result,ensure_ascii=True))
