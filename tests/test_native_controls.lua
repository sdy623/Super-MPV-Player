local target=arg[1];local directory=target:match('^(.*)[/\\]');local messages,timers,props,commands={},{},{},{}
local reply,parsed,settings; local hooks,observers,requests,events={},{},{},{}
props['display-names']={'\\\\.\\DISPLAY7'}
mp={log=function()end,add_hook=function(k,_,f)hooks[k]=f end,get_property=function(k,d)return props[k] or d end,msg={error=function()end},get_property_native=function(k,d)return props[k] or d end,get_property_bool=function(k,d)if props[k]~=nil then return props[k] end return d end,get_property_number=function()return 0 end,
set_property_native=function(k,v)props[k]=v end,register_event=function(k,f)events[k]=events[k] or {};table.insert(events[k],f)end,register_script_message=function(k,f)messages[k]=f end,
add_key_binding=function()end,observe_property=function(k,_,f)observers[k]=f end,add_periodic_timer=function()end,add_timeout=function(_,f)timers[#timers+1]=f;return {kill=function()end} end,
osd_message=function()end,commandv=function(...)local c={...};commands[#commands+1]=c;if c[1]=='vf' then props.vf=c[2]=='add' and {{label='windows_dv',params={['realtime-p81']=c[3]:find('realtime-p81=yes',1,true) and 'yes' or nil}}} or {} end;return true end,command_native_async=function(req,f)requests[#requests+1]=req;reply=f end}
package.preload['mp']=function()return mp end
package.preload['mp.options']=function()return {read_options=function(o)settings=o end}end
package.preload['mp.utils']=function()return {file_info=function()return {is_file=true}end,format_json=function(t)return tostring(t)end,parse_json=function()return parsed end}end
package.preload['ffi']=function()return {cdef=function()end,new=function()return {}end,string=function()return directory..'/mpv.exe' end,load=function()return {GetModuleFileNameW=function()return 1 end,WideCharToMultiByte=function()return 1 end}end}end
package.loaded['mp']=nil;package.loaded['mp.options']=nil;package.loaded['mp.utils']=nil;package.loaded['ffi']=nil
assert(loadfile(target))()
local function start_file()for _,f in ipairs(events['start-file'] or {}) do f() end end
assert(settings.auto_enable==true,'manifest-supported DV must prefer the installed Windows Dolby MFT')
assert(settings.fel_experiment==true,'P7 FEL must try the validated BL+EL Windows path by default and fall back only when the helper is unavailable')
local function probe(d)messages['windows-dv-dependencies-refresh']();parsed=d;reply(true,{status=0,stdout='x'})end
probe({installed=false,state='missing'});messages['windows-dv-inwindow-enable']();assert(#commands==0)
local args=requests[#requests].args;assert(args[#args-1]=='-DisplayName' and args[#args]=='\\\\.\\DISPLAY7','probe must target the display currently containing the mpv window')
probe({installed=true,screen_count=2,output_icc='C:/Windows/profile.icm'});assert(not props['user-data/windows-dv-inwindow'].available)
probe({installed=true,screen_count=1,output_icc=''});assert(not props['user-data/windows-dv-inwindow'].available)
probe({installed=true,screen_count=1,output_icc='C:/Windows/profile.icm'});assert(props['user-data/windows-dv-inwindow'].available)
assert(not props['user-data/windows-dv-inwindow'].windows_processed)
messages['windows-dv-inwindow-enable']();assert(commands[1][3]:find('output-icc=%'..#parsed.output_icc..'%'..parsed.output_icc,1,true))
assert(commands[1][3]:find(':fel-experiment=yes',1,true),'default P7 route must enable the guarded FEL helper')
props['video-out-params']={pixelformat='d3d11',['hw-pixelformat']='bgra'}
props['vf-metadata/windows_dv']={state='processed',['processed-frames']='120',['rpu-seen']='120',['frame-transport']='d3d11-gpu',['gpu-frames']='120',['source-base-transfer']='hlg'}
messages['windows-dv-inwindow-status']();assert(props['user-data/windows-dv-inwindow'].windows_processed,'GPU BGRA must count as actual Windows processing')
props['vf-metadata/windows_dv']['gpu-frames']='0';messages['windows-dv-inwindow-status']();assert(not props['user-data/windows-dv-inwindow'].windows_processed,'hardware format alone is not proof')
props['video-out-params']={pixelformat='bgra'};messages['windows-dv-inwindow-status']();assert(props['user-data/windows-dv-inwindow'].windows_processed,'CPU/P7 status must remain valid')
props['video-out-params']={pixelformat='d3d11',['hw-pixelformat']='rgbaf16'}
props['vf-metadata/windows_dv']['gpu-frames']='120';messages['windows-dv-inwindow-status']()
assert(props['user-data/windows-dv-inwindow'].windows_processed and props['user-data/windows-dv-inwindow'].float_output,'FP16 must be recognized without weakening dependency checks')
props['video-out-params']['hw-pixelformat']='rgbaf16'
props['vf-metadata/windows_dv']['output-contract']='windows-sdr10-fp16-transport'
props['vf-metadata/windows_dv']['source-profile']='8';messages['windows-dv-inwindow-status']()
assert(props['user-data/windows-dv-inwindow'].p8_native_sdr and props['user-data/windows-dv-inwindow'].native_sdr_output)
assert(props['user-data/windows-dv-inwindow'].float_output,'accepted native SDR10 codes must use the 50e88c0 FP16 presentation container')
assert(not props['user-data/windows-dv-inwindow'].p5_native_sdr)
props['vf-metadata/windows_dv']['source-profile']='5';messages['windows-dv-inwindow-status']()
assert(props['user-data/windows-dv-inwindow'].p5_native_sdr and not props['user-data/windows-dv-inwindow'].p8_native_sdr)
props['vf-metadata/windows_dv']['output-contract']='legacy-sdr';messages['windows-dv-inwindow-status']()
assert(not props['user-data/windows-dv-inwindow'].native_sdr_output)
messages['windows-dv-inwindow-disable']();props['user-data/dcp']={active=true};local before=#commands
messages['windows-dv-inwindow-enable']();assert(#commands==before,'DCP must not enter Dolby bridge')
assert(props['user-data/windows-dv-inwindow'].state=='disabled-for-dcp')
props['user-data/dcp']={};messages['windows-dv-inwindow-enable']()
messages['windows-dv-dependencies-refresh']();reply(false,nil)
assert(not props['user-data/windows-dv-inwindow'].available);assert(#props.vf==0,'failed probe must remove active filter')
print('PASS missing, multi-display, missing profile, explicit ICC, no false processing claim, fail-closed filter removal')

assert(hooks.on_preloaded, 'startup must wait for official dependency probe before first video frames')
settings.auto_enable=true
messages['windows-dv-dependencies-refresh']()
local deferred,continued=false,false
hooks.on_preloaded({defer=function()deferred=true end,cont=function()continued=true end})
assert(deferred and not continued)
parsed={installed=true,screen_count=1,output_icc='C:/Windows/profile.icm'};reply(true,{status=0,stdout='x'})
assert(continued and #props.vf==1,'filter must be attached before releasing preload')
messages['windows-dv-dependencies-refresh']();continued=false
hooks.on_preloaded({defer=function()end,cont=function()continued=true end});reply(false,nil)
assert(continued and #props.vf==0,'failed dependencies must release preload on original path')
print('PASS preload probe completion and missing-dependency recovery')

probe({installed=true,screen_count=1,output_icc='C:/Windows/profile.icm'})
messages['windows-dv-inwindow-disable']()
props['user-data/dv-playback']={el_disabled=true}
messages['windows-dv-inwindow-enable']()
assert(#props.vf==0,'EL-off must not silently start incomplete Windows FEL')
print('PASS Windows entry rejects intentionally disabled EL')

props['user-data/dv-playback']={}
start_file()
props['track-list']={{type='video',selected=true,['dolby-vision-profile']=9}}
probe({installed=true,screen_count=1,output_icc='C:/Windows/profile.icm',renderer_profiles={'dvhe.05','dvhe.08','dvav.09'}})
local route=props['user-data/windows-dv-inwindow']
assert(route.source_profile==9 and route.source_profile_string=='dvav.09' and route.renderer_declared==true)
assert(route.selected_route=='advanced-gpu-next','P9 must be captured by Advanced until the AVC bridge exists')
assert(#props.vf==0,'P9 must not enter the HEVC-only MFT bridge')
props['track-list'][1]['dolby-vision-profile']=10
hooks.on_preloaded({defer=function()end,cont=function()end})
route=props['user-data/windows-dv-inwindow']
assert(route.source_profile_string=='dav1.10' and route.selected_route=='advanced-gpu-next')
assert(#props.vf==0,'P10 must stay on gpu-next when the installed Dolby renderer does not declare dav1.10')
props['track-list'][1]['dolby-vision-profile']=8
hooks.on_preloaded({defer=function()end,cont=function()end})
assert(#props.vf==1,'P8 must still auto-route through a declared dvhe.08 renderer')
print('PASS manifest profile identity is captured while unsupported P9/P10 stay on Advanced')

props['display-names']={'\\\\.\\DISPLAY1'};messages['windows-dv-dependencies-refresh']();local stale=reply
props['display-names']={'\\\\.\\DISPLAY2'};observers['display-names']();timers[#timers]();local current=reply
parsed={installed=true,screen_count=2,output_icc='display2.icm',display_name='\\\\.\\DISPLAY2'};current(true,{status=0,stdout='x'})
parsed={installed=true,screen_count=2,output_icc='display1.icm',display_name='\\\\.\\DISPLAY1'};stale(true,{status=0,stdout='x'})
assert(props['user-data/windows-dv-inwindow'].dependency.display_name=='\\\\.\\DISPLAY2',
 'late probe result from the old display must not replace the current display target')
print('PASS display switch ignores stale ICC/EDID probe results')

local function status() return props['user-data/windows-dv-inwindow'] end
local function bridge() return status().bridge end
local function toggle_bridge() messages['dolby-sdr-bridge-toggle']() end
local ready={installed=true,screen_count=1,output_icc='test.icm',renderer_profiles={'dvhe.08'},
    display_color_mode='sdr',display_hdr_active=false}
start_file()
props['idle-active']=false
props['track-list']={{type='video',selected=true}}
props['video-params']={gamma='pq',colormatrix='bt.2020-ncl'}
props['vf-metadata/windows_dv']={}
probe(ready)
assert(bridge().available and not bridge().requested and not bridge().attached)
toggle_bridge()
assert(bridge().requested and bridge().attached and not bridge().processing)
assert(#props.vf==1 and commands[#commands][3]:find('realtime-p84=yes',1,true))
props['vf-metadata/windows_dv']={state='processed',['processed-frames']='7',['rpu-seen']='7',
 ['rtpq-generated']='7',['output-contract']='windows-sdr10-fp16-transport'}
props['video-out-params']={pixelformat='bgra'}
messages['windows-dv-inwindow-status']()
assert(bridge().processing and bridge().generated_p81==7 and not status().source_dv)
props['vf-metadata/windows_dv'].state='fallback';messages['windows-dv-inwindow-status']()
assert(not bridge().processing and bridge().state=='fallback')
toggle_bridge()
assert(not bridge().requested and not bridge().attached and status().selected_route=='windows-dolby-mft')

props['track-list'][1]['dolby-vision-profile']=8
toggle_bridge()
assert(bridge().requested and not bridge().attached and #props.vf==1)
messages['windows-dv-inwindow-disable']()
assert(not bridge().requested and #props.vf==0)
probe(ready)
assert(#props.vf==0,'refresh must not undo a manual route selection within the same file')
ready.display_hdr_active=true;probe(ready)
assert(not bridge().available)
toggle_bridge()
assert(not bridge().requested and #props.vf==0)
print('PASS opt-in bridge, single filter, real processing vs fallback, original RPU and HDR gate')
