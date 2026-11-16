local target=arg[1];local directory=target:match('^(.*)[/\\]');local messages,timers,props,commands={},{},{},{}
local reply,parsed
mp={msg={error=function()end},get_property_native=function(k,d)return props[k] or d end,get_property_bool=function(k,d)return d end,get_property_number=function()return 0 end,
set_property_native=function(k,v)props[k]=v end,register_event=function()end,register_script_message=function(k,f)messages[k]=f end,
add_key_binding=function()end,observe_property=function()end,add_periodic_timer=function()end,add_timeout=function(_,f)timers[#timers+1]=f end,
osd_message=function()end,commandv=function(...)local c={...};commands[#commands+1]=c;if c[1]=='vf' then props.vf=c[2]=='add' and {{label='windows_dv'}} or {} end;return true end,command_native_async=function(_,f)reply=f end}
package.preload['mp']=function()return mp end
package.preload['mp.options']=function()return {read_options=function(o)o.auto_enable=false end}end
package.preload['mp.utils']=function()return {file_info=function()return {is_file=true}end,format_json=function(t)return tostring(t)end,parse_json=function()return parsed end}end
package.preload['ffi']=function()return {cdef=function()end,new=function()return {}end,string=function()return directory..'/mpv.exe' end,load=function()return {GetModuleFileNameW=function()return 1 end,WideCharToMultiByte=function()return 1 end}end}end
assert(loadfile(target))()
local function probe(d)messages['windows-dv-dependencies-refresh']();parsed=d;reply(true,{status=0,stdout='x'})end
probe({installed=false,state='missing'});messages['windows-dv-inwindow-enable']();assert(#commands==0)
probe({installed=true,screen_count=2,output_icc='C:/Windows/profile.icm'});assert(not props['user-data/windows-dv-inwindow'].available)
probe({installed=true,screen_count=1,output_icc=''});assert(not props['user-data/windows-dv-inwindow'].available)
probe({installed=true,screen_count=1,output_icc='C:/Windows/profile.icm'});assert(props['user-data/windows-dv-inwindow'].available)
assert(not props['user-data/windows-dv-inwindow'].windows_processed)
messages['windows-dv-inwindow-enable']();assert(commands[1][3]:find('output-icc=%'..#parsed.output_icc..'%'..parsed.output_icc,1,true))
props['video-out-params']={pixelformat='d3d11',['hw-pixelformat']='bgra'}
props['vf-metadata/windows_dv']={state='processed',['processed-frames']='120',['rpu-seen']='120',['frame-transport']='d3d11-gpu',['gpu-frames']='120',['source-base-transfer']='hlg'}
messages['windows-dv-inwindow-status']();assert(props['user-data/windows-dv-inwindow'].windows_processed,'GPU BGRA must count as actual Windows processing')
props['vf-metadata/windows_dv']['gpu-frames']='0';messages['windows-dv-inwindow-status']();assert(not props['user-data/windows-dv-inwindow'].windows_processed,'hardware format alone is not proof')
props['video-out-params']={pixelformat='bgra'};messages['windows-dv-inwindow-status']();assert(props['user-data/windows-dv-inwindow'].windows_processed,'CPU/P7 status must remain valid')
messages['windows-dv-dependencies-refresh']();reply(false,nil)
assert(not props['user-data/windows-dv-inwindow'].available);assert(#props.vf==0,'failed probe must remove active filter')
print('PASS missing, multi-display, missing profile, explicit ICC, no false processing claim, fail-closed filter removal')
