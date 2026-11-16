local target=arg[1];local directory=target:match('^(.*)[/\\]');local messages,timers,props,commands={},{},{},{}
local reply,parsed
mp={msg={error=function()end},get_property_native=function(k,d)return props[k] or d end,get_property_bool=function(k,d)return d end,get_property_number=function()return 0 end,
set_property_native=function(k,v)props[k]=v end,register_event=function()end,register_script_message=function(k,f)messages[k]=f end,
add_key_binding=function()end,observe_property=function()end,add_periodic_timer=function()end,add_timeout=function(_,f)timers[#timers+1]=f end,
osd_message=function()end,commandv=function(...)commands[#commands+1]={...};return true end,command_native_async=function(_,f)reply=f end}
package.preload['mp']=function()return mp end
package.preload['mp.options']=function()return {read_options=function(o)o.auto_enable=false end}end
package.preload['mp.utils']=function()return {file_info=function()return {is_file=true}end,format_json=function(t)return tostring(t)end,parse_json=function()return parsed end}end
package.preload['ffi']=function()return {cdef=function()end,new=function()return {}end,string=function()return directory..'/mpv.exe' end,load=function()return {GetModuleFileNameW=function()return 1 end,WideCharToMultiByte=function()return 1 end}end}end
assert(loadfile(target))()
assert(not props['user-data/windows-dv-inwindow'].available)
timers[1]();parsed={installed=false,state='missing'};reply(true,{status=0,stdout='x'})
messages['windows-dv-inwindow-enable']();assert(#commands==0,'missing dependency must not add filter')
messages['windows-dv-dependencies-refresh']();parsed={installed=true,state='installed-unverified'};reply(true,{status=0,stdout='x'})
assert(props['user-data/windows-dv-inwindow'].available)
assert(not props['user-data/windows-dv-inwindow'].windows_processed,'installed is not processing')
messages['windows-dv-inwindow-enable']();assert(#commands==1 and commands[1][1]=='vf')
messages['windows-dv-dependencies-refresh']();reply(false,nil)
assert(not props['user-data/windows-dv-inwindow'].available,'failed probe fails closed')
print('PASS actual control script dependency gate, explicit activation and failure status')
