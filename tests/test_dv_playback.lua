-- Exercise the controller with an mpv API boundary double; no renderer claims.
local p={['idle-active']=false,['current-vo']='gpu-next',['time-pos']=3,
 ['video-params']={colormatrix='dolbyvision',gamma='pq',['gamma-original']='hlg'},
 ['video-out-params']={colormatrix='dolbyvision',gamma='pq'},['vf']={}}
local callbacks,events,timers,commands={},{},{},{}
local overlays={};local fail=false
mp={log=function()end,get_property_native=function(k,d)if p[k]~=nil then return p[k] end return d end,
 get_property=function(k,d)return p[k] or d end,get_property_number=function(k,d)return p[k] or d end,
 get_property_bool=function(k,d)if p[k]~=nil then return p[k] end return d end,
 set_property_native=function(k,v)p[k]=v end,set_property_bool=function(k,v)p[k]=v;return true end,
 register_script_message=function(k,f)callbacks[k]=f end,register_event=function(k,f)events[k]=f end,
 observe_property=function()end,add_periodic_timer=function(_,f)timers.poll=f end,
 add_timeout=function(_,f)timers[#timers+1]=f;return {kill=function()end}end,
 create_osd_overlay=function()local o={update=function(self)overlays[#overlays+1]=self.data end,remove=function()end};return o end,
 osd_message=function()end,
 commandv=function(...)local c={...};commands[#commands+1]=c
  if c[1]=='vf-command' then
   if fail then return nil,'rejected' end
   p['vf-metadata/windows_dv'][c[3]..'-requested']=c[4]=='no' and 'off' or 'on'
  end
  if c[1]=='vf' then
   if fail then return nil,'test failure' end
   if c[2]=='add' then
    local spec=tostring(c[3]);local added=spec:match('^@([^:]+):') or ''
    p.vf[#p.vf+1]={label=added,name='format',params={
     ['dolbyvision']=spec:find('dolbyvision=no',1,true) and 'no' or nil,
     ['enhancement-layer']=spec:find('enhancement-layer=no',1,true) and 'no' or nil}}
   elseif c[2]=='remove' then
    local removed=tostring(c[3]):match('^@(.+)$')
    for i=#p.vf,1,-1 do if p.vf[i].label==removed then table.remove(p.vf,i) end end
   end
  end
  return true
 end}
package.preload['mp']=function()return mp end
package.preload['mp.utils']=function()return {format_json=function(t)return t end}end
package.loaded['mp']=nil;package.loaded['mp.utils']=nil
assert(loadfile(arg[1]))()
assert(timers.poll,'controller must poll processing state')
timers.poll();assert(#overlays==1 and overlays[1]:find('gpu%-next / libplacebo'),'actual renderer DV must announce')
timers.poll();assert(#overlays==1,'do not repeat announcement per frame')
local s=p['user-data/dv-playback'];assert(s.base_transfer=='hlg' and s.render_transfer=='pq','do not confuse original HLG with reconstructed DV/PQ')
for _,source in ipairs({{5,'dvhe.05'},{8,'dvhe.08'},{9,'dvav.09'},{10,'dav1.10'}}) do
 p['track-list']={{type='video',selected=true,['dolby-vision-profile']=source[1]}}
 timers.poll()
 assert(p['user-data/dv-playback'].profile_string==source[2],source[2]..' source identity was not retained')
end
p['track-list']={{type='video',selected=true,['dolby-vision-el-present']=true}}
timers.poll()
assert(p['user-data/dv-playback'].profile==7 and p['user-data/dv-playback'].profile_string=='dvhe.07',
 'a paired EL with missing container profile metadata must be identified as Profile 7')
events['start-file']()
p['track-list']={{type='video',selected=true,['dolby-vision-profile']=8}}
events['file-loaded']()
timers.poll()
callbacks['dv-enhancement-toggle']();assert(#p.vf==0,'single-layer P8 must not allow EL toggle')
p['track-list']={{type='video',selected=true,['dolby-vision-profile']=7,['dolby-vision-el-present']=true}}
p['user-data/windows-dv-inwindow']={enabled=true,windows_processed=true}
p['vf-metadata/windows_dv']={['comparison-controls']='yes',['rpu-disable-supported']='yes',['source-profile']='7'}
timers.poll();assert(p['user-data/dv-playback'].dual_layer)
local before=#commands;callbacks['dv-enhancement-toggle']()
assert(#p.vf==0 and p['vf-metadata/windows_dv']['el-requested']=='off','Windows EL must use in-place filter command')
assert(p['user-data/windows-dv-inwindow'].enabled,'Windows backend must stay enabled')
callbacks['dv-enhancement-toggle']();assert(p['vf-metadata/windows_dv']['el-requested']=='on')
callbacks['dv-rpu-toggle']();assert(p['vf-metadata/windows_dv']['rpu-requested']=='off')
callbacks['dv-rpu-toggle']();assert(p['vf-metadata/windows_dv']['rpu-requested']=='on')
p['vf-metadata/windows_dv']['source-profile']='5';p['track-list'][1]['dolby-vision-profile']=5
timers.poll();callbacks['dv-rpu-toggle']();assert(p['vf-metadata/windows_dv']['rpu-requested']=='on','P5 must retain necessary color interpretation')
p['track-list'][1]['dolby-vision-profile']=7;p['vf-metadata/windows_dv']={}

p['user-data/windows-dv-inwindow']={enabled=false};timers.poll()
local button;for _,c in ipairs(commands)do if c[3]=='set-button' and c[4]=='dv_el' then button=c[5]end end
assert(button and not button.hide and button.badge=='EL' and button.command,'main toolbar needs an actionable EL button')
 commands={};p.pause=true;callbacks['dv-enhancement-toggle']()
assert(p['user-data/dv-playback'].el_disabled and p.vf[1].params['enhancement-layer']=='no')
 local refreshed=false;for _,c in ipairs(commands)do if c[1]=='seek' then refreshed=true end end
 assert(refreshed,'EL comparison must refresh the current paused frame')
callbacks['dv-enhancement-toggle']();assert(not p['user-data/dv-playback'].el_disabled and #p.vf==0)
for _,c in ipairs(commands)do assert(c[1]~='script-message','EL toggle must not change backend')end
 callbacks['dv-rpu-toggle']()
 assert(p['user-data/dv-playback'].rpu_disabled and p['user-data/dv-playback'].el_disabled and
        not p['user-data/dv-playback'].can_toggle_el,
  'open renderer RPU-off comparison must expose its BL-only state')
 local rpu_filter;for _,f in ipairs(p.vf)do if f.label=='dv_rpu_off' then rpu_filter=f end end
 assert(rpu_filter and rpu_filter.params.dolbyvision=='no' and rpu_filter.params['enhancement-layer']=='no',
  'RPU-off must restore the compatible base layer and suppress EL reconstruction')
 refreshed=false;for _,c in ipairs(commands)do if c[1]=='seek' then refreshed=true end end
 assert(refreshed,'RPU comparison must refresh the current paused frame')
 callbacks['dv-rpu-toggle']()
 assert(not p['user-data/dv-playback'].rpu_disabled and not p['user-data/dv-playback'].el_disabled and #p.vf==0,
  'restoring RPU must restore the original DV and EL path')
 p.pause=false
fail=true;callbacks['dv-enhancement-toggle']();assert(not p['user-data/dv-playback'].el_disabled);fail=false
callbacks['dv-enhancement-toggle']();events['start-file']();assert(#p.vf==0 and not p['user-data/dv-playback'].el_disabled,'EL comparison must not leak to next file')
p['track-list']={};p['video-out-params']={gamma='hlg',colormatrix='bt.2020-ncl'};p['video-params']={gamma='hlg'};p['user-data/windows-dv-inwindow']={}
events['file-loaded']();local before=#overlays;timers.poll();assert(#overlays==before,'plain HLG must not announce DV')
print('PASS DV announcement, HLG source/effective distinction, dual-layer toggle, failure and file reset')
