-- Truthful UI for the in-window Windows frame processor. Not an HDR-output claim.
local mp=require 'mp'
local utils=require 'mp.utils'
-- Prefer the installed official Windows Dolby processor only when both its
-- manifest and this bridge support the profile. P9/AVC and P10/AV1 stay on the
-- Advanced gpu-next route, and the menu can release the MFT for comparison.
local options={auto_enable=true,gpu_float=true,fel_experiment=true}
require('mp.options').read_options(options,'native_dv_controls')
local source=debug.getinfo(1,'S').source:sub(2)
local directory=source:match('^(.*)[/\\]')
local function norm(p) return tostring(p):gsub('\\','/'):gsub('/+$',''):lower() end
local function executable_directory()
 local ok,ffi=pcall(require,'ffi');if not ok then return '' end
 pcall(ffi.cdef,[[unsigned long __stdcall GetModuleFileNameW(void*, uint16_t*, unsigned long);
 int __stdcall WideCharToMultiByte(unsigned int,unsigned long,const uint16_t*,int,char*,int,void*,void*);]])
 local k=ffi.load('kernel32');local w=ffi.new('uint16_t[32768]')
 local n=tonumber(k.GetModuleFileNameW(nil,w,32768));if n==0 or n>=32768 then return '' end
 local b=ffi.new('char[131072]');local m=k.WideCharToMultiByte(65001,0,w,n,b,131072,nil,nil)
 if m<=0 then return '' end
 return ffi.string(b,m):match('^(.*)[/\\]') or ''
end
local exe_directory=executable_directory()
local parent=directory:match('^(.*)[/\\]')
local promoted=parent and norm(exe_directory)==norm(parent)
    and mp.get_property_native('option-info/image-subs-use-margins')~=nil
local core_available=norm(exe_directory)==norm(directory) or promoted==true
local dependency={installed=false,state="checking"}
local preload_waiters={}
local set_enabled
local legacy_manual=false
mp.register_event('start-file',function() legacy_manual=false end)
local function current_display_name()
 local names=mp.get_property_native('display-names',{})
 return type(names)=='table' and tostring(names[1] or '') or ''
end
local function profile_string(profile)
 return ({[5]='dvhe.05',[7]='dvhe.07',[8]='dvhe.08',[9]='dvav.09',[10]='dav1.10',[20]='dvh1.20'})[profile] or ''
end
local function source_profile()
 for _,track in ipairs(mp.get_property_native('track-list',{})) do
  if track.type=='video' and track.selected and track['dolby-vision-profile'] then
   return tonumber(track['dolby-vision-profile']) or 0
  end
 end
 return 0
end
local function renderer_declares(name)
 local declared=dependency.renderer_profiles
 if type(declared)~='table' or #declared==0 then return true end
 for _,profile in ipairs(declared) do if profile==name then return true end end
 return false
end
local function renderer_profile_for_source(profile)
 if profile==5 then return 'dvhe.05' end
 -- This bridge deliberately normalizes HEVC P7 to the installed P8 entry.
 if profile==7 or profile==8 then return 'dvhe.08' end
 if profile==9 then return 'dvav.09' end
 if profile==10 then return 'dav1.10' end
 if profile==20 then return 'dvh1.20' end
 return ''
end
local function automatic_mft_route()
 local profile=source_profile()
 if profile==5 then return renderer_declares('dvhe.05') end
 -- P7 MEL/FEL is normalized to the declared P8 renderer boundary in this bridge.
 if profile==7 or profile==8 then return renderer_declares('dvhe.08') end
 -- The current bridge transports HEVC NAL62 metadata. P9 needs an AVC path and
 -- P10 uses AV1 EMDF, so both stay on FFmpeg/libplacebo even when declared.
 if profile==9 or profile==10 or profile==20 then return false end
 return true -- Unknown during an idle/legacy preload; runtime validation still fails closed.
