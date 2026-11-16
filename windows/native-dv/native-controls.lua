-- Truthful UI for the in-window Windows frame processor. Not an HDR-output claim.
local mp=require 'mp'
local utils=require 'mp.utils'
local options={auto_enable=true}
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
 available=core_available and policy.windows(dependency,adapter).available
 local on=enabled()
 local meta=on and mp.get_property_native('vf-metadata/'..label,{}) or {}
 if mp.get_property_bool('idle-active',false) then meta={} end
 local src=mp.get_property_native('video-params',{})
 local out=mp.get_property_native('video-out-params',{})
 local seen=tonumber(meta['rpu-seen']) or 0
 local frames=tonumber(meta['processed-frames']) or 0
 local dv=seen>0 or src.colormatrix=='dolbyvision' or out.colormatrix=='dolbyvision'
 local processed=on and frames>0 and out.pixelformat=='bgra' and (meta.state=='processed' or meta.state=='processing')
 local state=not on and 'off' or (meta.state or 'waiting')
 local hlg=meta['source-base-transfer']=='hlg'
 if meta['source-base-transfer'] and meta['source-base-transfer']~='other-or-unknown' then known_hlg=hlg end
 local text,kind
 if processed then text=(hlg and 'DV/HLG' or 'DV')..' | Windows处理 → SDR';kind='windows-sdr'
 elseif on and dv and hlg and state=='fallback' then text='DV/HLG | gpu-next → SDR';kind='gpu-next-hlg'
 elseif on and state=='fallback' then text=(dv and 'DV | ' or '')..'已回退至原映射';kind='fallback'
 elseif on and dv then text='DV | Windows准备中';kind='pending'
 elseif dv then text='DV | gpu-next → SDR';kind='gpu-next'
 elseif available and on then text='Windows DV实验 | 等待DV帧';kind='waiting'
 elseif available then text='Windows DV实验 | 未启用';kind='off'
 else text='';kind='none' end
 status={dependency=dependency,executable_directory=exe_directory,available=available,enabled=on,state=state,source_dv=dv,windows_processed=processed,
  processed_frames=frames,rpu_seen=seen,paired_el=meta['paired-el']=='yes',
  source_base_transfer=meta['source-base-transfer'] or '',
  fallback_reason=meta['fallback-reason'] or '',badge=text,badge_kind=kind,
  physical_dv_hdr_output=false,output='SDR',experimental=true}
 local json=utils.format_json(status)
 if json~=last_json then mp.set_property_native('user-data/windows-dv-inwindow',status);last_json=json end

end
local function set_enabled(wanted)
 if wanted and not available then mp.osd_message('Windows DV 依赖尚未就绪：'..tostring(dependency.state)..'；请通过 Microsoft Store 或设备厂商安装兼容扩展',4);return end
 local refresh=wanted and not enabled() and known_hlg and mp.get_property_bool('pause',false)
 local at=refresh and mp.get_property_number('time-pos')
 local ok,err=true,nil
 if wanted and not enabled() then ok,err=mp.commandv('vf','add','@'..label..':windows-dv=dll=%'..#dll..'%'..dll)
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
mp.observe_property('vf','native',update)
mp.observe_property('osd-dimensions','native',update)
mp.add_periodic_timer(.25,update)
-- The experimental executable is a clearly separated opt-in entry.
-- The main executable loads diagnostics only; it never gains this filter implicitly.
local function detect_dependencies()
 dependency={installed=false,state='checking'};update()
 local system=os.getenv('SystemRoot') or 'C:/Windows'
 mp.command_native_async({name='subprocess',playback_only=false,capture_stdout=true,capture_stderr=true,
  args={system..'/System32/WindowsPowerShell/v1.0/powershell.exe','-NoProfile','-NonInteractive','-File',directory..'/Detect-Dependencies.ps1'}},
  function(ok,result)
   local parsed=ok and result and result.status==0 and utils.parse_json(result.stdout or '')
   dependency=type(parsed)=='table' and parsed or {installed=false,state='probe-failed'}
   update()
   if available and options.auto_enable then set_enabled(true) end
  end)
end
mp.register_script_message('windows-dv-dependencies-refresh',detect_dependencies)
mp.add_timeout(.05,detect_dependencies)
update()
