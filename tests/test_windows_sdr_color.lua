local target=arg[1];local p={};local observers,timers={},{}
local function name(k)return k:match('^options/(.+)$') or k end
local function read(k,d)local n=name(k);local v=n~=k and p[n] or p[k];if v==nil then v=p[k] end;if v==nil then return d end;return v end
local function write(k,v)local n=name(k);p[k]=v;if n~=k then p[n]=type(v)=='boolean' and (v and 'yes' or 'no') or tostring(v) end;return true end
mp={get_property_bool=function(k,d)return read(k,d) end,get_property=function(k,d)return read(k,d) end,get_property_native=function(k,d)return read(k,d) end,
set_property=write,set_property_native=write,set_property_bool=write,commandv=function()end,
msg={error=function()end,warn=function()end},osd_message=function()end,register_script_message=function()end,
observe_property=function(k,_,f)observers[k]=f end,register_event=function()end,
add_timeout=function(_,f)timers[#timers+1]=f;return {kill=function()end}end}
package.preload['mp.utils']=function()return {format_json=function()return '{}' end}end
assert(loadfile(target))()
observers['video-params']();timers[#timers]();assert(p['icc-profile-auto']=='no', 'Display Auto avoids an extra ICC transform')
p['user-data/windows-dv-inwindow']={windows_processed=true};observers['user-data/windows-dv-inwindow']('',p['user-data/windows-dv-inwindow']);timers[#timers]()
assert(p['icc-profile-auto']=='yes');assert(p['target-trc']=='bt.1886');assert(p['target-peak']=='auto')
p['user-data/windows-dv-inwindow']={windows_processed=true,float_output=true};observers['user-data/windows-dv-inwindow']('',p['user-data/windows-dv-inwindow']);timers[#timers]()
assert(p['target-trc']=='auto' and p['target-peak']=='auto','scRGB must follow selected display mapping, not fixed BGRA mode')
p['user-data/dcp']={active=true};observers['video-params']();timers[#timers]();assert(p['file-local-options/icc-profile-auto']==true)
p['user-data/dcp']={}
p['user-data/windows-dv-inwindow']={windows_processed=false};observers['user-data/windows-dv-inwindow']('',p['user-data/windows-dv-inwindow']);timers[#timers]()
assert(p['icc-profile-auto']=='no');assert(p['target-trc']=='auto');assert(p['target-peak']=='auto')
print('PASS Windows SDR uses matching system ICC; disabling restores Display Auto')