end
-- Attach the enabled filter before the playback clock starts, not during its
-- first frames. The official package probe stays asynchronous and read-only.
mp.add_hook('on_preloaded',50,function(hook)
 if options.auto_enable and dependency.state=='checking' then
  hook:defer();preload_waiters[#preload_waiters+1]=hook
 elseif options.auto_enable and not legacy_manual and set_enabled then
  set_enabled(automatic_mft_route())
 end
end)
local function resume_preload()
 if options.auto_enable and not legacy_manual and set_enabled then set_enabled(automatic_mft_route()) end
 local pending=preload_waiters;preload_waiters={}
 for _,hook in ipairs(pending) do hook:cont() end
end
local policy=dofile(directory.."/dependency-policy.lua")
local bridge_policy=dofile(directory..'/dolby-sdr-policy.lua')
local available=false
local bridge_requested=false
local bridge_readiness='checking'
local windows_wanted=false
local reconcile
local dll=directory:gsub('\\','/')..'/windows-dv-frames.dll'
local label='windows_dv'
local status={};local last_json='';local known_hlg=false
mp.register_event('start-file',function()known_hlg=false end)
local function actual_route()
 for _,f in ipairs(mp.get_property_native('vf',{})) do
  if f.label==label and f.enabled~=false then
   return (f.params or {})['realtime-p81']=='yes' and 'bridge' or 'windows'
  end
 end
end
local function bridge_source()
 return bridge_policy.source(mp.get_property_native('video-params',{}),source_profile(),
   not mp.get_property_bool('idle-active',true))
end
local function update()
 local adapter=utils.file_info(directory..'/windows-dv-frames.dll')~=nil and utils.file_info(directory..'/mpv_windows_dv_rpu.dll')~=nil
 available=core_available and policy.windows(dependency,adapter).available and dependency.screen_count==1 and dependency.output_icc~=nil and dependency.output_icc~=""
 bridge_readiness=bridge_policy.readiness(dependency,core_available and adapter and utils.file_info(directory..'/libdovi.dll')~=nil)
 local route=actual_route()
 local on=route~=nil
 local bridge_on=route=='bridge'
 local meta=on and mp.get_property_native('vf-metadata/'..label,{}) or {}
 if mp.get_property_bool('idle-active',false) then meta={} end
 local src=mp.get_property_native('video-params',{})
 local out=mp.get_property_native('video-out-params',{})
 local seen=tonumber(meta['rpu-seen']) or 0
 local frames=tonumber(meta['processed-frames']) or 0
 local generated_p81=tonumber(meta['rtpq-generated']) or 0
 local generated_p84=tonumber(meta['rt84-generated']) or 0
 local generated=generated_p81+generated_p84
 local dv=(seen>0 and not bridge_on) or src.colormatrix=='dolbyvision' or out.colormatrix=='dolbyvision'
 local gpu_output=out.pixelformat=='d3d11' and (out['hw-pixelformat']=='bgra' or out['hw-pixelformat']=='rgbaf16' or out['hw-pixelformat']=='x2bgr10') and meta['frame-transport']=='d3d11-gpu' and (tonumber(meta['gpu-frames']) or 0)>0
 local processed=on and frames>0 and (out.pixelformat=='bgra' or gpu_output) and (meta.state=='processed' or meta.state=='processing')
 local bridge_processed=bridge_on and processed and generated>0 and
  (meta['fallback-reason'] or '')=='' and
  (meta['output-contract']=='windows-sdr10-fp16-transport' or meta['output-contract']=='windows-sdr10-rgb10-transport')
 local state=not on and 'off' or (meta.state or 'waiting')
 local hlg=meta['source-base-transfer']=='hlg'
 if meta['source-base-transfer'] and meta['source-base-transfer']~='other-or-unknown' then known_hlg=hlg end
 local text,kind
 local profile=source_profile()
 local source_tag=profile_string(profile)
 local renderer_tag=renderer_profile_for_source(profile)
 local renderer_declared=renderer_tag~='' and renderer_declares(renderer_tag) or false
 if mp.get_property_native('user-data/dcp',{}).active==true then text='DCP XYZ | gpu-next → SDR';kind='dcp-xyz';state='disabled-for-dcp'
 elseif bridge_on then
  if bridge_processed then text='Dolby SDR Bridge | '..(hlg and 'P8.4' or 'P8.1')..' → SDR';kind='bridge-sdr'
  elseif state=='fallback' then text='Dolby SDR Bridge | 已回退至 gpu-next';kind='fallback'
  else text='Dolby SDR Bridge | 等待已处理帧';kind='pending' end
 elseif on and state=='fel-gpu-next' then text='DV FEL | gpu-next → SDR';kind='gpu-next-fel'
 elseif processed and meta['rpu-display-contribution']=='off' then text='Windows HDR → SDR | RPU 显示作用关闭';kind='windows-base-sdr'
 elseif processed then text=(hlg and 'DV/HLG' or 'DV')..' | Windows处理 → SDR';kind='windows-sdr'
 elseif on and dv and hlg and state=='fallback' then text='DV/HLG | gpu-next → SDR';kind='gpu-next-hlg'
 elseif on and state=='fallback' then text=(dv and 'DV | ' or '')..'已回退至原映射';kind='fallback'
 elseif on and dv then text='DV | Windows准备中';kind='pending'
 elseif dv then text='DV | gpu-next → SDR';kind='gpu-next'
 elseif available and on then text='Windows DV实验 | 等待DV帧';kind='waiting'
 elseif available then text='Windows DV实验 | 未启用';kind='off'
 else text='';kind='none' end
 status={dependency=dependency,executable_directory=exe_directory,available=available,enabled=on,state=state,source_dv=dv,windows_processed=processed,
  source_profile=profile,source_profile_string=source_tag,renderer_profile_requested=renderer_tag,
  renderer_declared=renderer_declared,selected_route=bridge_on and 'dolby-sdr-bridge' or on and 'windows-dolby-mft' or (profile>0 and 'advanced-gpu-next' or 'undecided'),
  open_route_supported=profile==5 or profile==7 or profile==8 or profile==9 or profile==10,
  p5_native_sdr=processed and meta['source-profile']=='5' and (meta['output-contract']=='windows-sdr10-rgb10-transport' or meta['output-contract']=='windows-sdr10-fp16-transport'),
  p8_native_sdr=processed and meta['source-profile']=='8' and (meta['output-contract']=='windows-sdr10-rgb10-transport' or meta['output-contract']=='windows-sdr10-fp16-transport'),
  native_sdr_output=processed and (meta['output-contract']=='windows-sdr10-rgb10-transport' or meta['output-contract']=='windows-sdr10-fp16-transport'),
  relative_srgb_output=processed and (meta['output-contract']=='display-relative-srgb-fp16' or meta['output-contract']=='windows-sdr10-rgb10-transport' or meta['output-contract']=='windows-sdr10-fp16-transport'),
  float_output=processed and out['hw-pixelformat']=='rgbaf16',processed_frames=frames,rpu_seen=seen,paired_el=meta['paired-el']=='yes',
  source_base_transfer=meta['source-base-transfer'] or '',
  windows_effect=meta['rpu-display-contribution']=='off' and 'Windows HDR video processor' or 'Dolby RendererEffect',
  el_contribution=meta['el-contribution'],rpu_display_contribution=meta['rpu-display-contribution'],rpu_forwarded=tonumber(meta['rpu-forwarded']) or 0,
  fallback_reason=meta['fallback-reason'] or '',badge=text,badge_kind=kind,
  physical_dv_hdr_output=false,output='SDR',experimental=true,
  bridge={requested=bridge_requested,available=bridge_readiness=='ready',readiness=bridge_readiness,
    attached=bridge_on,processing=bridge_processed,
    generated_p81=bridge_on and generated_p81 or 0,generated_p84=bridge_on and generated_p84 or 0,
    processed_frames=bridge_on and frames or 0,source=bridge_source() or '',
    state=bridge_on and state or bridge_requested and 'waiting-for-compatible-input' or 'off'}}
 local json=utils.format_json(status)
 if json~=last_json then mp.set_property_native('user-data/windows-dv-inwindow',status);last_json=json end

end
reconcile=function(refresh_bridge)
 local previous=actual_route()
 local comparison=mp.get_property_native('user-data/dv-playback',{})
 local dcp=mp.get_property_native('user-data/dcp',{}).active==true or mp.get_property('video-params/colormatrix')=='xyz'
 local blocked=dcp or (not previous and (comparison.el_disabled or comparison.rpu_disabled))
 local wanted=bridge_policy.route(bridge_requested,bridge_readiness,bridge_source(),windows_wanted,available,blocked)
 if previous==wanted then update();return true end
 local refresh=(refresh_bridge or wanted=='windows' and not previous and known_hlg) and mp.get_property_bool('pause',false)
 local at=refresh and mp.get_property_number('time-pos')
 local ok,err=true,nil
 if wanted then
  local args=wanted=='bridge' and ':output-icc=none:gpu-float=yes:realtime-p81=yes:realtime-p84=yes' or
   (':output-icc=%'..#dependency.output_icc..'%'..dependency.output_icc..
    (options.gpu_float and ':gpu-float=yes' or '')..(options.fel_experiment and ':fel-experiment=yes' or ''))
  -- mpv replaces an existing matching vf label atomically, preserving the rest of the chain.
  ok,err=mp.commandv('vf','add','@'..label..':windows-dv=dll=%'..#dll..'%'..dll..args)
 elseif previous then ok,err=mp.commandv('vf','remove','@'..label) end
 if ok and at then mp.commandv('seek',at,'absolute+exact') end
 if not ok then mp.osd_message('Windows处理切换失败：'..tostring(err),4) end
 update()
 return ok
end
set_enabled=function(wanted,manual)
 local bridge_was=bridge_requested
 if manual then bridge_requested=false end
 if wanted and manual and not available then
  bridge_requested=bridge_was
  mp.osd_message('Windows Dolby 未就绪，请重新检测系统依赖。',4);return
 end
 if wanted and manual and not actual_route() then
  local comparison=mp.get_property_native('user-data/dv-playback',{})
  if comparison.el_disabled or comparison.rpu_disabled then
   bridge_requested=bridge_was
   mp.osd_message('请先恢复 EL／RPU，再启用 Windows Dolby。',3);return
  end
 end
 local windows_was=windows_wanted
 windows_wanted=wanted
 if reconcile(manual and bridge_was) then
  if manual then legacy_manual=true end
 else
  bridge_requested=bridge_was;windows_wanted=windows_was;update()
 end
end
local function bridge_toggle()
 if not bridge_requested and bridge_readiness~='ready' then update();return end
 bridge_requested=not bridge_requested
 if reconcile(true)==false then bridge_requested=not bridge_requested;update() end
end
local function toggle() set_enabled(actual_route()~='windows',true) end
mp.add_key_binding('Ctrl+Alt+j','toggle-windows-dv-inwindow',toggle)
mp.register_script_message('windows-dv-inwindow-toggle',toggle)
mp.register_script_message('windows-dv-inwindow-enable',function()set_enabled(true,true)end)
mp.register_script_message('windows-dv-inwindow-disable',function()set_enabled(false,true)end)
mp.register_script_message('dolby-sdr-bridge-toggle',bridge_toggle)
mp.register_script_message('windows-dv-inwindow-status',update)
mp.observe_property('video-out-params','native',update)
mp.observe_property('track-list','native',update)
mp.observe_property('vf','native',update)
mp.observe_property('user-data/dcp','native',update)
mp.observe_property('osd-dimensions','native',update)
local function refresh_armed()
 if bridge_requested then reconcile(false) end
end
mp.observe_property('video-params','native',refresh_armed)
mp.observe_property('track-list','native',refresh_armed)
mp.observe_property('user-data/dcp','native',refresh_armed)
mp.register_event('end-file',refresh_armed)
mp.add_periodic_timer(.25,update)
-- The experimental executable is a clearly separated opt-in entry.
-- The main executable loads diagnostics only; it never gains this filter implicitly.
local dependency_generation=0
local function detect_dependencies()
 dependency_generation=dependency_generation+1
 local generation=dependency_generation
 dependency={installed=false,state='checking'};update()
 local system=os.getenv('SystemRoot') or 'C:/Windows'
 local args={system..'/System32/WindowsPowerShell/v1.0/powershell.exe','-NoProfile','-NonInteractive','-File',directory..'/Detect-Dependencies.ps1'}
 local display=current_display_name()
 if display~='' then args[#args+1]='-DisplayName';args[#args+1]=display end
 mp.command_native_async({name='subprocess',playback_only=false,capture_stdout=true,capture_stderr=true,args=args},
  function(ok,result)
   if generation~=dependency_generation then return end
   local parsed=ok and result and result.status==0 and utils.parse_json(result.stdout or '')
   dependency=type(parsed)=='table' and parsed or {installed=false,state='probe-failed'}
   update()
   resume_preload()
   reconcile(false)
  end)
end
mp.register_script_message('windows-dv-dependencies-refresh',detect_dependencies)
local last_display=false
local display_timer
mp.observe_property('display-names','native',function()
 local current=current_display_name()
 if last_display==false or current~=last_display then
  last_display=current
  if display_timer then display_timer:kill() end
  display_timer=mp.add_timeout(.10,function()display_timer=nil;detect_dependencies()end)
 end
end)
mp.add_timeout(.05,function()
 if last_display==false then last_display=current_display_name();detect_dependencies() end
end)
update()
