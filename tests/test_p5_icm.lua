local p={['idle-active']=false,['video-params']={gamma='pq',colormatrix='dolbyvision'},
 ['icc-profile']='factory-explicit.icm',['icc-profile-auto']='yes',['treat-srgb-as-power22']='auto'}
local observers, callbacks, timers, writes={},{},{},{}
local function name(k)return k:match('^options/(.+)$') or k end
local function read(k,d)local n=name(k);local v=n~=k and p[n] or p[k];if v==nil then v=p[k] end;if v==nil then return d end;return v end
local function write(k,v)local n=name(k);local out=n~=k and (type(v)=='boolean' and (v and 'yes' or 'no') or tostring(v)) or v;if n~=k then writes[#writes+1]={n,out} end;p[k]=v;if n~=k then p[n]=out end;return true end
mp={get_property_bool=function(k,d)return read(k,d) end,
 get_property=function(k,d)return read(k,d) end,
 get_property_native=function(k,d)return read(k,d) end,
 set_property=write,set_property_native=write,set_property_bool=write,
 commandv=function()end,osd_message=function()end,msg={error=error,warn=function()end},
 register_script_message=function(k,f)callbacks[k]=f end,
 observe_property=function(k,_,f)observers[k]=f end,register_event=function()end,
 add_timeout=function(_,f)timers[#timers+1]=f;return {kill=function()end}end}
package.preload['mp.utils']=function()return {format_json=function()return '{}' end}end
assert(loadfile(arg[1]))()
p['user-data/windows-dv-inwindow']={windows_processed=true,float_output=true,relative_srgb_output=true}
callbacks.mode('sdr-reference');timers[#timers-1]()
assert(p['target-trc']=='auto' and p['icc-profile-auto']=='yes')
writes={}
-- New per-frame RPU peaks must not change the already established output contract.
for i=1,3 do
 p['video-params']['max-pq-y']=.6+i*.01
 observers['video-params']();timers[#timers]()
end
for _,w in ipairs(writes)do
 assert(w[1]~='target-trc' or w[2]=='auto','relative SDR temporarily switched to '..w[2])
end
assert(#writes==0,'unchanged target options rewritten '..#writes..' times')
print('PASS changing RPU peaks do not toggle transfer or reload unchanged color options')
-- A manual SDR peak must survive fallback but must never briefly replace auto in DV.
p['user-data/windows-dv-inwindow']={windows_processed=false}
callbacks.set('target-peak','100');assert(p['target-peak']=='100')
for _,state in ipairs({{windows_processed=true,float_output=true,relative_srgb_output=true},{windows_processed=true,float_output=false}})do
 p['user-data/windows-dv-inwindow']=state;writes={}
 observers['video-params']();timers[#timers]()
 for _,w in ipairs(writes)do assert(w[1]~='target-peak' or w[2]=='auto','Windows peak temporarily changed to '..w[2])end
 assert(p['target-peak']=='auto')
end
p['user-data/windows-dv-inwindow']={windows_processed=false};observers['video-params']();timers[#timers]()
assert(p['target-peak']=='100','SDR user override must return when Windows processing is off')
print('PASS Windows DV peak remains auto without erasing original-mapping override')

-- A curve chosen while using original mapping must not leak into Windows SDR.
p['user-data/windows-dv-inwindow']={windows_processed=false}
callbacks.set('tone-mapping','hable');assert(p['tone-mapping']=='hable')
p['user-data/windows-dv-inwindow']={windows_processed=true,float_output=true,relative_srgb_output=true}
observers['video-params']();timers[#timers]()
assert(p['tone-mapping']=='auto','old hable curve still active on Windows output')
callbacks.set('tone-mapping','reinhard')
assert(p['tone-mapping']=='auto','script message bypassed locked control')
p['user-data/windows-dv-inwindow']={windows_processed=false}
observers['video-params']();timers[#timers]()
assert(p['tone-mapping']=='hable','original mapping curve was lost')
print('PASS Windows output ignores old/manual mapping curves and restores them on exit')

-- P5's accepted display output bypasses MPV ICM, without a global disable.
p['icc-profile']='factory-explicit.icm';p['icc-profile-auto']='yes'
p['user-data/windows-dv-inwindow']={windows_processed=true,float_output=true,relative_srgb_output=true,p5_native_sdr=true}
observers['user-data/windows-dv-inwindow']('',p['user-data/windows-dv-inwindow']);timers[#timers]()
assert(p['icc-profile-auto']=='no','P5 still applies target ICM')
assert(p['icc-profile']=='','P5 explicit ICC remains active')
p['user-data/windows-dv-inwindow']={windows_processed=true,float_output=true,relative_srgb_output=true,p5_native_sdr=false}
observers['user-data/windows-dv-inwindow']('',p['user-data/windows-dv-inwindow']);timers[#timers]()
assert(p['icc-profile-auto']=='yes' and p['icc-profile']=='factory-explicit.icm','P8.4 settings not restored')
p['user-data/windows-dv-inwindow']={windows_processed=true,float_output=true,relative_srgb_output=true,p5_native_sdr=true}
observers['video-params']();timers[#timers]()
p['user-data/dcp']={active=true};observers['user-data/dcp']();timers[#timers]()
assert(p['icc-profile']=='factory-explicit.icm','DCP explicit ICC restoration skipped')
assert(p['file-local-options/icc-profile-auto']==true,'DCP ICM must remain active')
print('PASS P5-only ICM bypass; restore P8.4 and DCP')

-- Native SDR output handling is selected by the output contract, not P8 HDR tags.
p['user-data/dcp']={};p['icc-profile']='factory-explicit.icm';p['icc-profile-auto']='yes'
p['user-data/windows-dv-inwindow']={windows_processed=true,float_output=true,relative_srgb_output=true,p8_native_sdr=true,native_sdr_output=true}
observers['user-data/windows-dv-inwindow']('',p['user-data/windows-dv-inwindow']);timers[#timers]()
assert(p['icc-profile-auto']=='no' and p['icc-profile']=='','P8 native SDR should not receive a second MPV display transform')
assert(p['target-trc']=='srgb','P8 native SDR must keep negotiated sRGB transfer, not an auto gamma reinterpretation')
assert(p['target-prim']=='bt.709' and p['target-peak']=='auto')
assert(p['treat-srgb-as-power22']=='no','P8 native output needs explicit piecewise sRGB on both sides')
callbacks.set('icc-profile-auto','yes');assert(p['icc-profile-auto']=='no','manual override must not double-convert native output')
p['user-data/windows-dv-inwindow']={windows_processed=true,float_output=false}
observers['user-data/windows-dv-inwindow']('',p['user-data/windows-dv-inwindow']);timers[#timers]()
assert(p['icc-profile']=='factory-explicit.icm' and p['icc-profile-auto']=='yes','P7 legacy ICC not restored')
assert(p['target-trc']=='bt.1886','P7 legacy transfer changed')
assert(p['treat-srgb-as-power22']=='auto','P7 transfer preference not restored')
print('PASS P8 native SDR output policy and P7 restoration')
