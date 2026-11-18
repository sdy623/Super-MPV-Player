-- Read-only video inspector. Source, processing and screen descriptors are separate.
local mp=require 'mp'
local utils=require 'mp.utils'
local panel=mp.create_osd_overlay('ass-events')
local visible=false
local function get(k,d)local v=mp.get_property_native(k,d);return v end
local function text(v)return tostring(v==nil and '—' or v):gsub('\\','／'):gsub('{','（'):gsub('}','）'):gsub('[\r\n]',' ')end
local transfers={hlg='HLG · ARIB STD-B67',pq='PQ · SMPTE ST 2084',srgb='sRGB',linear='Linear',scrgb='scRGB · 80 nit scale',['gamma2.2']='Gamma 2.2',['ipt-pq-c2']='IPT-PQ-C2 (P5)'}
local function tr(v)return transfers[v] or text(v)end
local function num(v,n)return type(v)=='number' and string.format('%.'..(n or 2)..'f',v) or '—'end
local function dims(v)return v.w and (v.w..' × '..v.h) or '—'end
local function rate(v)return type(v)=='number' and num(v/1000000,2)..' Mbps' or '—'end
local function draw()
 if not visible then return end
 local src=get('video-params',{});local out=get('video-out-params',{});local target=get('video-target-params',{})
 local win=get('user-data/windows-dv-inwindow',{});local dv=get('user-data/dv-playback',{});local meta=get('vf-metadata/windows_dv',{})
 local vt,at={},{};for _,t in ipairs(get('track-list',{}))do if t.selected and t.type=='video' then vt=t elseif t.selected and t.type=='audio' then at=t end end
 local profile=vt['dolby-vision-profile'] or meta['source-profile'];local base=src['gamma-original'] or dv.base_transfer or src.gamma
 local matrix=src['colormatrix-original'] or (profile==5 and 'IPT-PQ-C2') or src.colormatrix
 local left={
 {'SOURCE / 源视频',true},
 {'Codec',get('video-codec')}, {'Picture',dims(src)..' · '..num(get('container-fps'),3)..' fps'},
 {'DV profile',profile and ('P'..(tonumber(profile)==8 and (base=='hlg' and '8.4' or base=='pq' and '8.1' or '8') or profile)..' · level '..text(vt['dolby-vision-level'])) or 'Not declared'},
 {'Base transfer',tr(base)}, {'Base primaries',tonumber(profile)==5 and 'P5 IPT-PQ-C2 (not HDR10 BL)' or src['primaries-original'] or src.primaries},
 {'Matrix (decoded)',matrix}, {'Range / format',text(src.colorlevels)..' / '..text(src['hw-pixelformat'] or src.pixelformat)},
 {'Layers',dv.rpu_disabled and 'BL-only comparison' or dv.dual_layer and 'BL + EL + RPU' or (profile and 'BL + RPU' or 'Single layer')},
 {'Video bitrate',rate(get('video-bitrate'))},
 {'AUDIO / 声音',true}, {'Codec',get('audio-codec-name',at.codec)},
 {'Output',get('current-ao')}, {'Channels / rate',text(get('audio-params/channel-count'))..' / '..text(get('audio-params/samplerate'))..' Hz'},
 {'Audio bitrate',rate(get('audio-bitrate'))},
 }
 local applied=(meta['el-contribution'] or (dv.el_disabled and 'off' or dv.rpu_disabled and 'off (RPU required)' or 'on'))
 local right={
 {'PROCESSING / 处理与输出',true},
 {'Backend',win.enabled and (meta['rpu-display-contribution']=='off' and 'Windows HDR → gpu-next' or 'Windows Dolby → gpu-next') or 'gpu-next / libplacebo'},
 {'Hardware decoder',get('hwdec-current')},
 {'EL picture',dv.dual_layer and applied or 'No enhancement layer'},
 {'RPU / EL path',dv.rpu_disabled and 'off · compatible BL-only' or (meta['rpu-display-contribution'] or 'on')},
 {'RPU seen / sent',text(meta['rpu-seen'])..' / '..text(meta['rpu-forwarded'])},
 {'Windows output',meta['output'] or 'Not used'},
 {'Renderer input',tr(out.gamma)..' / '..text(out.primaries)},
 {'Screen transfer',tr(target.gamma)},
 {'Screen primaries',target.primaries},
 {'MPV ICC',get('icc-profile-auto')==true and 'Auto (display profile)' or ((get('icc-profile','')~='') and 'Explicit profile' or 'Off')},
 {'Peak reference',text(get('target-peak'))..' · target '..text(target['max-luma'])..' nits'},
 {'Output surface',text(target.pixelformat)..' / '..text(target.colorlevels)},
 {'A/V sync',num(get('avsync'),4)..' s'},
 {'Dropped decode / VO',text(get('decoder-frame-drop-count'))..' / '..text(get('frame-drop-count'))},
 }
 mp.set_property_native('user-data/video-details',{source=left,processing=right})
 local a={[[{\an7\pos(0,0)\bord0\shad0\1c&H181818&\1a&H16&\p1}m 80 60 l 1200 60 l 1200 590 l 80 590{\p0}]]}
 local function line(x,y,t,size,color)
  a[#a+1]=string.format('{\\an7\\pos(%d,%d)\\fnSegoe UI\\fs%d\\bord0\\shad0\\1c&H%s&\\1a&H00&}%s',x,y,size,color or 'EEEEEE',text(t))
 end
 line(108,80,'Video information / 视频信息',26)
 line(1040,88,'Esc 关闭',17,'BBBBBB')
 local name=get('media-title','');local chars={};for c in name:gmatch('[%z\1-\127\194-\244][\128-\191]*')do chars[#chars+1]=c;if #chars>=74 then chars[#chars+1]='…';break end end
 line(108,120,table.concat(chars),18,'C7C7C7')
 for col,rows in ipairs({left,right})do
  local x=col==1 and 108 or 672
  for i,row in ipairs(rows)do
   local y=158+(i-1)*28
   if row[2]==true then line(x,y,row[1],19,'F7C46B')
   else line(x,y,row[1],17,'AAAAAA');line(x+190,y,row[2],17)end
  end
 end
 panel.res_x=1280;panel.res_y=720;panel.data=table.concat(a,'\n');panel:update()
end
local function close()visible=false;panel:remove();mp.remove_key_binding('video-details-close')end
local function toggle()
 if visible then close();return end
 visible=true;mp.add_forced_key_binding('ESC','video-details-close',close);draw()
end
local function register()
 mp.commandv('script-message-to','uosc','set-button','video_details',utils.format_json({icon='info',tooltip='视频信息 / Video information',command={'script-message','video-details-toggle'}}))
end
mp.register_script_message('video-details-toggle',toggle)
mp.register_script_message('uosc-version',register)
mp.add_key_binding('Ctrl+Alt+i','video-details',toggle)
mp.add_periodic_timer(.5,draw)
mp.register_event('end-file',close)
mp.add_timeout(.2,register)
