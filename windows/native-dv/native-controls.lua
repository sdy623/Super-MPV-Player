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
 elseif options.auto_enable and set_enabled then
  set_enabled(automatic_mft_route())
 end
end)
local function resume_preload()
 if options.auto_enable and set_enabled then set_enabled(automatic_mft_route()) end
 local pending=preload_waiters;preload_waiters={}
 for _,hook in ipairs(pending) do hook:cont() end
end
local policy=dofile(directory.."/dependency-policy.lua")
local available=false
local dll=directory:gsub('\\','/')..'/windows-dv-frames.dll'
local label='windows_dv'
local status={};local last_json='';local known_hlg=false
mp.register_event('start-file',function()known_hlg=false end)
local function enabled()
 for _,f in ipairs(mp.get_property_native('vf',{})) do if f.label==label then return true end end
 return false
end
local function update()
 local adapter=utils.file_info(directory..'/windows-dv-frames.dll')~=nil and utils.file_info(directory..'/mpv_windows_dv_rpu.dll')~=nil
 available=core_available and policy.windows(dependency,adapter).available and dependency.screen_count==1 and dependency.output_icc~=nil and dependency.output_icc~=""
 local on=enabled()
 local meta=on and mp.get_property_native('vf-metadata/'..label,{}) or {}
 if mp.get_property_bool('idle-active',false) then meta={} end
 local src=mp.get_property_native('video-params',{})
 local out=mp.get_property_native('video-out-params',{})
 local seen=tonumber(meta['rpu-seen']) or 0
 local frames=tonumber(meta['processed-frames']) or 0
 local dv=seen>0 or src.colormatrix=='dolbyvision' or out.colormatrix=='dolbyvision'
 local gpu_output=out.pixelformat=='d3d11' and (out['hw-pixelformat']=='bgra' or out['hw-pixelformat']=='rgbaf16' or out['hw-pixelformat']=='x2bgr10') and meta['frame-transport']=='d3d11-gpu' and (tonumber(meta['gpu-frames']) or 0)>0
 local processed=on and frames>0 and (out.pixelformat=='bgra' or gpu_output) and (meta.state=='processed' or meta.state=='processing')
 local state=not on and 'off' or (meta.state or 'waiting')
 local hlg=meta['source-base-transfer']=='hlg'
 if meta['source-base-transfer'] and meta['source-base-transfer']~='other-or-unknown' then known_hlg=hlg end
 local text,kind
 local profile=source_profile()
 local source_tag=profile_string(profile)
 local renderer_tag=renderer_profile_for_source(profile)
 local renderer_declared=renderer_tag~='' and renderer_declares(renderer_tag) or false
 if mp.get_property_native('user-data/dcp',{}).active==true then text='DCP XYZ | gpu-next → SDR';kind='dcp-xyz';state='disabled-for-dcp'
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
  renderer_declared=renderer_declared,selected_route=on and 'windows-dolby-mft' or (profile>0 and 'advanced-gpu-next' or 'undecided'),
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
  physical_dv_hdr_output=false,output='SDR',experimental=true}
 local json=utils.format_json(status)
 if json~=last_json then mp.set_property_native('user-data/windows-dv-inwindow',status);last_json=json end

end
set_enabled=function(wanted)
 if wanted and not enabled() and (mp.get_property_native('user-data/dv-playback',{}).el_disabled or mp.get_property_native('user-data/dv-playback',{}).rpu_disabled) then
  mp.osd_message('请先恢复 EL／RPU，再启用 Windows 杜比处理',3);return
 end
 if wanted and (mp.get_property_native('user-data/dcp',{}).active==true or mp.get_property('video-params/colormatrix')=='xyz') then update();return end
 if wanted and not available then mp.osd_message('Windows DV 依赖尚未就绪：'..tostring(dependency.state)..'；请通过 Microsoft Store 或设备厂商安装兼容扩展',4);return end
 local refresh=wanted and not enabled() and known_hlg and mp.get_property_bool('pause',false)
 local at=refresh and mp.get_property_number('time-pos')
 local ok,err=true,nil
 if wanted and not enabled() then ok,err=mp.commandv('vf','add','@'..label..':windows-dv=dll=%'..#dll..'%'..dll..(dependency.output_icc and dependency.output_icc~='' and ':output-icc=%'..#dependency.output_icc..'%'..dependency.output_icc or '')..(options.gpu_float and ':gpu-float=yes' or '')..(options.fel_experiment and ':fel-experiment=yes' or ''))
 elseif not wanted and enabled() then ok,err=mp.commandv('vf','remove','@'..label) end
 if ok and at then mp.commandv('seek',at,'absolute+exact') end
 if not ok then mp.osd_message('Windows处理切换失败：'..tostring(err),4) end
 update()
end
local function toggle() set_enabled(not enabled()) end
mp.add_key_binding('Ctrl+Alt+j','toggle-windows-dv-inwindow',toggle)
mp.register_script_message('windows-dv-inwindow-toggle',toggle)
mp.register_script_message('windows-dv-inwindow-enable',function()set_enabled(true)end)
mp.register_script_message('windows-dv-inwindow-disable',function()set_enabled(false)end)
mp.register_script_message('windows-dv-inwindow-status',update)
mp.observe_property('video-out-params','native',update)
mp.observe_property('track-list','native',update)
mp.observe_property('vf','native',update)
mp.observe_property('user-data/dcp','native',update)
mp.observe_property('osd-dimensions','native',update)
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
  if not available and enabled() then set_enabled(false) end
  if available and options.auto_enable then set_enabled(automatic_mft_route()) end
   resume_preload()
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
