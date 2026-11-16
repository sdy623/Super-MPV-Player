local target=arg[1];local p={};local observers,timers={},{}
mp={get_property_bool=function(_,d)return d end,get_property=function(_,d)return d end,get_property_native=function(k,d)return p[k] or d end,
set_property=function(k,v)p[k]=v;return true end,set_property_native=function(k,v)p[k]=v end,set_property_bool=function(k,v)p[k]=v end,commandv=function()end,
msg={error=function()end,warn=function()end},osd_message=function()end,register_script_message=function()end,
observe_property=function(k,_,f)observers[k]=f end,register_event=function()end,
add_timeout=function(_,f)timers[#timers+1]=f;return {kill=function()end}end}
package.preload['mp.utils']=function()return {format_json=function()return '{}' end}end
assert(loadfile(target))()
observers['video-params']();timers[#timers]();assert(p['icc-profile-auto']=='no')
p['user-data/windows-dv-inwindow']={windows_processed=true};observers['user-data/windows-dv-inwindow']('',p['user-data/windows-dv-inwindow']);timers[#timers]()
assert(p['icc-profile-auto']=='yes');assert(p['target-trc']=='bt.1886');assert(p['target-peak']=='203')
p['user-data/windows-dv-inwindow']={windows_processed=true,float_output=true};observers['user-data/windows-dv-inwindow']('',p['user-data/windows-dv-inwindow']);timers[#timers]()
assert(p['target-trc']=='auto' and p['target-peak']=='auto','scRGB must follow selected display mapping, not fixed BGRA mode')
p['user-data/dcp']={active=true};observers['video-params']();timers[#timers]();assert(p['file-local-options/icc-profile-auto']==true)
p['user-data/dcp']={}
p['user-data/windows-dv-inwindow']={windows_processed=false};observers['user-data/windows-dv-inwindow']('',p['user-data/windows-dv-inwindow']);timers[#timers]()
assert(p['icc-profile-auto']=='no');assert(p['target-trc']=='auto');assert(p['target-peak']=='auto')
print('PASS Windows SDR uses matching system ICC; disabling restores display-auto')
